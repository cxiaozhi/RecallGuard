#include "recall_memory/codegraph/indexer.hpp"
#include "recall_memory/mcp/server.hpp"
#include "recall_memory/memory/service.hpp"
#include "recall_memory/storage/store.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <string>

TEST_CASE("Embedded MCP handles requests and accepts notifications") {
    const auto value = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto database = std::filesystem::temp_directory_path() /
                          ("recall-memory-embedded-" + std::to_string(value) + ".db");
    {
        recall_memory::Store store(database);
        recall_memory::CodeGraphIndexer indexer(store);
        recall_memory::MemoryService memory(store);
        recall_memory::McpServer server(store, indexer, memory);

        const auto initialize = server.handle(R"json({
            "jsonrpc":"2.0",
            "id":1,
            "method":"initialize",
            "params":{"protocolVersion":"2026-07-28"}
        })json");
        REQUIRE(initialize.has_value());
        const auto initialized = nlohmann::json::parse(*initialize);
        REQUIRE(initialized.at("result").at("serverInfo").at("name") == "Recall Memory");

        const auto create = server.handle(R"json({
            "jsonrpc":"2.0",
            "id":2,
            "method":"tools/call",
            "params":{
                "name":"recall_memory_create_memory_space",
                "arguments":{"name":"embedded-mcp-test"}
            }
        })json");
        REQUIRE(create.has_value());
        const auto created = nlohmann::json::parse(*create);
        REQUIRE(created.at("result").at("isError") == false);
        REQUIRE(created.at("result").at("structuredContent").at("workspaceId")
                    .get<std::string>().starts_with("ms_"));

        REQUIRE_FALSE(server.handle(R"json({
            "jsonrpc":"2.0",
            "method":"notifications/initialized"
        })json").has_value());
    }
    std::error_code error;
    std::filesystem::remove(database, error);
}
