#pragma once

#include "recall_memory/codegraph/indexer.hpp"
#include "recall_memory/memory/service.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace recall_memory {

class McpServer {
public:
    McpServer(Store& store, CodeGraphIndexer& indexer, MemoryService& memory);

    std::optional<std::string> handle(std::string_view message);

private:
    Store& store_;
    CodeGraphIndexer& indexer_;
    MemoryService& memory_;
};

}  // namespace recall_memory
