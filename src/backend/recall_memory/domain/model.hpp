#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace recall_memory {

enum class ExperienceKind {
    task_outcome,
    operational_lesson,
    user_preference,
    domain_fact,
    bug_fix,
    architecture_decision,
    procedure,
    failed_approach,
    project_fact,
};

enum class ExperienceStatus { candidate, verified, stale, retired };

enum class ScopeKind {
    agent,
    context,
    resource,
    entity,
    workflow,
    domain,
    file,
    symbol,
    module,
    tag,
};

enum class GraphNodeKind {
    file,
    namespace_symbol,
    class_symbol,
    struct_symbol,
    enum_symbol,
    function_symbol,
    method_symbol,
};

enum class GraphEdgeKind { contains, includes, calls, references };

struct Scope {
    ScopeKind kind{ScopeKind::tag};
    std::string value;
};

struct Evidence {
    std::string type;
    std::string uri;
    std::optional<std::string> content_hash;
};

struct VerificationStep {
    std::string command;
    std::optional<std::string> cwd;
};

struct ExperienceDraft {
    std::string workspace_id;
    ExperienceKind kind{ExperienceKind::task_outcome};
    std::string title;
    std::string trigger;
    std::optional<std::string> symptom;
    std::optional<std::string> root_cause;
    std::optional<std::string> invariant;
    std::optional<std::string> fix_summary;
    std::vector<Scope> scopes;
    std::vector<Evidence> evidence;
    std::vector<VerificationStep> verification_steps;
    double confidence{0.5};
};

struct Experience : ExperienceDraft {
    std::string id;
    ExperienceStatus status{ExperienceStatus::candidate};
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> last_verified_at;
};

struct GraphNode {
    std::string id;
    std::string workspace_id;
    GraphNodeKind kind{GraphNodeKind::file};
    std::string name;
    std::string qualified_name;
    std::string file_path;
    std::uint32_t start_line{0};
    std::uint32_t end_line{0};
    std::string content_hash;
};

struct GraphEdge {
    std::string id;
    std::string workspace_id;
    GraphEdgeKind kind{GraphEdgeKind::contains};
    std::string source_node_id;
    std::optional<std::string> target_node_id;
    std::optional<std::string> target_name;
    double confidence{1.0};
    std::string evidence;
};

struct ParsedFile {
    std::string relative_path;
    std::string content_hash;
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
};

struct IndexReport {
    std::string workspace_id;
    std::size_t discovered_files{0};
    std::size_t indexed_files{0};
    std::size_t unchanged_files{0};
    std::size_t removed_files{0};
    std::size_t node_count{0};
    std::size_t edge_count{0};
    std::vector<std::string> errors;
};

struct GraphStatus {
    std::string workspace_id;
    std::size_t file_count{0};
    std::size_t node_count{0};
    std::size_t edge_count{0};
    std::size_t unresolved_edge_count{0};
};

struct RecallRequest {
    std::string workspace_id;
    std::string task;
    std::vector<std::string> errors;
    std::vector<std::string> agents;
    std::vector<std::string> contexts;
    std::vector<std::string> resources;
    std::vector<std::string> entities;
    std::vector<std::string> workflows;
    std::vector<Scope> scopes;
    std::vector<std::string> files;
    std::vector<std::string> symbols;
    std::size_t limit{10};
    bool include_candidates{false};
};

struct RecallHit {
    Experience experience;
    double score{0.0};
    std::vector<std::string> reasons;
};

struct GuardRequest {
    std::string workspace_id;
    std::string task;
    std::string diff_summary;
    std::vector<std::string> changed_agents;
    std::vector<std::string> changed_contexts;
    std::vector<std::string> changed_resources;
    std::vector<std::string> changed_entities;
    std::vector<std::string> changed_workflows;
    std::vector<Scope> changed_scopes;
    std::vector<std::string> changed_files;
    std::vector<std::string> changed_symbols;
    std::size_t graph_depth{2};
};

struct GuardResult {
    std::string risk{"low"};
    std::vector<RecallHit> recalled;
    std::vector<std::string> invariants;
    std::vector<VerificationStep> required_verifications;
    std::vector<std::string> warnings;
};

std::string to_string(ExperienceKind value);
std::string to_string(ExperienceStatus value);
std::string to_string(ScopeKind value);
std::string to_string(GraphNodeKind value);
std::string to_string(GraphEdgeKind value);

ExperienceKind experience_kind_from_string(const std::string& value);
ExperienceStatus experience_status_from_string(const std::string& value);
ScopeKind scope_kind_from_string(const std::string& value);
GraphNodeKind graph_node_kind_from_string(const std::string& value);
GraphEdgeKind graph_edge_kind_from_string(const std::string& value);

}  // namespace recall_memory
