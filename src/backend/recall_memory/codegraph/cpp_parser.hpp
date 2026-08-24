#pragma once

#include "recall_memory/domain/model.hpp"

#include <string>

namespace recall_memory {

class CppParser {
public:
    CppParser();
    ~CppParser();

    CppParser(const CppParser&) = delete;
    CppParser& operator=(const CppParser&) = delete;
    CppParser(CppParser&&) noexcept;
    CppParser& operator=(CppParser&&) noexcept;

    ParsedFile parse(
        const std::string& workspace_id,
        const std::string& relative_path,
        const std::string& source) const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace recall_memory
