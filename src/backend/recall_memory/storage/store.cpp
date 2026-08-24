#include "recall_memory/storage/store.hpp"

#include "recall_memory/domain/json.hpp"
#include "recall_memory/storage/sqlite.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace recall_memory {
namespace {

std::string now_utc() {
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::uint64_t fnv1a(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

std::string random_id(std::string_view prefix) {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    return std::string(prefix) + hex64(generator()) + hex64(generator());
}

std::string normalized_path(const std::filesystem::path& path) {
    return std::filesystem::weakly_canonical(path).generic_string();
}

std::optional<std::string> optional_text(const sqlite::Statement& statement, int column) {
    if (statement.is_null(column)) return std::nullopt;
    return statement.text(column);
}

Experience read_experience(sqlite::Statement& statement) {
    Experience result;
    result.id = statement.text(0);
    result.workspace_id = statement.text(1);
    result.kind = experience_kind_from_string(statement.text(2));
    result.status = experience_status_from_string(statement.text(3));
    result.title = statement.text(4);
    result.trigger = statement.text(5);
    result.symptom = optional_text(statement, 6);
    result.root_cause = optional_text(statement, 7);
    result.invariant = optional_text(statement, 8);
    result.fix_summary = optional_text(statement, 9);
    result.confidence = statement.real(10);
    result.scopes = nlohmann::json::parse(statement.text(11)).get<std::vector<Scope>>();
    result.evidence = nlohmann::json::parse(statement.text(12)).get<std::vector<Evidence>>();
    result.verification_steps = nlohmann::json::parse(statement.text(13)).get<std::vector<VerificationStep>>();
    result.created_at = statement.text(14);
    result.updated_at = statement.text(15);
    result.last_verified_at = optional_text(statement, 16);
    return result;
}

constexpr std::string_view experience_columns = R"sql(
    id, workspace_id, kind, status, title, trigger, symptom, root_cause,
    invariant_text, fix_summary, confidence, scopes_json, evidence_json,
    verification_json, created_at, updated_at, last_verified_at
)sql";

bool path_matches(std::string_view stored, std::string_view requested) {
    if (stored == requested) return true;
    if (stored.size() < requested.size()) return false;
    const auto offset = stored.size() - requested.size();
    return stored.substr(offset) == requested && (offset == 0 || stored[offset - 1] == '/');
}

}  // namespace

struct Store::Impl {
    explicit Impl(const std::filesystem::path& path) : database(path) {
        database.execute("PRAGMA journal_mode=WAL");
        database.execute("PRAGMA synchronous=NORMAL");
        database.execute("PRAGMA foreign_keys=ON");
        database.execute(R"sql(
            CREATE TABLE IF NOT EXISTS workspaces (
                id TEXT PRIMARY KEY,
                root_path TEXT NOT NULL UNIQUE,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS graph_files (
                workspace_id TEXT NOT NULL,
                path TEXT NOT NULL,
                content_hash TEXT NOT NULL,
                indexed_at TEXT NOT NULL,
                PRIMARY KEY (workspace_id, path),
                FOREIGN KEY (workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS graph_nodes (
                id TEXT PRIMARY KEY,
                workspace_id TEXT NOT NULL,
                kind TEXT NOT NULL,
                name TEXT NOT NULL,
                qualified_name TEXT NOT NULL,
                file_path TEXT NOT NULL,
                start_line INTEGER NOT NULL,
                end_line INTEGER NOT NULL,
                content_hash TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS graph_nodes_workspace_file
                ON graph_nodes(workspace_id, file_path);
            CREATE INDEX IF NOT EXISTS graph_nodes_workspace_name
                ON graph_nodes(workspace_id, name);
            CREATE INDEX IF NOT EXISTS graph_nodes_workspace_qualified
                ON graph_nodes(workspace_id, qualified_name);

            CREATE TABLE IF NOT EXISTS graph_edges (
                id TEXT PRIMARY KEY,
                workspace_id TEXT NOT NULL,
                kind TEXT NOT NULL,
                source_node_id TEXT NOT NULL,
                target_node_id TEXT,
                target_name TEXT,
                confidence REAL NOT NULL,
                evidence TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS graph_edges_source ON graph_edges(workspace_id, source_node_id);
            CREATE INDEX IF NOT EXISTS graph_edges_target ON graph_edges(workspace_id, target_node_id);
            CREATE INDEX IF NOT EXISTS graph_edges_target_name ON graph_edges(workspace_id, target_name);

            CREATE TABLE IF NOT EXISTS experiences (
                id TEXT PRIMARY KEY,
                workspace_id TEXT NOT NULL,
                kind TEXT NOT NULL,
                status TEXT NOT NULL,
                title TEXT NOT NULL,
                trigger TEXT NOT NULL,
                symptom TEXT,
                root_cause TEXT,
                invariant_text TEXT,
                fix_summary TEXT,
                confidence REAL NOT NULL,
                scopes_json TEXT NOT NULL,
                evidence_json TEXT NOT NULL,
                verification_json TEXT NOT NULL,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                last_verified_at TEXT,
                FOREIGN KEY (workspace_id) REFERENCES workspaces(id) ON DELETE CASCADE
            );
            CREATE INDEX IF NOT EXISTS experiences_workspace_status
                ON experiences(workspace_id, status, updated_at DESC);

            CREATE TABLE IF NOT EXISTS experience_feedback (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                experience_id TEXT NOT NULL,
                value TEXT NOT NULL,
                note TEXT NOT NULL,
                created_at TEXT NOT NULL,
                FOREIGN KEY (experience_id) REFERENCES experiences(id) ON DELETE CASCADE
            );
        )sql");
    }

    sqlite::Database database;
};

Store::Store(const std::filesystem::path& database_path) : impl_(std::make_unique<Impl>(database_path)) {}
Store::~Store() = default;
Store::Store(Store&&) noexcept = default;
Store& Store::operator=(Store&&) noexcept = default;

std::string Store::ensure_workspace(const std::filesystem::path& root_path) {
    const auto root = normalized_path(root_path);
    if (!std::filesystem::is_directory(root_path)) throw std::invalid_argument("Workspace is not a directory: " + root);
    const auto id = "ws_" + hex64(fnv1a(root));
    const auto timestamp = now_utc();
    auto statement = impl_->database.prepare(R"sql(
        INSERT INTO workspaces(id, root_path, created_at, updated_at) VALUES(?, ?, ?, ?)
        ON CONFLICT(root_path) DO UPDATE SET updated_at=excluded.updated_at
    )sql");
    statement.bind(1, id);
    statement.bind(2, root);
    statement.bind(3, timestamp);
    statement.bind(4, timestamp);
    statement.execute();

    auto query = impl_->database.prepare("SELECT id FROM workspaces WHERE root_path=?");
    query.bind(1, root);
    if (!query.step()) throw std::runtime_error("Failed to load workspace after upsert");
    return query.text(0);
}

std::string Store::ensure_memory_space(const std::string& name) {
    const auto first = name.find_first_not_of(" \t\r\n");
    const auto last = name.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) throw std::invalid_argument("Memory space name is required");
    const auto trimmed = name.substr(first, last - first + 1);
    if (trimmed.size() > 200) throw std::invalid_argument("Memory space name must not exceed 200 bytes");

    const auto locator = "memory://" + trimmed;
    const auto id = "ms_" + hex64(fnv1a(locator));
    const auto timestamp = now_utc();
    auto statement = impl_->database.prepare(R"sql(
        INSERT INTO workspaces(id, root_path, created_at, updated_at) VALUES(?, ?, ?, ?)
        ON CONFLICT(root_path) DO UPDATE SET updated_at=excluded.updated_at
    )sql");
    statement.bind(1, id);
    statement.bind(2, locator);
    statement.bind(3, timestamp);
    statement.bind(4, timestamp);
    statement.execute();

    auto query = impl_->database.prepare("SELECT id FROM workspaces WHERE root_path=?");
    query.bind(1, locator);
    if (!query.step()) throw std::runtime_error("Failed to load memory space after upsert");
    return query.text(0);
}

std::optional<std::filesystem::path> Store::workspace_root(const std::string& workspace_id) const {
    auto statement = impl_->database.prepare("SELECT root_path FROM workspaces WHERE id=?");
    statement.bind(1, workspace_id);
    if (!statement.step()) return std::nullopt;
    const auto locator = statement.text(0);
    if (locator.starts_with("memory://")) return std::nullopt;
    return std::filesystem::path(locator);
}

std::optional<std::string> Store::indexed_file_hash(
    const std::string& workspace_id, const std::string& relative_path) const {
    auto statement = impl_->database.prepare(
        "SELECT content_hash FROM graph_files WHERE workspace_id=? AND path=?");
    statement.bind(1, workspace_id);
    statement.bind(2, relative_path);
    if (!statement.step()) return std::nullopt;
    return statement.text(0);
}

void Store::replace_file_graph(const ParsedFile& file) {
    if (file.nodes.empty()) throw std::invalid_argument("Parsed file must contain its file node");
    const auto& workspace_id = file.nodes.front().workspace_id;
    sqlite::Transaction transaction(impl_->database);

    auto delete_edges = impl_->database.prepare(R"sql(
        DELETE FROM graph_edges WHERE workspace_id=? AND source_node_id IN (
            SELECT id FROM graph_nodes WHERE workspace_id=? AND file_path=?
        )
    )sql");
    delete_edges.bind(1, workspace_id);
    delete_edges.bind(2, workspace_id);
    delete_edges.bind(3, file.relative_path);
    delete_edges.execute();

    auto delete_nodes = impl_->database.prepare("DELETE FROM graph_nodes WHERE workspace_id=? AND file_path=?");
    delete_nodes.bind(1, workspace_id);
    delete_nodes.bind(2, file.relative_path);
    delete_nodes.execute();

    auto upsert_file = impl_->database.prepare(R"sql(
        INSERT INTO graph_files(workspace_id, path, content_hash, indexed_at) VALUES(?, ?, ?, ?)
        ON CONFLICT(workspace_id, path) DO UPDATE SET
            content_hash=excluded.content_hash, indexed_at=excluded.indexed_at
    )sql");
    upsert_file.bind(1, workspace_id);
    upsert_file.bind(2, file.relative_path);
    upsert_file.bind(3, file.content_hash);
    upsert_file.bind(4, now_utc());
    upsert_file.execute();

    auto insert_node = impl_->database.prepare(R"sql(
        INSERT INTO graph_nodes(
            id, workspace_id, kind, name, qualified_name, file_path,
            start_line, end_line, content_hash
        ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
    )sql");
    for (const auto& node : file.nodes) {
        insert_node.bind(1, node.id);
        insert_node.bind(2, node.workspace_id);
        insert_node.bind(3, to_string(node.kind));
        insert_node.bind(4, node.name);
        insert_node.bind(5, node.qualified_name);
        insert_node.bind(6, node.file_path);
        insert_node.bind(7, static_cast<std::int64_t>(node.start_line));
        insert_node.bind(8, static_cast<std::int64_t>(node.end_line));
        insert_node.bind(9, node.content_hash);
        insert_node.execute();
        insert_node.reset();
    }

    auto insert_edge = impl_->database.prepare(R"sql(
        INSERT INTO graph_edges(
            id, workspace_id, kind, source_node_id, target_node_id,
            target_name, confidence, evidence
        ) VALUES(?, ?, ?, ?, ?, ?, ?, ?)
    )sql");
    for (const auto& edge : file.edges) {
        insert_edge.bind(1, edge.id);
        insert_edge.bind(2, edge.workspace_id);
        insert_edge.bind(3, to_string(edge.kind));
        insert_edge.bind(4, edge.source_node_id);
        if (edge.target_node_id) insert_edge.bind(5, *edge.target_node_id); else insert_edge.bind_null(5);
        if (edge.target_name) insert_edge.bind(6, *edge.target_name); else insert_edge.bind_null(6);
        insert_edge.bind(7, edge.confidence);
        insert_edge.bind(8, edge.evidence);
        insert_edge.execute();
        insert_edge.reset();
    }
    transaction.commit();
}

std::vector<std::string> Store::indexed_files(const std::string& workspace_id) const {
    std::vector<std::string> result;
    auto statement = impl_->database.prepare("SELECT path FROM graph_files WHERE workspace_id=? ORDER BY path");
    statement.bind(1, workspace_id);
    while (statement.step()) result.push_back(statement.text(0));
    return result;
}

void Store::remove_file_graph(const std::string& workspace_id, const std::string& relative_path) {
    sqlite::Transaction transaction(impl_->database);
    auto edges = impl_->database.prepare(R"sql(
        DELETE FROM graph_edges WHERE workspace_id=? AND (
            source_node_id IN (SELECT id FROM graph_nodes WHERE workspace_id=? AND file_path=?) OR
            target_node_id IN (SELECT id FROM graph_nodes WHERE workspace_id=? AND file_path=?))
    )sql");
    edges.bind(1, workspace_id);
    edges.bind(2, workspace_id);
    edges.bind(3, relative_path);
    edges.bind(4, workspace_id);
    edges.bind(5, relative_path);
    edges.execute();
    auto nodes = impl_->database.prepare("DELETE FROM graph_nodes WHERE workspace_id=? AND file_path=?");
    nodes.bind(1, workspace_id);
    nodes.bind(2, relative_path);
    nodes.execute();
    auto file = impl_->database.prepare("DELETE FROM graph_files WHERE workspace_id=? AND path=?");
    file.bind(1, workspace_id);
    file.bind(2, relative_path);
    file.execute();
    transaction.commit();
}

void Store::resolve_graph_edges(const std::string& workspace_id) {
    sqlite::Transaction transaction(impl_->database);
    auto clear_stale = impl_->database.prepare(R"sql(
        UPDATE graph_edges SET target_node_id=NULL
        WHERE workspace_id=? AND target_node_id IS NOT NULL
          AND target_node_id NOT IN (SELECT id FROM graph_nodes WHERE workspace_id=?)
    )sql");
    clear_stale.bind(1, workspace_id);
    clear_stale.bind(2, workspace_id);
    clear_stale.execute();

    auto edges = impl_->database.prepare(R"sql(
        SELECT id, kind, target_name FROM graph_edges
        WHERE workspace_id=? AND target_node_id IS NULL AND target_name IS NOT NULL
    )sql");
    edges.bind(1, workspace_id);
    std::vector<std::array<std::string, 3>> unresolved;
    while (edges.step()) unresolved.push_back({edges.text(0), edges.text(1), edges.text(2)});

    auto update = impl_->database.prepare("UPDATE graph_edges SET target_node_id=? WHERE id=?");
    for (const auto& [edge_id, kind, target_name] : unresolved) {
        std::optional<std::string> target;
        if (kind == "includes") {
            auto query = impl_->database.prepare(R"sql(
                SELECT id FROM graph_nodes
                WHERE workspace_id=? AND kind='file' AND (file_path=? OR file_path LIKE ?)
                ORDER BY CASE WHEN file_path=? THEN 0 ELSE 1 END, length(file_path) LIMIT 1
            )sql");
            query.bind(1, workspace_id);
            query.bind(2, target_name);
            query.bind(3, "%/" + target_name);
            query.bind(4, target_name);
            if (query.step()) target = query.text(0);
        } else {
            auto query = impl_->database.prepare(R"sql(
                SELECT id FROM graph_nodes
                WHERE workspace_id=? AND kind<>'file'
                  AND (qualified_name=? OR name=? OR qualified_name LIKE ?)
                ORDER BY CASE WHEN qualified_name=? THEN 0 WHEN name=? THEN 1 ELSE 2 END LIMIT 1
            )sql");
            query.bind(1, workspace_id);
            query.bind(2, target_name);
            query.bind(3, target_name);
            query.bind(4, "%::" + target_name);
            query.bind(5, target_name);
            query.bind(6, target_name);
            if (query.step()) target = query.text(0);
        }
        if (target) {
            update.bind(1, *target);
            update.bind(2, edge_id);
            update.execute();
            update.reset();
        }
    }
    transaction.commit();
}

GraphStatus Store::graph_status(const std::string& workspace_id) const {
    GraphStatus result{.workspace_id = workspace_id};
    auto count = [this, &workspace_id](std::string_view sql) {
        auto statement = impl_->database.prepare(sql);
        statement.bind(1, workspace_id);
        if (!statement.step()) return std::size_t{0};
        return static_cast<std::size_t>(statement.integer(0));
    };
    result.file_count = count("SELECT count(*) FROM graph_files WHERE workspace_id=?");
    result.node_count = count("SELECT count(*) FROM graph_nodes WHERE workspace_id=?");
    result.edge_count = count("SELECT count(*) FROM graph_edges WHERE workspace_id=?");
    result.unresolved_edge_count = count(
        "SELECT count(*) FROM graph_edges WHERE workspace_id=? AND target_node_id IS NULL AND target_name IS NOT NULL");
    return result;
}

std::unordered_map<std::string, std::size_t> Store::related_scopes(
    const std::string& workspace_id,
    const std::vector<std::string>& files,
    const std::vector<std::string>& symbols,
    std::size_t depth) const {
    struct NodeInfo { std::string id; std::string name; std::string qualified; std::string file; };
    std::vector<NodeInfo> all_nodes;
    auto nodes = impl_->database.prepare(
        "SELECT id, name, qualified_name, file_path FROM graph_nodes WHERE workspace_id=?");
    nodes.bind(1, workspace_id);
    while (nodes.step()) all_nodes.push_back({nodes.text(0), nodes.text(1), nodes.text(2), nodes.text(3)});

    std::unordered_map<std::string, NodeInfo> by_id;
    std::unordered_map<std::string, std::size_t> distance;
    std::vector<std::string> frontier;
    for (const auto& node : all_nodes) {
        by_id.emplace(node.id, node);
        const bool file_seed = std::ranges::any_of(files, [&](const auto& file) { return path_matches(node.file, file); });
        const bool symbol_seed = std::ranges::any_of(symbols, [&](const auto& symbol) {
            return node.name == symbol || node.qualified == symbol ||
                   (node.qualified.size() > symbol.size() && node.qualified.ends_with("::" + symbol));
        });
        if (file_seed || symbol_seed) {
            distance.emplace(node.id, 0);
            frontier.push_back(node.id);
        }
    }

    for (std::size_t level = 0; level < depth && !frontier.empty(); ++level) {
        std::vector<std::string> next;
        auto outgoing = impl_->database.prepare(R"sql(
            SELECT target_node_id FROM graph_edges
            WHERE workspace_id=? AND source_node_id=? AND target_node_id IS NOT NULL
            UNION
            SELECT source_node_id FROM graph_edges
            WHERE workspace_id=? AND target_node_id=?
        )sql");
        for (const auto& node_id : frontier) {
            outgoing.bind(1, workspace_id);
            outgoing.bind(2, node_id);
            outgoing.bind(3, workspace_id);
            outgoing.bind(4, node_id);
            while (outgoing.step()) {
                const auto related = outgoing.text(0);
                if (!distance.contains(related)) {
                    distance.emplace(related, level + 1);
                    next.push_back(related);
                }
            }
            outgoing.reset();
        }
        frontier = std::move(next);
    }

    std::unordered_map<std::string, std::size_t> result;
    for (const auto& [id, value] : distance) {
        const auto found = by_id.find(id);
        if (found == by_id.end()) continue;
        const auto& node = found->second;
        auto add = [&result, value](const std::string& key) {
            const auto [position, inserted] = result.emplace(key, value);
            if (!inserted) position->second = std::min(position->second, value);
        };
        add("file:" + node.file);
        add("symbol:" + node.name);
        add("symbol:" + node.qualified);
        auto suffix = node.qualified;
        while (true) {
            const auto separator = suffix.find("::");
            if (separator == std::string::npos) break;
            suffix = suffix.substr(separator + 2);
            add("symbol:" + suffix);
        }
    }
    return result;
}

Experience Store::propose_experience(const ExperienceDraft& draft) {
    const auto id = random_id("exp_");
    const auto timestamp = now_utc();
    auto statement = impl_->database.prepare(R"sql(
        INSERT INTO experiences(
            id, workspace_id, kind, status, title, trigger, symptom, root_cause,
            invariant_text, fix_summary, confidence, scopes_json, evidence_json,
            verification_json, created_at, updated_at, last_verified_at
        ) VALUES(?, ?, ?, 'candidate', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL)
    )sql");
    statement.bind(1, id);
    statement.bind(2, draft.workspace_id);
    statement.bind(3, to_string(draft.kind));
    statement.bind(4, draft.title);
    statement.bind(5, draft.trigger);
    if (draft.symptom) statement.bind(6, *draft.symptom); else statement.bind_null(6);
    if (draft.root_cause) statement.bind(7, *draft.root_cause); else statement.bind_null(7);
    if (draft.invariant) statement.bind(8, *draft.invariant); else statement.bind_null(8);
    if (draft.fix_summary) statement.bind(9, *draft.fix_summary); else statement.bind_null(9);
    statement.bind(10, draft.confidence);
    statement.bind(11, nlohmann::json(draft.scopes).dump());
    statement.bind(12, nlohmann::json(draft.evidence).dump());
    statement.bind(13, nlohmann::json(draft.verification_steps).dump());
    statement.bind(14, timestamp);
    statement.bind(15, timestamp);
    statement.execute();
    return *experience(id);
}

Experience Store::verify_experience(const std::string& id, const std::vector<Evidence>& evidence) {
    auto current = experience(id);
    if (!current) throw std::invalid_argument("Unknown experience: " + id);
    for (const auto& item : evidence) {
        const auto duplicate = std::ranges::any_of(current->evidence, [&](const auto& existing) {
            return existing.type == item.type && existing.uri == item.uri;
        });
        if (!duplicate) current->evidence.push_back(item);
    }
    const auto timestamp = now_utc();
    auto statement = impl_->database.prepare(R"sql(
        UPDATE experiences SET status='verified', evidence_json=?, updated_at=?, last_verified_at=? WHERE id=?
    )sql");
    statement.bind(1, nlohmann::json(current->evidence).dump());
    statement.bind(2, timestamp);
    statement.bind(3, timestamp);
    statement.bind(4, id);
    statement.execute();
    return *experience(id);
}

Experience Store::update_experience_status(const std::string& id, ExperienceStatus status) {
    auto statement = impl_->database.prepare("UPDATE experiences SET status=?, updated_at=? WHERE id=?");
    statement.bind(1, to_string(status));
    statement.bind(2, now_utc());
    statement.bind(3, id);
    statement.execute();
    auto result = experience(id);
    if (!result) throw std::invalid_argument("Unknown experience: " + id);
    return *result;
}

std::optional<Experience> Store::experience(const std::string& id) const {
    auto statement = impl_->database.prepare("SELECT " + std::string(experience_columns) + " FROM experiences WHERE id=?");
    statement.bind(1, id);
    if (!statement.step()) return std::nullopt;
    return read_experience(statement);
}

std::vector<Experience> Store::active_experiences(
    const std::string& workspace_id, bool include_candidates) const {
    auto statement = impl_->database.prepare(
        "SELECT " + std::string(experience_columns) +
        " FROM experiences WHERE workspace_id=? AND (status='verified'" +
        std::string(include_candidates ? " OR status='candidate'" : "") + ") ORDER BY updated_at DESC LIMIT 1000");
    statement.bind(1, workspace_id);
    std::vector<Experience> result;
    while (statement.step()) result.push_back(read_experience(statement));
    return result;
}

void Store::record_feedback(
    const std::string& experience_id, const std::string& value, const std::string& note) {
    auto statement = impl_->database.prepare(R"sql(
        INSERT INTO experience_feedback(experience_id, value, note, created_at) VALUES(?, ?, ?, ?)
    )sql");
    statement.bind(1, experience_id);
    statement.bind(2, value);
    statement.bind(3, note);
    statement.bind(4, now_utc());
    statement.execute();
    if (value == "outdated" || value == "incorrect") update_experience_status(experience_id, ExperienceStatus::stale);
}

}  // namespace recall_memory
