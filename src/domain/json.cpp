#include "recall_memory/domain/json.hpp"

#include <stdexcept>
#include <string_view>

namespace recall_memory {
namespace {

template <typename T>
T enum_from_string(const std::string& value, std::initializer_list<std::pair<std::string_view, T>> values) {
    for (const auto& [name, item] : values) {
        if (value == name) {
            return item;
        }
    }
    throw std::invalid_argument("Unknown enum value: " + value);
}

template <typename T>
void read_optional(const nlohmann::json& json, const char* key, std::optional<T>& output) {
    if (json.contains(key) && !json.at(key).is_null()) {
        output = json.at(key).get<T>();
    } else {
        output.reset();
    }
}

}  // namespace

std::string to_string(ExperienceKind value) {
    switch (value) {
        case ExperienceKind::task_outcome: return "task_outcome";
        case ExperienceKind::operational_lesson: return "operational_lesson";
        case ExperienceKind::user_preference: return "user_preference";
        case ExperienceKind::domain_fact: return "domain_fact";
        case ExperienceKind::bug_fix: return "bug_fix";
        case ExperienceKind::architecture_decision: return "architecture_decision";
        case ExperienceKind::procedure: return "procedure";
        case ExperienceKind::failed_approach: return "failed_approach";
        case ExperienceKind::project_fact: return "project_fact";
    }
    throw std::invalid_argument("Invalid ExperienceKind");
}

std::string to_string(ExperienceStatus value) {
    switch (value) {
        case ExperienceStatus::candidate: return "candidate";
        case ExperienceStatus::verified: return "verified";
        case ExperienceStatus::stale: return "stale";
        case ExperienceStatus::retired: return "retired";
    }
    throw std::invalid_argument("Invalid ExperienceStatus");
}

std::string to_string(ScopeKind value) {
    switch (value) {
        case ScopeKind::agent: return "agent";
        case ScopeKind::context: return "context";
        case ScopeKind::resource: return "resource";
        case ScopeKind::entity: return "entity";
        case ScopeKind::workflow: return "workflow";
        case ScopeKind::domain: return "domain";
        case ScopeKind::file: return "file";
        case ScopeKind::symbol: return "symbol";
        case ScopeKind::module: return "module";
        case ScopeKind::tag: return "tag";
    }
    throw std::invalid_argument("Invalid ScopeKind");
}

std::string to_string(GraphNodeKind value) {
    switch (value) {
        case GraphNodeKind::file: return "file";
        case GraphNodeKind::namespace_symbol: return "namespace";
        case GraphNodeKind::class_symbol: return "class";
        case GraphNodeKind::struct_symbol: return "struct";
        case GraphNodeKind::enum_symbol: return "enum";
        case GraphNodeKind::function_symbol: return "function";
        case GraphNodeKind::method_symbol: return "method";
    }
    throw std::invalid_argument("Invalid GraphNodeKind");
}

std::string to_string(GraphEdgeKind value) {
    switch (value) {
        case GraphEdgeKind::contains: return "contains";
        case GraphEdgeKind::includes: return "includes";
        case GraphEdgeKind::calls: return "calls";
        case GraphEdgeKind::references: return "references";
    }
    throw std::invalid_argument("Invalid GraphEdgeKind");
}

ExperienceKind experience_kind_from_string(const std::string& value) {
    return enum_from_string<ExperienceKind>(value, {
        {"task_outcome", ExperienceKind::task_outcome},
        {"operational_lesson", ExperienceKind::operational_lesson},
        {"user_preference", ExperienceKind::user_preference},
        {"domain_fact", ExperienceKind::domain_fact},
        {"bug_fix", ExperienceKind::bug_fix},
        {"architecture_decision", ExperienceKind::architecture_decision},
        {"procedure", ExperienceKind::procedure},
        {"failed_approach", ExperienceKind::failed_approach},
        {"project_fact", ExperienceKind::project_fact},
    });
}

ExperienceStatus experience_status_from_string(const std::string& value) {
    return enum_from_string<ExperienceStatus>(value, {
        {"candidate", ExperienceStatus::candidate},
        {"verified", ExperienceStatus::verified},
        {"stale", ExperienceStatus::stale},
        {"retired", ExperienceStatus::retired},
    });
}

ScopeKind scope_kind_from_string(const std::string& value) {
    return enum_from_string<ScopeKind>(value, {
        {"agent", ScopeKind::agent}, {"context", ScopeKind::context},
        {"resource", ScopeKind::resource}, {"entity", ScopeKind::entity},
        {"workflow", ScopeKind::workflow}, {"domain", ScopeKind::domain},
        {"file", ScopeKind::file}, {"symbol", ScopeKind::symbol},
        {"module", ScopeKind::module}, {"tag", ScopeKind::tag},
    });
}

GraphNodeKind graph_node_kind_from_string(const std::string& value) {
    return enum_from_string<GraphNodeKind>(value, {
        {"file", GraphNodeKind::file}, {"namespace", GraphNodeKind::namespace_symbol},
        {"class", GraphNodeKind::class_symbol}, {"struct", GraphNodeKind::struct_symbol},
        {"enum", GraphNodeKind::enum_symbol}, {"function", GraphNodeKind::function_symbol},
        {"method", GraphNodeKind::method_symbol},
    });
}

GraphEdgeKind graph_edge_kind_from_string(const std::string& value) {
    return enum_from_string<GraphEdgeKind>(value, {
        {"contains", GraphEdgeKind::contains}, {"includes", GraphEdgeKind::includes},
        {"calls", GraphEdgeKind::calls}, {"references", GraphEdgeKind::references},
    });
}

void to_json(nlohmann::json& json, const Scope& value) {
    json = {{"kind", to_string(value.kind)}, {"value", value.value}};
}

void from_json(const nlohmann::json& json, Scope& value) {
    value.kind = scope_kind_from_string(json.at("kind").get<std::string>());
    json.at("value").get_to(value.value);
}

void to_json(nlohmann::json& json, const Evidence& value) {
    json = {{"type", value.type}, {"uri", value.uri}};
    if (value.content_hash) json["contentHash"] = *value.content_hash;
}

void from_json(const nlohmann::json& json, Evidence& value) {
    json.at("type").get_to(value.type);
    json.at("uri").get_to(value.uri);
    read_optional(json, "contentHash", value.content_hash);
}

void to_json(nlohmann::json& json, const VerificationStep& value) {
    json = {{"command", value.command}};
    if (value.cwd) json["cwd"] = *value.cwd;
}

void from_json(const nlohmann::json& json, VerificationStep& value) {
    json.at("command").get_to(value.command);
    read_optional(json, "cwd", value.cwd);
}

void to_json(nlohmann::json& json, const ExperienceDraft& value) {
    json = {
        {"workspaceId", value.workspace_id}, {"kind", to_string(value.kind)},
        {"title", value.title}, {"trigger", value.trigger}, {"scopes", value.scopes},
        {"evidence", value.evidence}, {"verificationSteps", value.verification_steps},
        {"confidence", value.confidence},
    };
    if (value.symptom) json["symptom"] = *value.symptom;
    if (value.root_cause) json["rootCause"] = *value.root_cause;
    if (value.invariant) json["invariant"] = *value.invariant;
    if (value.fix_summary) json["fixSummary"] = *value.fix_summary;
}

void from_json(const nlohmann::json& json, ExperienceDraft& value) {
    json.at("workspaceId").get_to(value.workspace_id);
    value.kind = experience_kind_from_string(json.at("kind").get<std::string>());
    json.at("title").get_to(value.title);
    json.at("trigger").get_to(value.trigger);
    value.scopes = json.value("scopes", std::vector<Scope>{});
    value.evidence = json.value("evidence", std::vector<Evidence>{});
    value.verification_steps = json.value("verificationSteps", std::vector<VerificationStep>{});
    value.confidence = json.value("confidence", 0.5);
    read_optional(json, "symptom", value.symptom);
    read_optional(json, "rootCause", value.root_cause);
    read_optional(json, "invariant", value.invariant);
    read_optional(json, "fixSummary", value.fix_summary);
}

void to_json(nlohmann::json& json, const Experience& value) {
    to_json(json, static_cast<const ExperienceDraft&>(value));
    json["id"] = value.id;
    json["status"] = to_string(value.status);
    json["createdAt"] = value.created_at;
    json["updatedAt"] = value.updated_at;
    if (value.last_verified_at) json["lastVerifiedAt"] = *value.last_verified_at;
}

void from_json(const nlohmann::json& json, Experience& value) {
    from_json(json, static_cast<ExperienceDraft&>(value));
    json.at("id").get_to(value.id);
    value.status = experience_status_from_string(json.at("status").get<std::string>());
    json.at("createdAt").get_to(value.created_at);
    json.at("updatedAt").get_to(value.updated_at);
    read_optional(json, "lastVerifiedAt", value.last_verified_at);
}

void to_json(nlohmann::json& json, const GraphNode& value) {
    json = {
        {"id", value.id}, {"workspaceId", value.workspace_id}, {"kind", to_string(value.kind)},
        {"name", value.name}, {"qualifiedName", value.qualified_name}, {"file", value.file_path},
        {"startLine", value.start_line}, {"endLine", value.end_line}, {"contentHash", value.content_hash},
    };
}

void to_json(nlohmann::json& json, const GraphEdge& value) {
    json = {
        {"id", value.id}, {"workspaceId", value.workspace_id}, {"kind", to_string(value.kind)},
        {"source", value.source_node_id}, {"confidence", value.confidence}, {"evidence", value.evidence},
    };
    if (value.target_node_id) json["target"] = *value.target_node_id;
    if (value.target_name) json["targetName"] = *value.target_name;
}

void to_json(nlohmann::json& json, const IndexReport& value) {
    json = {
        {"workspaceId", value.workspace_id}, {"discoveredFiles", value.discovered_files},
        {"indexedFiles", value.indexed_files}, {"unchangedFiles", value.unchanged_files},
        {"removedFiles", value.removed_files}, {"nodeCount", value.node_count},
        {"edgeCount", value.edge_count}, {"errors", value.errors},
    };
}

void to_json(nlohmann::json& json, const GraphStatus& value) {
    json = {
        {"workspaceId", value.workspace_id}, {"fileCount", value.file_count},
        {"nodeCount", value.node_count}, {"edgeCount", value.edge_count},
        {"unresolvedEdgeCount", value.unresolved_edge_count},
    };
}

void to_json(nlohmann::json& json, const RecallRequest& value) {
    json = {
        {"workspaceId", value.workspace_id}, {"task", value.task}, {"errors", value.errors},
        {"agents", value.agents},
        {"contexts", value.contexts}, {"resources", value.resources},
        {"entities", value.entities}, {"workflows", value.workflows},
        {"scopes", value.scopes},
        {"files", value.files}, {"symbols", value.symbols}, {"limit", value.limit},
        {"includeCandidates", value.include_candidates},
    };
}

void from_json(const nlohmann::json& json, RecallRequest& value) {
    json.at("workspaceId").get_to(value.workspace_id);
    json.at("task").get_to(value.task);
    value.errors = json.value("errors", std::vector<std::string>{});
    value.agents = json.value("agents", std::vector<std::string>{});
    value.contexts = json.value("contexts", std::vector<std::string>{});
    value.resources = json.value("resources", std::vector<std::string>{});
    value.entities = json.value("entities", std::vector<std::string>{});
    value.workflows = json.value("workflows", std::vector<std::string>{});
    value.scopes = json.value("scopes", std::vector<Scope>{});
    value.files = json.value("files", std::vector<std::string>{});
    value.symbols = json.value("symbols", std::vector<std::string>{});
    value.limit = json.value("limit", std::size_t{10});
    value.include_candidates = json.value("includeCandidates", false);
}

void to_json(nlohmann::json& json, const RecallHit& value) {
    json = {{"experience", value.experience}, {"score", value.score}, {"reasons", value.reasons}};
}

void to_json(nlohmann::json& json, const GuardRequest& value) {
    json = {
        {"workspaceId", value.workspace_id}, {"task", value.task}, {"diffSummary", value.diff_summary},
        {"changedAgents", value.changed_agents},
        {"changedContexts", value.changed_contexts}, {"changedResources", value.changed_resources},
        {"changedEntities", value.changed_entities}, {"changedWorkflows", value.changed_workflows},
        {"changedScopes", value.changed_scopes},
        {"changedFiles", value.changed_files}, {"changedSymbols", value.changed_symbols},
        {"graphDepth", value.graph_depth},
    };
}

void from_json(const nlohmann::json& json, GuardRequest& value) {
    json.at("workspaceId").get_to(value.workspace_id);
    value.task = json.value("task", std::string{});
    value.diff_summary = json.value("diffSummary", std::string{});
    value.changed_agents = json.value("changedAgents", std::vector<std::string>{});
    value.changed_contexts = json.value("changedContexts", std::vector<std::string>{});
    value.changed_resources = json.value("changedResources", std::vector<std::string>{});
    value.changed_entities = json.value("changedEntities", std::vector<std::string>{});
    value.changed_workflows = json.value("changedWorkflows", std::vector<std::string>{});
    value.changed_scopes = json.value("changedScopes", std::vector<Scope>{});
    value.changed_files = json.value("changedFiles", std::vector<std::string>{});
    value.changed_symbols = json.value("changedSymbols", std::vector<std::string>{});
    value.graph_depth = json.value("graphDepth", std::size_t{2});
}

void to_json(nlohmann::json& json, const GuardResult& value) {
    json = {
        {"risk", value.risk}, {"recalled", value.recalled}, {"invariants", value.invariants},
        {"requiredVerifications", value.required_verifications}, {"warnings", value.warnings},
    };
}

}  // namespace recall_memory
