#include "recall_memory/codegraph/indexer.hpp"
#include "recall_memory/memory/service.hpp"
#include "recall_memory/storage/store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path temporary_workspace() {
    const auto value = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("recall-memory-" + std::to_string(value));
    std::filesystem::create_directories(root / "src");
    std::ofstream source(root / "src" / "token_cache.cpp");
    source << R"cpp(
        class TokenCache {
        public:
            void store(int generation) { persist(generation); }
        };
    )cpp";
    return root;
}

}  // namespace

TEST_CASE("Verified experience is recalled through text and graph scopes") {
    const auto root = temporary_workspace();
    {
        recall_memory::Store store(root / "recall-memory.db");
        recall_memory::CodeGraphIndexer indexer(store);
        recall_memory::MemoryService memory(store);
        const auto report = indexer.index(root);

        recall_memory::ExperienceDraft draft{
            .workspace_id = report.workspace_id,
            .kind = recall_memory::ExperienceKind::bug_fix,
            .title = "Prevent stale token overwrite",
            .trigger = "Changing TokenCache storage or refresh ordering",
            .symptom = "An older token replaces a newly refreshed token",
            .root_cause = "Token generations were not compared before persistence",
            .invariant = "Token generation must increase monotonically",
            .fix_summary = "Reject writes with an older generation",
            .scopes = {
                {recall_memory::ScopeKind::symbol, "TokenCache::store"},
                {recall_memory::ScopeKind::file, "src/token_cache.cpp"},
            },
            .evidence = {{"commit", "git:abc123", std::nullopt}},
            .verification_steps = {{"ctest -R token_cache", std::nullopt}},
            .confidence = 0.95,
        };

        const auto candidate = memory.propose(draft);
        REQUIRE(memory.recall({
            .workspace_id = report.workspace_id,
            .task = "change TokenCache store behavior",
            .files = {"src/token_cache.cpp"},
        }).empty());

        memory.verify(candidate.id, {{"test", "ctest:token_cache", std::nullopt}});
        const auto recalled = memory.recall({
            .workspace_id = report.workspace_id,
            .task = "change cache persistence",
            .files = {"src/token_cache.cpp"},
            .symbols = {"TokenCache::store"},
        });
        REQUIRE(recalled.size() == 1);
        REQUIRE(recalled.front().score > 0.5);

        const auto guard = memory.guard({
            .workspace_id = report.workspace_id,
            .task = "refactor token storage",
            .diff_summary = "change the persistence ordering",
            .changed_files = {"src/token_cache.cpp"},
            .changed_symbols = {"TokenCache::store"},
        });
        REQUIRE(guard.risk == "high");
        REQUIRE(guard.invariants == std::vector<std::string>{"Token generation must increase monotonically"});
        REQUIRE(guard.required_verifications.size() == 1);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("General agent experience is recalled without a code graph signal") {
    const auto root = temporary_workspace();
    {
        recall_memory::Store store(root / "general-memory.db");
        recall_memory::MemoryService memory(store);
        const auto memory_space_id = store.ensure_memory_space("finance-operations");

        const auto candidate = memory.propose({
            .workspace_id = memory_space_id,
            .kind = recall_memory::ExperienceKind::operational_lesson,
            .title = "Confirm the reporting period before exporting",
            .trigger = "Generating a monthly finance report",
            .symptom = "The report used the previous month by mistake",
            .root_cause = "The agent inferred the period instead of reading task context",
            .invariant = "Use the explicitly selected reporting period",
            .fix_summary = "Bind export parameters to the selected period",
            .scopes = {
                {recall_memory::ScopeKind::workflow, "monthly-report-export"},
                {recall_memory::ScopeKind::resource, "finance-report"},
            },
            .confidence = 0.9,
        });
        memory.verify(candidate.id, {{"user", "approval:finance-owner", std::nullopt}});

        const auto recalled = memory.recall({
            .workspace_id = memory_space_id,
            .task = "Export the selected monthly report",
            .resources = {"finance-report"},
            .workflows = {"monthly-report-export"},
        });
        REQUIRE(recalled.size() == 1);
        REQUIRE(recalled.front().score > 0.5);

        const auto guard = memory.guard({
            .workspace_id = memory_space_id,
            .task = "Deliver the monthly finance report",
            .changed_resources = {"finance-report"},
            .changed_workflows = {"monthly-report-export"},
        });
        REQUIRE(guard.risk == "high");
        REQUIRE(guard.invariants == std::vector<std::string>{"Use the explicitly selected reporting period"});
    }
    std::filesystem::remove_all(root);
}
