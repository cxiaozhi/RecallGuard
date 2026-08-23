#pragma once

#include "recall_memory/codegraph/indexer.hpp"
#include "recall_memory/memory/service.hpp"

#include <iosfwd>

namespace recall_memory {

class McpServer {
public:
    McpServer(Store& store, CodeGraphIndexer& indexer, MemoryService& memory);

    int run(std::istream& input, std::ostream& output);

private:
    Store& store_;
    CodeGraphIndexer& indexer_;
    MemoryService& memory_;
};

}  // namespace recall_memory
