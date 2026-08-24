#pragma once

#include "recall_memory/codegraph/indexer.hpp"
#include "recall_memory/memory/service.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace recall_memory {

class HttpServer {
public:
    HttpServer(Store& store, CodeGraphIndexer& indexer, MemoryService& memory);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool listen(const std::string& host, std::uint16_t port);
    bool bind(const std::string& host, std::uint16_t port);
    bool listen_after_bind();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace recall_memory
