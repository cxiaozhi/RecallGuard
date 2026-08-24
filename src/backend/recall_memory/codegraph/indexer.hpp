#pragma once

#include "recall_memory/codegraph/cpp_parser.hpp"
#include "recall_memory/domain/model.hpp"
#include "recall_memory/storage/store.hpp"

#include <filesystem>
#include <memory>

namespace recall_memory {

class CodeGraphIndexer {
public:
    explicit CodeGraphIndexer(Store& store);

    IndexReport index(const std::filesystem::path& workspace_root);

private:
    Store& store_;
    CppParser parser_;
};

}  // namespace recall_memory
