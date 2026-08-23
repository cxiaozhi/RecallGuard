#include "recall_memory/api/http_server.hpp"
#include "recall_memory/codegraph/indexer.hpp"
#include "recall_memory/memory/service.hpp"
#include "recall_memory/storage/store.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::filesystem::path database{"recall-memory.db"};
    std::string host{"127.0.0.1"};
    std::uint16_t port{47831};
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto next = [&]() -> std::string {
            if (index + 1 >= argc) throw std::invalid_argument("Missing value for " + argument);
            return argv[++index];
        };
        if (argument == "--db") options.database = next();
        else if (argument == "--host") options.host = next();
        else if (argument == "--port") {
            const auto value = std::stoul(next());
            if (value == 0 || value > 65535) throw std::invalid_argument("Port must be between 1 and 65535");
            options.port = static_cast<std::uint16_t>(value);
        } else if (argument == "--help") {
            std::cout << "recall-memoryd [--db PATH] [--host 127.0.0.1] [--port 47831]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + argument);
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        recall_memory::Store store(options.database);
        recall_memory::CodeGraphIndexer indexer(store);
        recall_memory::MemoryService memory(store);
        recall_memory::HttpServer server(store, indexer, memory);
        std::cout << "Recall Memory listening on http://" << options.host << ':' << options.port << '\n';
        return server.listen(options.host, options.port) ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << "recall-memoryd: " << exception.what() << '\n';
        return 1;
    }
}
