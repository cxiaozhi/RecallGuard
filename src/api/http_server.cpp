#include "recall_memory/api/http_server.hpp"

#include "recall_memory/domain/json.hpp"
#include "recall_memory/mcp/server.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>

namespace recall_memory {
namespace {

using JsonHandler = std::function<nlohmann::json(const httplib::Request&)>;

void json_response(httplib::Response& response, int status, const nlohmann::json& body) {
    response.status = status;
    response.set_content(body.dump(), "application/json; charset=utf-8");
}

auto route(JsonHandler handler) {
    return [handler = std::move(handler)](const httplib::Request& request, httplib::Response& response) {
        try {
            json_response(response, 200, handler(request));
        } catch (const nlohmann::json::exception& exception) {
            json_response(response, 400, {{"error", "invalid_json"}, {"message", exception.what()}});
        } catch (const std::invalid_argument& exception) {
            json_response(response, 400, {{"error", "invalid_request"}, {"message", exception.what()}});
        } catch (const std::exception& exception) {
            json_response(response, 500, {{"error", "internal_error"}, {"message", exception.what()}});
        }
    };
}

nlohmann::json body(const httplib::Request& request) {
    if (request.body.empty()) return nlohmann::json::object();
    return nlohmann::json::parse(request.body);
}

bool valid_origin(const httplib::Request& request) {
    if (!request.has_header("Origin")) return true;
    static const std::regex loopback_origin(R"(^https?://(127\.0\.0\.1|localhost)(:[0-9]+)?$)");
    return std::regex_match(request.get_header_value("Origin"), loopback_origin);
}

}  // namespace

struct HttpServer::Impl {
    Impl(Store& store, CodeGraphIndexer& indexer, MemoryService& memory)
        : mcp(store, indexer, memory) {
        server.set_payload_max_length(2U * 1024U * 1024U);

        server.Post("/mcp", [this](const httplib::Request& request, httplib::Response& response) {
            if (!valid_origin(request)) {
                json_response(response, 403, {
                    {"jsonrpc", "2.0"}, {"id", nullptr},
                    {"error", {{"code", -32000}, {"message", "Origin is not allowed"}}},
                });
                return;
            }
            const auto result = mcp.handle(request.body);
            if (!result) {
                response.status = 202;
                return;
            }
            response.status = 200;
            response.set_content(*result, "application/json; charset=utf-8");
        });
        server.Get("/mcp", [](const httplib::Request&, httplib::Response& response) {
            response.status = 405;
            response.set_header("Allow", "POST");
        });

        server.Get("/health", route([&store](const httplib::Request&) {
            return nlohmann::json{{"status", "ok"}, {"service", "Recall Memory"}, {"version", "0.1.0"}};
        }));

        server.Post("/v1/workspaces/index", route([&indexer](const httplib::Request& request) {
            const auto json = body(request);
            const auto root = json.at("rootPath").get<std::string>();
            return nlohmann::json(indexer.index(std::filesystem::path(root)));
        }));

        server.Post("/v1/memory-spaces", route([&store](const httplib::Request& request) {
            const auto json = body(request);
            const auto name = json.at("name").get<std::string>();
            return nlohmann::json{{"workspaceId", store.ensure_memory_space(name)}, {"name", name}};
        }));

        server.Get(R"(/v1/workspaces/([^/]+)/graph/status)", route([&store](const httplib::Request& request) {
            return nlohmann::json(store.graph_status(request.matches[1].str()));
        }));

        server.Post("/v1/recall", route([&memory](const httplib::Request& request) {
            return nlohmann::json(memory.recall(body(request).get<RecallRequest>()));
        }));

        server.Post("/v1/guard", route([&memory](const httplib::Request& request) {
            return nlohmann::json(memory.guard(body(request).get<GuardRequest>()));
        }));

        server.Post("/v1/experiences/propose", route([&memory](const httplib::Request& request) {
            return nlohmann::json(memory.propose(body(request).get<ExperienceDraft>()));
        }));

        server.Post(R"(/v1/experiences/([^/]+)/verify)", route([&memory](const httplib::Request& request) {
            const auto json = body(request);
            return nlohmann::json(memory.verify(
                request.matches[1].str(), json.at("evidence").get<std::vector<Evidence>>()));
        }));

        server.Post(R"(/v1/experiences/([^/]+)/stale)", route([&memory](const httplib::Request& request) {
            return nlohmann::json(memory.mark_stale(request.matches[1].str()));
        }));

        server.Post(R"(/v1/experiences/([^/]+)/feedback)", route([&memory](const httplib::Request& request) {
            const auto json = body(request);
            memory.feedback(
                request.matches[1].str(),
                json.at("value").get<std::string>(),
                json.value("note", std::string{}));
            return nlohmann::json{{"accepted", true}};
        }));
    }

    McpServer mcp;
    httplib::Server server;
};

HttpServer::HttpServer(Store& store, CodeGraphIndexer& indexer, MemoryService& memory)
    : impl_(std::make_unique<Impl>(store, indexer, memory)) {}

HttpServer::~HttpServer() = default;

bool HttpServer::listen(const std::string& host, std::uint16_t port) {
    return impl_->server.listen(host, static_cast<int>(port));
}

bool HttpServer::bind(const std::string& host, std::uint16_t port) {
    return impl_->server.bind_to_port(host, static_cast<int>(port));
}

bool HttpServer::listen_after_bind() { return impl_->server.listen_after_bind(); }

void HttpServer::stop() { impl_->server.stop(); }

}  // namespace recall_memory
