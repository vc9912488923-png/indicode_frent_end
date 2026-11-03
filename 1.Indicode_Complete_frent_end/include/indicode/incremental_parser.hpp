#pragma once

#include "indicode/ast.hpp"
#include "indicode/diagnostic.hpp"
#include "indicode/source.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace indicode {

struct SourceEdit {
    std::size_t start_byte = 0;
    std::size_t erased_bytes = 0;
    std::string inserted_text;
};

struct IncrementalParseItem {
    std::string source_text;
    std::size_t start_byte = 0;
    Program program;
    std::vector<Diagnostic> diagnostics;
    bool reused = false;
};

struct IncrementalParseResult {
    std::uint64_t revision = 0;
    std::size_t parsed_item_count = 0;
    std::size_t reused_item_count = 0;
    std::vector<IncrementalParseItem> items;
    std::vector<Diagnostic> diagnostics;
};

class IncrementalParser {
public:
    explicit IncrementalParser(SourceFile source);

    const IncrementalParseResult& parse();
    const IncrementalParseResult& apply_edit(const SourceEdit& edit);
    const std::string& source_text() const { return source_.contents; }
    std::uint64_t revision() const { return revision_; }

private:
    SourceFile source_;
    std::uint64_t revision_ = 0;
    bool parsed_ = false;
    IncrementalParseResult result_;

    const IncrementalParseResult& rebuild();
};

}