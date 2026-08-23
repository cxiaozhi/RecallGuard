#include "recall_memory/memory/service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace recall_memory {
namespace {

std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::unordered_set<std::string> tokenize(std::string_view text) {
    std::unordered_set<std::string> result;
    std::string token;
    auto flush = [&]() {
        if (token.size() >= 2) result.insert(lower_ascii(token));
        token.clear();
    };
    for (const auto raw : text) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) != 0 || character == '_' || character >= 0x80) {
            token.push_back(static_cast<char>(character));
        } else {
            flush();
        }
    }
    flush();
    return result;
}

std::string experience_text(const Experience& experience) {
    std::string result = experience.title + " " + experience.trigger;
    const auto append = [&result](const std::optional<std::string>& value) {
        if (value) result += " " + *value;
    };
    append(experience.symptom);
    append(experience.root_cause);
    append(experience.invariant);
    append(experience.fix_summary);
    for (const auto& scope : experience.scopes) result += " " + scope.value;
    return result;
}

double token_overlap(
    const std::unordered_set<std::string>& query,
    const std::unordered_set<std::string>& candidate) {
    if (query.empty() || candidate.empty()) return 0.0;
    std::size_t matches = 0;
    for (const auto& token : query) {
        if (candidate.contains(token)) ++matches;
    }
    return static_cast<double>(matches) /
           std::sqrt(static_cast<double>(query.size() * candidate.size()));
}

std::string scope_key(const Scope& scope) { return to_string(scope.kind) + ":" + scope.value; }

bool path_matches(std::string_view left, std::string_view right) {
    if (left == right) return true;
    if (left.size() > right.size() && left.ends_with(right)) return true;
    return right.size() > left.size() && right.ends_with(left);
}

bool value_matches(std::string_view left, std::string_view right) {
    if (left.empty() || right.empty()) return false;
    const auto normalized_left = lower_ascii(std::string(left));
    const auto normalized_right = lower_ascii(std::string(right));
    return normalized_left == normalized_right ||
           normalized_left.find(normalized_right) != std::string::npos ||
           normalized_right.find(normalized_left) != std::string::npos;
}

bool matches_any(std::string_view scope, const std::vector<std::string>& values) {
    return std::ranges::any_of(values, [scope](const auto& value) { return value_matches(scope, value); });
}

void validate_draft(const ExperienceDraft& draft) {
    if (draft.workspace_id.empty()) throw std::invalid_argument("workspaceId is required");
    if (draft.title.empty()) throw std::invalid_argument("title is required");
    if (draft.trigger.empty()) throw std::invalid_argument("trigger is required");
    if (draft.confidence < 0.0 || draft.confidence > 1.0) {
        throw std::invalid_argument("confidence must be between 0 and 1");
    }
    if (draft.scopes.empty()) throw std::invalid_argument("At least one scope is required");
    for (const auto& scope : draft.scopes) {
        if (scope.value.empty()) throw std::invalid_argument("scope value must not be empty");
    }
}

}  // namespace

MemoryService::MemoryService(Store& store) : store_(store) {}

Experience MemoryService::propose(const ExperienceDraft& draft) {
    validate_draft(draft);
    return store_.propose_experience(draft);
}

Experience MemoryService::verify(const std::string& id, const std::vector<Evidence>& evidence) {
    if (evidence.empty()) throw std::invalid_argument("Verification requires at least one evidence item");
    return store_.verify_experience(id, evidence);
}

Experience MemoryService::mark_stale(const std::string& id) {
    return store_.update_experience_status(id, ExperienceStatus::stale);
}

void MemoryService::feedback(const std::string& id, const std::string& value, const std::string& note) {
    static const std::unordered_set<std::string> allowed = {"useful", "outdated", "incorrect"};
    if (!allowed.contains(value)) throw std::invalid_argument("feedback must be useful, outdated, or incorrect");
    store_.record_feedback(id, value, note);
}

std::vector<RecallHit> MemoryService::recall(const RecallRequest& request) const {
    if (request.workspace_id.empty()) throw std::invalid_argument("workspaceId is required");
    if (request.limit == 0 || request.limit > 100) throw std::invalid_argument("limit must be between 1 and 100");

    std::string query_text = request.task;
    for (const auto& error : request.errors) query_text += " " + error;
    for (const auto& agent : request.agents) query_text += " " + agent;
    for (const auto& context : request.contexts) query_text += " " + context;
    for (const auto& resource : request.resources) query_text += " " + resource;
    for (const auto& entity : request.entities) query_text += " " + entity;
    for (const auto& workflow : request.workflows) query_text += " " + workflow;
    for (const auto& scope : request.scopes) query_text += " " + scope.value;
    for (const auto& file : request.files) query_text += " " + file;
    for (const auto& symbol : request.symbols) query_text += " " + symbol;
    const auto query_tokens = tokenize(query_text);
    const auto graph_scopes = store_.related_scopes(
        request.workspace_id, request.files, request.symbols, 2);

    std::vector<RecallHit> hits;
    for (const auto& experience : store_.active_experiences(request.workspace_id, request.include_candidates)) {
        RecallHit hit{.experience = experience};
        const auto overlap = token_overlap(query_tokens, tokenize(experience_text(experience)));
        if (overlap > 0.0) {
            hit.score += std::min(0.42, overlap * 0.65);
            hit.reasons.push_back("task text overlaps the recorded experience");
        }

        const auto lowered_document = lower_ascii(experience_text(experience));
        for (const auto& error : request.errors) {
            if (!error.empty() && lowered_document.find(lower_ascii(error)) != std::string::npos) {
                hit.score += 0.16;
                hit.reasons.push_back("error signature matches");
                break;
            }
        }

        double best_scope_score = 0.0;
        std::string best_scope_reason;
        for (const auto& scope : experience.scopes) {
            for (const auto& query_scope : request.scopes) {
                if (scope.kind == query_scope.kind && value_matches(scope.value, query_scope.value)) {
                    best_scope_score = std::max(best_scope_score, 0.42);
                    best_scope_reason = "direct explicit scope match";
                }
            }
            if (scope.kind == ScopeKind::file) {
                for (const auto& file : request.files) {
                    if (path_matches(scope.value, file)) {
                        best_scope_score = std::max(best_scope_score, 0.38);
                        best_scope_reason = "direct file scope match";
                    }
                }
            } else if (scope.kind == ScopeKind::symbol) {
                for (const auto& symbol : request.symbols) {
                    if (scope.value == symbol || scope.value.ends_with("::" + symbol)) {
                        best_scope_score = std::max(best_scope_score, 0.42);
                        best_scope_reason = "direct symbol scope match";
                    }
                }
            } else {
                const std::vector<std::string>* values = nullptr;
                switch (scope.kind) {
                    case ScopeKind::agent: values = &request.agents; break;
                    case ScopeKind::context: values = &request.contexts; break;
                    case ScopeKind::resource: values = &request.resources; break;
                    case ScopeKind::entity: values = &request.entities; break;
                    case ScopeKind::workflow: values = &request.workflows; break;
                    case ScopeKind::domain:
                    case ScopeKind::module:
                    case ScopeKind::tag: values = &request.contexts; break;
                    case ScopeKind::file:
                    case ScopeKind::symbol: break;
                }
                if (values != nullptr && matches_any(scope.value, *values)) {
                    best_scope_score = std::max(best_scope_score, 0.38);
                    best_scope_reason = "direct " + to_string(scope.kind) + " scope match";
                }
            }
            if (const auto found = graph_scopes.find(scope_key(scope)); found != graph_scopes.end()) {
                const auto graph_score = 0.32 / static_cast<double>(found->second + 1);
                if (graph_score > best_scope_score) {
                    best_scope_score = graph_score;
                    best_scope_reason = "scope is connected through the code graph at distance " +
                                        std::to_string(found->second);
                }
            }
        }
        if (best_scope_score > 0.0) {
            hit.score += best_scope_score;
            hit.reasons.push_back(std::move(best_scope_reason));
        }

        hit.score += std::clamp(experience.confidence, 0.0, 1.0) * 0.12;
        if (experience.status == ExperienceStatus::verified) {
            hit.score += 0.16;
            hit.reasons.push_back("experience is verified");
        } else {
            hit.score += 0.02;
            hit.reasons.push_back("experience is still a candidate");
        }
        hit.score = std::min(1.0, hit.score);
        if (hit.score >= 0.18) hits.push_back(std::move(hit));
    }

    std::ranges::sort(hits, [](const auto& left, const auto& right) {
        if (left.score != right.score) return left.score > right.score;
        return left.experience.updated_at > right.experience.updated_at;
    });
    if (hits.size() > request.limit) hits.resize(request.limit);
    return hits;
}

GuardResult MemoryService::guard(const GuardRequest& request) const {
    RecallRequest recall_request{
        .workspace_id = request.workspace_id,
        .task = request.task + " " + request.diff_summary,
        .agents = request.changed_agents,
        .contexts = request.changed_contexts,
        .resources = request.changed_resources,
        .entities = request.changed_entities,
        .workflows = request.changed_workflows,
        .scopes = request.changed_scopes,
        .files = request.changed_files,
        .symbols = request.changed_symbols,
        .limit = 25,
        .include_candidates = false,
    };
    GuardResult result;
    result.recalled = recall(recall_request);

    double highest_risk = 0.0;
    std::set<std::string> invariant_set;
    std::set<std::string> verification_set;
    for (const auto& hit : result.recalled) {
        highest_risk = std::max(highest_risk, hit.score);
        if (hit.experience.invariant && invariant_set.insert(*hit.experience.invariant).second) {
            result.invariants.push_back(*hit.experience.invariant);
        }
        for (const auto& step : hit.experience.verification_steps) {
            const auto key = step.command + "\n" + step.cwd.value_or("");
            if (verification_set.insert(key).second) result.required_verifications.push_back(step);
        }
        if (hit.score >= 0.55) {
            result.warnings.push_back(
                "Historical repetition risk: " + hit.experience.title + " (" + hit.experience.id + ")");
        }
    }

    if (highest_risk >= 0.72 || (!result.invariants.empty() && highest_risk >= 0.55)) {
        result.risk = "high";
    } else if (highest_risk >= 0.35) {
        result.risk = "medium";
    }
    if (!result.required_verifications.empty()) {
        result.warnings.push_back("Run every required verification before accepting the result");
    }
    return result;
}

}  // namespace recall_memory
