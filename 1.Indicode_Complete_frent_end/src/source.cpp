#include "indicode/source.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace indicode {
namespace {

std::size_t valid_utf8_scalar_length(std::string_view text, std::size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    std::size_t length = 0;
    if (first < 0x80) return 1;
    if (first >= 0xc2 && first <= 0xdf) length = 2;
    else if (first >= 0xe0 && first <= 0xef) length = 3;
    else if (first >= 0xf0 && first <= 0xf4) length = 4;
    else return 0;
    if (offset + length > text.size()) return 0;

    const auto second = static_cast<unsigned char>(text[offset + 1]);
    if ((second & 0xc0) != 0x80 ||
        (first == 0xe0 && second < 0xa0) || (first == 0xed && second > 0x9f) ||
        (first == 0xf0 && second < 0x90) || (first == 0xf4 && second > 0x8f)) return 0;
    for (std::size_t index = 2; index < length; ++index) {
        if ((static_cast<unsigned char>(text[offset + index]) & 0xc0) != 0x80) return 0;
    }
    return length;
}

}

SourceId SourceManager::add_source(std::string name, std::string contents) {
    if (sources_.size() >= std::numeric_limits<SourceId>::max()) {
        throw std::length_error("source manager exhausted its source ID space");
    }

    std::vector<std::size_t> starts{0};
    for (std::size_t offset = 0; offset < contents.size(); ++offset) {
        if (contents[offset] == '\r') {
            if (offset + 1 < contents.size() && contents[offset + 1] == '\n') ++offset;
            starts.push_back(offset + 1);
        } else if (contents[offset] == '\n') {
            starts.push_back(offset + 1);
        }
    }

    const auto id = static_cast<SourceId>(sources_.size());
    sources_.push_back({std::move(name), std::move(contents)});
    line_starts_.push_back(std::move(starts));
    return id;
}

const SourceFile& SourceManager::get_source(SourceId id) const {
    if (id >= sources_.size()) throw std::out_of_range("invalid source ID");
    return sources_[id];
}

SourceLocation SourceManager::location(SourceId id, std::size_t byte_offset) const {
    const auto& source = get_source(id);
    if (byte_offset > source.contents.size()) {
        throw std::out_of_range("source byte offset is past end of file");
    }

    const auto& starts = line_starts_[id];
    const auto line_start = std::upper_bound(starts.begin(), starts.end(), byte_offset) - starts.begin() - 1;
    std::size_t column = 1;
    for (std::size_t offset = starts[line_start]; offset < byte_offset;) {
        if (source.contents[offset] == '\t') {
            column += 4 - ((column - 1) % 4);
            ++offset;
            continue;
        }
        const auto length = valid_utf8_scalar_length(source.contents, offset);
        offset += length == 0 ? 1 : length;
        ++column;
    }
    return {byte_offset, static_cast<std::size_t>(line_start) + 1, column};
}

SourceLine SourceManager::line(SourceId id, std::size_t one_based_line) const {
    const auto& source = get_source(id);
    const auto& starts = line_starts_[id];
    if (one_based_line == 0 || one_based_line > starts.size()) {
        throw std::out_of_range("invalid source line");
    }
    const auto index = one_based_line - 1;
    const auto start = starts[index];
    auto end = index + 1 < starts.size() ? starts[index + 1] : source.contents.size();
    while (end > start && (source.contents[end - 1] == '\n' || source.contents[end - 1] == '\r')) {
        --end;
    }
    return {std::string_view(source.contents).substr(start, end - start), start};
}

}
