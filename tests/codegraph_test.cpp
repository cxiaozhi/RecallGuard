#include "recall_memory/codegraph/cpp_parser.hpp"
#include "recall_memory/codegraph/indexer.hpp"
#include "recall_memory/domain/json.hpp"
#include "recall_memory/storage/store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path temporary_path(std::string_view prefix) {
    const auto value = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(value));
}

}  // namespace

TEST_CASE("C++ parser extracts stable symbols and syntax-level calls") {
    const std::string source = R"cpp(
        namespace auth {
        class TokenCache {
        public:
            void store(int generation) { persist(generation); }
        };
        void refresh() { TokenCache cache; cache.store(4); }
        }
    )cpp";

    recall_memory::CppParser parser;
    const auto parsed = parser.parse("ws_test", "src/auth.cpp", source);

    INFO(nlohmann::json(parsed.nodes).dump(2));
    REQUIRE(parsed.nodes.size() >= 5);
    REQUIRE(std::ranges::any_of(parsed.nodes, [](const auto& node) {
        return node.name == "TokenCache" && node.qualified_name == "auth::TokenCache";
    }));
    REQUIRE(std::ranges::any_of(parsed.nodes, [](const auto& node) {
        return node.name == "store" && node.qualified_name == "auth::TokenCache::store";
    }));
    REQUIRE(std::ranges::any_of(parsed.edges, [](const auto& edge) {
        return edge.kind == recall_memory::GraphEdgeKind::calls && edge.target_name == "persist";
    }));
}

TEST_CASE("Indexer incrementally stores a workspace graph") {
    const auto root = temporary_path("recall-memory-graph-");
    std::filesystem::create_directories(root / "src");
    {
        std::ofstream source(root / "src" / "main.cpp");
        source << "void helper() {}\nint main() { helper(); }\n";
    }

    const auto database_path = root / "memory.db";
    {
        recall_memory::Store store(database_path);
        recall_memory::CodeGraphIndexer indexer(store);
        const auto first = indexer.index(root);
        REQUIRE(first.indexed_files == 1);
        REQUIRE(first.node_count >= 3);
        REQUIRE(first.edge_count >= 3);

        const auto second = indexer.index(root);
        REQUIRE(second.indexed_files == 0);
        REQUIRE(second.unchanged_files == 1);
    }
    std::filesystem::remove_all(root);
}
