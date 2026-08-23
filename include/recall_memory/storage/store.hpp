#pragma once

#include "recall_memory/domain/model.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace recall_memory {

class Store {
public:
    explicit Store(const std::filesystem::path& database_path);
    ~Store();

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;

    std::string ensure_workspace(const std::filesystem::path& root_path);
    std::string ensure_memory_space(const std::string& name);
    std::optional<std::filesystem::path> workspace_root(const std::string& workspace_id) const;

    std::optional<std::string> indexed_file_hash(
        const std::string& workspace_id,
        const std::string& relative_path) const;
    void replace_file_graph(const ParsedFile& file);
    std::vector<std::string> indexed_files(const std::string& workspace_id) const;
    void remove_file_graph(const std::string& workspace_id, const std::string& relative_path);
    void resolve_graph_edges(const std::string& workspace_id);
    GraphStatus graph_status(const std::string& workspace_id) const;

    std::unordered_map<std::string, std::size_t> related_scopes(
        const std::string& workspace_id,
        const std::vector<std::string>& files,
        const std::vector<std::string>& symbols,
        std::size_t depth) const;

    Experience propose_experience(const ExperienceDraft& draft);
    Experience verify_experience(const std::string& id, const std::vector<Evidence>& evidence);
    Experience update_experience_status(const std::string& id, ExperienceStatus status);
    std::optional<Experience> experience(const std::string& id) const;
    std::vector<Experience> active_experiences(
        const std::string& workspace_id,
        bool include_candidates) const;
    void record_feedback(const std::string& experience_id, const std::string& value, const std::string& note);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace recall_memory
