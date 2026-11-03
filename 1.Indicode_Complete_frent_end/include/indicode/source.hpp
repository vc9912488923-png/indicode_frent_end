#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace indicode {

using SourceId = std::uint32_t;

struct SourceLocation {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

struct SourceSpan {
    SourceLocation start;
    SourceLocation end;
};

struct SourceFile {
    std::string name;
    std::string contents;
};

struct SourceLine {
    std::string_view text;
    std::size_t offset = 0;
};

class SourceManager {
public:
    SourceId add_source(std::string name, std::string contents);
    const SourceFile& get_source(SourceId id) const;
    SourceLocation location(SourceId id, std::size_t byte_offset) const;
    SourceLine line(SourceId id, std::size_t one_based_line) const;

private:
    std::vector<SourceFile> sources_;
    std::vector<std::vector<std::size_t>> line_starts_;
};

}