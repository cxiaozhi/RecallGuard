#include "recall_memory/codegraph/indexer.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace recall_memory {
namespace {

constexpr std::uintmax_t maximum_source_size = 4U * 1024U * 1024U;

const std::unordered_set<std::string> source_extensions = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp",
};

const std::unordered_set<std::string> ignored_directories = {
    ".git", ".svn", ".hg", ".vs", "build", "out", "node_modules", "vendor", "third_party",
};

std::uint64_t fnv1a(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string content_hash(std::string_view value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << fnv1a(value);
    return output.str();
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not open file");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool is_source_file(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return source_extensions.contains(extension);
}

}  // namespace

CodeGraphIndexer::CodeGraphIndexer(Store& store) : store_(store) {}

IndexReport CodeGraphIndexer::index(const std::filesystem::path& workspace_root) {
    const auto canonical_root = std::filesystem::weakly_canonical(workspace_root);
    const auto workspace_id = store_.ensure_workspace(canonical_root);
    IndexReport report{.workspace_id = workspace_id};
    std::set<std::string> discovered;

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        canonical_root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            report.errors.push_back(error.message());
            error.clear();
            continue;
        }
        const auto& entry = *iterator;
        if (entry.is_directory(error)) {
            if (ignored_directories.contains(entry.path().filename().string())) iterator.disable_recursion_pending();
            continue;
        }
        if (error || !entry.is_regular_file(error) || !is_source_file(entry.path())) continue;
        const auto relative = std::filesystem::relative(entry.path(), canonical_root, error).generic_string();
        if (error) {
            report.errors.push_back(entry.path().generic_string() + ": " + error.message());
            error.clear();
            continue;
        }
        discovered.insert(relative);
        ++report.discovered_files;
        try {
            const auto size = entry.file_size();
            if (size > maximum_source_size) {
                report.errors.push_back(relative + ": skipped because it exceeds 4 MiB");
                continue;
            }
            const auto source = read_file(entry.path());
            const auto hash = content_hash(source);
            if (store_.indexed_file_hash(workspace_id, relative) == hash) {
                ++report.unchanged_files;
                continue;
            }
            auto parsed = parser_.parse(workspace_id, relative, source);
            store_.replace_file_graph(parsed);
            ++report.indexed_files;
        } catch (const std::exception& exception) {
            report.errors.push_back(relative + ": " + exception.what());
        }
    }

    for (const auto& existing : store_.indexed_files(workspace_id)) {
        if (!discovered.contains(existing)) {
            store_.remove_file_graph(workspace_id, existing);
            ++report.removed_files;
        }
    }
    store_.resolve_graph_edges(workspace_id);
    const auto status = store_.graph_status(workspace_id);
    report.node_count = status.node_count;
    report.edge_count = status.edge_count;
    return report;
}

}  // namespace recall_memory
