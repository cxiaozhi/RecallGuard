#include "recall_memory/codegraph/cpp_parser.hpp"

#include <tree_sitter/api.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

extern "C" const TSLanguage* tree_sitter_cpp();

namespace recall_memory {
namespace {

std::uint64_t fnv1a(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hash_text(std::string_view value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << fnv1a(value);
    return output.str();
}

std::string node_text(TSNode node, std::string_view source) {
    const auto start = static_cast<std::size_t>(ts_node_start_byte(node));
    const auto end = static_cast<std::size_t>(ts_node_end_byte(node));
    if (start > end || end > source.size()) return {};
    return std::string(source.substr(start, end - start));
}

bool has_type(TSNode node, std::string_view type) { return std::string_view(ts_node_type(node)) == type; }

TSNode child_by_field(TSNode node, const char* field) {
    return ts_node_child_by_field_name(node, field, static_cast<std::uint32_t>(std::char_traits<char>::length(field)));
}

std::string trim(std::string value) {
    const auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::ranges::find_if_not(value, is_space));
    value.erase(std::ranges::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

std::string declaration_name(TSNode node, std::string_view source) {
    if (ts_node_is_null(node)) return {};
    const std::string_view type = ts_node_type(node);
    if (type == "identifier" || type == "field_identifier" || type == "namespace_identifier" ||
        type == "type_identifier" || type == "operator_name" || type == "destructor_name" ||
        type == "qualified_identifier") {
        return trim(node_text(node, source));
    }

    const auto declarator = child_by_field(node, "declarator");
    if (!ts_node_is_null(declarator)) {
        const auto result = declaration_name(declarator, source);
        if (!result.empty()) return result;
    }

    const auto name = child_by_field(node, "name");
    if (!ts_node_is_null(name)) {
        const auto result = declaration_name(name, source);
        if (!result.empty()) return result;
    }

    const auto count = ts_node_named_child_count(node);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto child = ts_node_named_child(node, index);
        const std::string_view child_type = ts_node_type(child);
        if (child_type == "parameter_list" || child_type == "template_argument_list" ||
            child_type == "compound_statement") {
            continue;
        }
        const auto result = declaration_name(child, source);
        if (!result.empty()) return result;
    }
    return {};
}

std::string unqualified_name(std::string value) {
    if (const auto position = value.rfind("::"); position != std::string::npos) value = value.substr(position + 2);
    if (const auto position = value.rfind("->"); position != std::string::npos) value = value.substr(position + 2);
    if (const auto position = value.rfind('.'); position != std::string::npos) value = value.substr(position + 1);
    if (const auto position = value.find('<'); position != std::string::npos) value.resize(position);
    return trim(std::move(value));
}

std::string include_target(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '<' && value.back() == '>'))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string qualify(std::string_view container, std::string_view name) {
    if (container.empty() || name.find("::") != std::string_view::npos) return std::string(name);
    return std::string(container) + "::" + std::string(name);
}

std::string make_node_id(
    std::string_view workspace_id,
    std::string_view file,
    GraphNodeKind kind,
    std::string_view qualified_name,
    std::string_view discriminator = {}) {
    return "node_" + hash_text(
        std::string(workspace_id) + "|" + std::string(file) + "|" + to_string(kind) + "|" +
        std::string(qualified_name) + "|" + std::string(discriminator));
}

std::string make_edge_id(
    const GraphNode& source,
    GraphEdgeKind kind,
    std::string_view target,
    std::uint32_t byte_offset) {
    return "edge_" + hash_text(
        source.id + "|" + to_string(kind) + "|" + std::string(target) + "|" + std::to_string(byte_offset));
}

struct WalkContext {
    std::string container_name;
    std::string container_node_id;
    bool inside_type{false};
};

class Walker {
public:
    Walker(std::string workspace_id, std::string relative_path, std::string_view source, std::string content_hash)
        : workspace_id_(std::move(workspace_id)),
          relative_path_(std::move(relative_path)),
          source_(source),
          content_hash_(std::move(content_hash)) {
        GraphNode file;
        file.id = make_node_id(workspace_id_, relative_path_, GraphNodeKind::file, relative_path_);
        file.workspace_id = workspace_id_;
        file.kind = GraphNodeKind::file;
        file.name = relative_path_;
        file.qualified_name = relative_path_;
        file.file_path = relative_path_;
        file.content_hash = content_hash_;
        file.start_line = 1;
        file.end_line = static_cast<std::uint32_t>(std::count(source_.begin(), source_.end(), '\n') + 1);
        parsed_.relative_path = relative_path_;
        parsed_.content_hash = content_hash_;
        parsed_.nodes.push_back(file);
        root_context_.container_node_id = file.id;
    }

    ParsedFile run(TSNode root) {
        walk(root, root_context_);
        return std::move(parsed_);
    }

private:
    void walk(TSNode node, const WalkContext& context) {
        const std::string_view type = ts_node_type(node);
        if (type == "namespace_definition") {
            walk_container(node, context, GraphNodeKind::namespace_symbol, false);
            return;
        }
        if (type == "class_specifier") {
            walk_container(node, context, GraphNodeKind::class_symbol, true);
            return;
        }
        if (type == "struct_specifier") {
            walk_container(node, context, GraphNodeKind::struct_symbol, true);
            return;
        }
        if (type == "enum_specifier") {
            add_symbol(node, context, GraphNodeKind::enum_symbol, child_by_field(node, "name"));
        } else if (type == "function_definition") {
            walk_function(node, context);
            return;
        } else if (type == "preproc_include") {
            add_include(node);
        } else if (type == "call_expression") {
            add_call(node, context);
        }
        walk_children(node, context);
    }

    void walk_children(TSNode node, const WalkContext& context) {
        const auto count = ts_node_named_child_count(node);
        for (std::uint32_t index = 0; index < count; ++index) {
            walk(ts_node_named_child(node, index), context);
        }
    }

    void walk_container(
        TSNode node,
        const WalkContext& parent,
        GraphNodeKind kind,
        bool inside_type) {
        const auto name_node = child_by_field(node, "name");
        const auto name = declaration_name(name_node, source_);
        if (name.empty()) {
            walk_children(node, parent);
            return;
        }
        const auto node_id = add_symbol(node, parent, kind, name_node);
        WalkContext child{
            .container_name = qualify(parent.container_name, name),
            .container_node_id = node_id,
            .inside_type = inside_type,
        };
        walk_children(node, child);
    }

    void walk_function(TSNode node, const WalkContext& parent) {
        const auto declarator = child_by_field(node, "declarator");
        const auto raw_name = declaration_name(declarator, source_);
        if (raw_name.empty()) {
            walk_children(node, parent);
            return;
        }
        const auto name = unqualified_name(raw_name);
        const auto qualified = raw_name.find("::") != std::string::npos ? raw_name : qualify(parent.container_name, name);
        const auto kind = parent.inside_type || raw_name.find("::") != std::string::npos
                              ? GraphNodeKind::method_symbol
                              : GraphNodeKind::function_symbol;
        const auto signature = trim(node_text(declarator, source_));
        auto symbol = make_symbol(node, kind, name, qualified, signature);
        add_contains(parent.container_node_id, symbol, node);
        parsed_.nodes.push_back(symbol);
        WalkContext child{
            .container_name = qualified,
            .container_node_id = symbol.id,
            .inside_type = false,
        };
        walk_children(node, child);
    }

    std::string add_symbol(TSNode node, const WalkContext& parent, GraphNodeKind kind, TSNode name_node) {
        const auto name = declaration_name(name_node, source_);
        if (name.empty()) return parent.container_node_id;
        const auto qualified = qualify(parent.container_name, name);
        auto symbol = make_symbol(node, kind, name, qualified, {});
        add_contains(parent.container_node_id, symbol, node);
        const auto id = symbol.id;
        parsed_.nodes.push_back(std::move(symbol));
        return id;
    }

    GraphNode make_symbol(
        TSNode node,
        GraphNodeKind kind,
        std::string name,
        std::string qualified,
        std::string_view discriminator) const {
        const auto start = ts_node_start_point(node);
        const auto end = ts_node_end_point(node);
        return GraphNode{
            .id = make_node_id(workspace_id_, relative_path_, kind, qualified, discriminator),
            .workspace_id = workspace_id_,
            .kind = kind,
            .name = std::move(name),
            .qualified_name = std::move(qualified),
            .file_path = relative_path_,
            .start_line = start.row + 1,
            .end_line = end.row + 1,
            .content_hash = content_hash_,
        };
    }

    void add_contains(const std::string& parent_id, const GraphNode& child, TSNode evidence_node) {
        const auto& parent = parsed_.nodes.front();
        parsed_.edges.push_back(GraphEdge{
            .id = make_edge_id(parent, GraphEdgeKind::contains, child.id, ts_node_start_byte(evidence_node)),
            .workspace_id = workspace_id_,
            .kind = GraphEdgeKind::contains,
            .source_node_id = parent_id,
            .target_node_id = child.id,
            .target_name = child.qualified_name,
            .confidence = 1.0,
            .evidence = relative_path_ + ":" + std::to_string(child.start_line),
        });
    }

    void add_include(TSNode node) {
        auto path = child_by_field(node, "path");
        if (ts_node_is_null(path)) path = ts_node_named_child(node, 0);
        const auto target = include_target(node_text(path, source_));
        if (target.empty()) return;
        const auto& file = parsed_.nodes.front();
        parsed_.edges.push_back(GraphEdge{
            .id = make_edge_id(file, GraphEdgeKind::includes, target, ts_node_start_byte(node)),
            .workspace_id = workspace_id_,
            .kind = GraphEdgeKind::includes,
            .source_node_id = file.id,
            .target_name = target,
            .confidence = 0.9,
            .evidence = relative_path_ + ":" + std::to_string(ts_node_start_point(node).row + 1),
        });
    }

    void add_call(TSNode node, const WalkContext& context) {
        if (context.container_node_id.empty()) return;
        const auto function = child_by_field(node, "function");
        const auto raw_target = trim(node_text(function, source_));
        const auto target = raw_target.find("::") != std::string::npos ? raw_target : unqualified_name(raw_target);
        if (target.empty()) return;

        GraphNode source = parsed_.nodes.front();
        source.id = context.container_node_id;
        parsed_.edges.push_back(GraphEdge{
            .id = make_edge_id(source, GraphEdgeKind::calls, target, ts_node_start_byte(node)),
            .workspace_id = workspace_id_,
            .kind = GraphEdgeKind::calls,
            .source_node_id = context.container_node_id,
            .target_name = target,
            .confidence = raw_target == target ? 0.72 : 0.58,
            .evidence = relative_path_ + ":" + std::to_string(ts_node_start_point(node).row + 1),
        });
    }

    std::string workspace_id_;
    std::string relative_path_;
    std::string_view source_;
    std::string content_hash_;
    ParsedFile parsed_;
    WalkContext root_context_;
};

}  // namespace

struct CppParser::Impl {
    Impl() : parser(ts_parser_new()) {
        if (!parser) throw std::runtime_error("Could not create Tree-sitter parser");
        if (!ts_parser_set_language(parser, tree_sitter_cpp())) {
            ts_parser_delete(parser);
            parser = nullptr;
            throw std::runtime_error("Tree-sitter C++ grammar is incompatible with the parser runtime");
        }
    }
    ~Impl() { if (parser) ts_parser_delete(parser); }
    TSParser* parser;
};

CppParser::CppParser() : impl_(new Impl()) {}
CppParser::~CppParser() { delete impl_; }
CppParser::CppParser(CppParser&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}
CppParser& CppParser::operator=(CppParser&& other) noexcept {
    if (this == &other) return *this;
    delete impl_;
    impl_ = std::exchange(other.impl_, nullptr);
    return *this;
}

ParsedFile CppParser::parse(
    const std::string& workspace_id,
    const std::string& relative_path,
    const std::string& source) const {
    if (!impl_) throw std::logic_error("Parser was moved from");
    auto* tree = ts_parser_parse_string(
        impl_->parser, nullptr, source.data(), static_cast<std::uint32_t>(source.size()));
    if (!tree) throw std::runtime_error("Tree-sitter failed to parse " + relative_path);
    std::unique_ptr<TSTree, decltype(&ts_tree_delete)> owned_tree(tree, ts_tree_delete);
    Walker walker(workspace_id, relative_path, source, hash_text(source));
    return walker.run(ts_tree_root_node(tree));
}

}  // namespace recall_memory
