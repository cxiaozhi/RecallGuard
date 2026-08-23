#include "recall_memory/mcp/server.hpp"

#include "recall_memory/domain/json.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace recall_memory {
namespace {

using json = nlohmann::json;

json tool_definition(std::string name, std::string description, json properties, json required = json::array()) {
    return {
        {"name", std::move(name)},
        {"description", std::move(description)},
        {"inputSchema", {
            {"type", "object"}, {"properties", std::move(properties)},
            {"required", std::move(required)}, {"additionalProperties", false},
        }},
    };
}

json tools() {
    const json string_array = {{"type", "array"}, {"items", {{"type", "string"}}}};
    const json scope_array = {
        {"type", "array"}, {"items", {
            {"type", "object"},
            {"properties", {
                {"kind", {{"type", "string"}, {"enum", {"agent", "context", "resource", "entity", "workflow", "domain", "file", "symbol", "module", "tag"}}}},
                {"value", {{"type", "string"}}},
            }},
            {"required", {"kind", "value"}}, {"additionalProperties", false},
        }},
    };
    return json::array({
        tool_definition(
            "recall_memory_create_memory_space",
            "创建或取得一个不依赖代码仓库的通用记忆空间。",
            {{"name", {{"type", "string"}, {"description", "项目、用户、团队或业务域名称"}}}},
            {"name"}),
        tool_definition(
            "recall_memory_index_workspace",
            "增量索引 C/C++ 工作区并刷新可选的代码图谱。",
            {{"rootPath", {{"type", "string"}, {"description", "工作区绝对路径"}}}},
            {"rootPath"}),
        tool_definition(
            "recall_memory_graph_status",
            "返回工作区代码图谱的文件、符号、边和未解析边数量。",
            {{"workspaceId", {{"type", "string"}}}}, {"workspaceId"}),
        tool_definition(
            "recall_memory_recall",
            "根据当前任务、上下文、资源、实体或可选代码信号召回已验证经验。",
            {
                {"workspaceId", {{"type", "string"}}},
                {"task", {{"type", "string"}}},
                {"errors", string_array}, {"agents", string_array}, {"contexts", string_array},
                {"resources", string_array}, {"entities", string_array}, {"workflows", string_array},
                {"scopes", scope_array},
                {"files", string_array}, {"symbols", string_array},
                {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                {"includeCandidates", {{"type", "boolean"}}},
            },
            {"workspaceId", "task"}),
        tool_definition(
            "recall_memory_guard",
            "用历史经验检查计划或任务结果，返回重复错误风险、不变量和必要验证。",
            {
                {"workspaceId", {{"type", "string"}}}, {"task", {{"type", "string"}}},
                {"diffSummary", {{"type", "string"}}}, {"changedAgents", string_array},
                {"changedContexts", string_array}, {"changedResources", string_array},
                {"changedEntities", string_array}, {"changedWorkflows", string_array},
                {"changedScopes", scope_array},
                {"changedFiles", string_array},
                {"changedSymbols", string_array}, {"graphDepth", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5}}},
            },
            {"workspaceId", "task"}),
        tool_definition(
            "recall_memory_propose_experience",
            "提交一条尚未受信的候选经验；此工具不能自行验证或激活经验。",
            {
                {"workspaceId", {{"type", "string"}}},
                {"kind", {{"type", "string"}, {"enum", {"task_outcome", "operational_lesson", "user_preference", "domain_fact", "bug_fix", "architecture_decision", "procedure", "failed_approach", "project_fact"}}}},
                {"title", {{"type", "string"}}}, {"trigger", {{"type", "string"}}},
                {"symptom", {{"type", "string"}}}, {"rootCause", {{"type", "string"}}},
                {"invariant", {{"type", "string"}}}, {"fixSummary", {{"type", "string"}}},
                {"scopes", scope_array},
                {"evidence", {{"type", "array"}, {"items", {{"type", "object"}}}}},
                {"verificationSteps", {{"type", "array"}, {"items", {{"type", "object"}}}}},
                {"confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
            },
            {"workspaceId", "kind", "title", "trigger", "scopes"}),
        tool_definition(
            "recall_memory_feedback",
            "将召回经验标记为有用、过时或错误。",
            {
                {"experienceId", {{"type", "string"}}},
                {"value", {{"type", "string"}, {"enum", {"useful", "outdated", "incorrect"}}}},
                {"note", {{"type", "string"}}},
            },
            {"experienceId", "value"}),
    });
}

json tool_success(const json& value) {
    return {
        {"content", json::array({{{"type", "text"}, {"text", value.dump(2)}}})},
        {"structuredContent", value},
        {"isError", false},
    };
}

json tool_failure(const std::exception& exception) {
    return {
        {"content", json::array({{{"type", "text"}, {"text", exception.what()}}})},
        {"isError", true},
    };
}

json rpc_error(const json& id, int code, std::string message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
}

}  // namespace

McpServer::McpServer(Store& store, CodeGraphIndexer& indexer, MemoryService& memory)
    : store_(store), indexer_(indexer), memory_(memory) {}

int McpServer::run(std::istream& input, std::ostream& output) {
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        json request;
        json id = nullptr;
        try {
            request = json::parse(line);
            id = request.value("id", json(nullptr));
            const auto method = request.at("method").get<std::string>();
            if (method.starts_with("notifications/")) continue;

            json result;
            if (method == "initialize") {
                const auto requested_version = request.value("params", json::object()).value(
                    "protocolVersion", std::string("2025-06-18"));
                result = {
                    {"protocolVersion", requested_version},
                    {"capabilities", {{"tools", {{"listChanged", false}}}}},
                    {"serverInfo", {{"name", "Recall Memory"}, {"version", "0.1.0"}}},
                    {"instructions", "开始任务前先召回相关经验，接受结果前运行防重复检查。代码任务可先建立代码图谱以增强召回。"},
                };
            } else if (method == "ping") {
                result = json::object();
            } else if (method == "tools/list") {
                result = {{"tools", tools()}};
            } else if (method == "tools/call") {
                const auto params = request.at("params");
                const auto name = params.at("name").get<std::string>();
                const auto arguments = params.value("arguments", json::object());
                try {
                    if (name == "recall_memory_create_memory_space") {
                        const auto space_name = arguments.at("name").get<std::string>();
                        result = tool_success({
                            {"workspaceId", store_.ensure_memory_space(space_name)}, {"name", space_name}});
                    } else if (name == "recall_memory_index_workspace") {
                        result = tool_success(indexer_.index(arguments.at("rootPath").get<std::string>()));
                    } else if (name == "recall_memory_graph_status") {
                        result = tool_success(store_.graph_status(arguments.at("workspaceId").get<std::string>()));
                    } else if (name == "recall_memory_recall") {
                        result = tool_success(memory_.recall(arguments.get<RecallRequest>()));
                    } else if (name == "recall_memory_guard") {
                        result = tool_success(memory_.guard(arguments.get<GuardRequest>()));
                    } else if (name == "recall_memory_propose_experience") {
                        result = tool_success(memory_.propose(arguments.get<ExperienceDraft>()));
                    } else if (name == "recall_memory_feedback") {
                        memory_.feedback(
                            arguments.at("experienceId").get<std::string>(),
                            arguments.at("value").get<std::string>(),
                            arguments.value("note", std::string{}));
                        result = tool_success({{"accepted", true}});
                    } else {
                        throw std::invalid_argument("Unknown tool: " + name);
                    }
                } catch (const std::exception& exception) {
                    result = tool_failure(exception);
                }
            } else {
                output << rpc_error(id, -32601, "Method not found: " + method).dump() << '\n' << std::flush;
                continue;
            }
            output << json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}.dump()
                   << '\n' << std::flush;
        } catch (const json::exception& exception) {
            output << rpc_error(id, -32602, exception.what()).dump() << '\n' << std::flush;
        } catch (const std::exception& exception) {
            output << rpc_error(id, -32603, exception.what()).dump() << '\n' << std::flush;
        }
    }
    return 0;
}

}  // namespace recall_memory
