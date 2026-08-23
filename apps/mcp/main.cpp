#include "recall_memory/codegraph/indexer.hpp"
#include "recall_memory/mcp/server.hpp"
#include "recall_memory/memory/service.hpp"
#include "recall_memory/storage/store.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main() {
    try {
        const auto* configured = std::getenv("RECALL_MEMORY_DB");
        const std::filesystem::path database = configured ? configured : "recall-memory.db";
        recall_memory::Store store(database);
        recall_memory::CodeGraphIndexer indexer(store);
        recall_memory::MemoryService memory(store);
        recall_memory::McpServer server(store, indexer, memory);
        return server.run(std::cin, std::cout);
    } catch (const std::exception& exception) {
        std::cerr << "recall-memory-mcp: " << exception.what() << '\n';
        return 1;
    }
}
