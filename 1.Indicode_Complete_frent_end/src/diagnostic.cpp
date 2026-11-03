#include "indicode/diagnostic.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace indicode {
namespace {

const char* severity_name(DiagnosticSeverity severity) {
    return severity == DiagnosticSeverity::Error ? "error" : "warning";
}

std::string json_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00";
                result += hex[(character >> 4) & 0x0f];
                result += hex[character & 0x0f];
            } else {
                result += static_cast<char>(character);
            }
        }
    }
    return result;
}

std::size_t utf8_scalar_length(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) {
        return 1;
    }
    const auto first = static_cast<unsigned char>(text[offset]);
    std::size_t length = first < 0x80 ? 1 :
                         first >= 0xc2 && first <= 0xdf ? 2 :
                         first >= 0xe0 && first <= 0xef ? 3 :
                         first >= 0xf0 && first <= 0xf4 ? 4 : 1;
    if (offset + length > text.size()) {
        return 1;
    }
    for (std::size_t index = 1; index < length; ++index) {
        if ((static_cast<unsigned char>(text[offset + index]) & 0xc0) != 0x80) {
            return 1;
        }
    }
    return length;
}

std::size_t display_column(std::string_view text, std::size_t byte_offset) {
    std::size_t column = 0;
    for (std::size_t index = 0; index < std::min(byte_offset, text.size());) {
        if (text[index] == '\t') {
            column += 4 - column % 4;
            ++index;
        } else {
            index += utf8_scalar_length(text, index);
            ++column;
        }
    }
    return column;
}

std::string expand_tabs(std::string_view text) {
    std::string result;
    std::size_t column = 0;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '\t') {
            const auto spaces = 4 - column % 4;
            result.append(spaces, ' ');
            column += spaces;
            ++index;
        } else {
            const auto length = utf8_scalar_length(text, index);
            result.append(text.substr(index, length));
            index += length;
            ++column;
        }
    }
    return result;
}

}

std::string format_diagnostic_text(const SourceManager& sources, SourceId source_id,
                                   const Diagnostic& diagnostic) {
    const auto& source = sources.get_source(source_id);
    std::ostringstream output;
    output << source.name << ':' << diagnostic.span.start.line << ':'
           << diagnostic.span.start.column << ": " << severity_name(diagnostic.severity)
           << '[' << diagnostic.code << "]: " << diagnostic.message;

    const auto line = sources.line(source_id, diagnostic.span.start.line);
    output << "\n  " << expand_tabs(line.text) << "\n  ";
    const auto start_offset = diagnostic.span.start.offset >= line.offset
        ? std::min(diagnostic.span.start.offset - line.offset, line.text.size())
        : std::size_t{0};
    const auto column = display_column(line.text, start_offset);
    output << std::string(column, ' ');
    const auto same_line_end = diagnostic.span.end.line == diagnostic.span.start.line
        ? std::min(diagnostic.span.end.offset >= line.offset
                       ? diagnostic.span.end.offset - line.offset
                       : start_offset,
                   line.text.size())
        : start_offset + utf8_scalar_length(line.text, start_offset);
    const auto end_column = display_column(line.text, same_line_end);
    output << std::string(std::max(std::size_t{1}, end_column > column ? end_column - column : 0), '^');
    if (diagnostic.fix_it) {
        output << "\n  help: " << diagnostic.fix_it->message;
        if (!diagnostic.fix_it->replacement.empty()) {
            output << " (insert `" << diagnostic.fix_it->replacement << '`' << ')';
        }
    }
    for (const auto& note : diagnostic.notes) {
        output << "\n  " << source.name << ':' << note.span.start.line << ':'
               << note.span.start.column << ": note: " << note.message;
    }
    return output.str();
}

std::string format_diagnostic_text(const SourceFile& source, const Diagnostic& diagnostic) {
    SourceManager sources;
    const auto source_id = sources.add_source(source.name, source.contents);
    return format_diagnostic_text(sources, source_id, diagnostic);
}

std::string format_diagnostics_json(const SourceFile& source,
                                    const std::vector<Diagnostic>& diagnostics) {
    std::ostringstream output;
    output << "{\"schemaVersion\":1,\"diagnostics\":[";
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        const auto& diagnostic = diagnostics[index];
        output << "{\"severity\":\"" << severity_name(diagnostic.severity)
               << "\",\"code\":\"" << json_escape(diagnostic.code)
               << "\",\"message\":\"" << json_escape(diagnostic.message)
               << "\",\"file\":\"" << json_escape(source.name)
               << "\",\"start\":{\"offset\":" << diagnostic.span.start.offset
               << ",\"line\":" << diagnostic.span.start.line
               << ",\"column\":" << diagnostic.span.start.column
               << "},\"end\":{\"offset\":" << diagnostic.span.end.offset
               << ",\"line\":" << diagnostic.span.end.line
               << ",\"column\":" << diagnostic.span.end.column << "},\"fixIt\":";
        if (diagnostic.fix_it) {
            output << "{\"message\":\"" << json_escape(diagnostic.fix_it->message)
                   << "\",\"replacement\":\"" << json_escape(diagnostic.fix_it->replacement)
                   << "\",\"start\":{\"offset\":" << diagnostic.fix_it->span.start.offset
                   << ",\"line\":" << diagnostic.fix_it->span.start.line
                   << ",\"column\":" << diagnostic.fix_it->span.start.column
                   << "},\"end\":{\"offset\":" << diagnostic.fix_it->span.end.offset
                   << ",\"line\":" << diagnostic.fix_it->span.end.line
                   << ",\"column\":" << diagnostic.fix_it->span.end.column << "}}";
        } else {
            output << "null";
        }
        output << ",\"notes\":[";
        for (std::size_t note_index = 0; note_index < diagnostic.notes.size(); ++note_index) {
            if (note_index != 0) output << ',';
            const auto& note = diagnostic.notes[note_index];
            output << "{\"message\":\"" << json_escape(note.message)
                   << "\",\"start\":{\"offset\":" << note.span.start.offset
                   << ",\"line\":" << note.span.start.line
                   << ",\"column\":" << note.span.start.column
                   << "},\"end\":{\"offset\":" << note.span.end.offset
                   << ",\"line\":" << note.span.end.line
                   << ",\"column\":" << note.span.end.column << "}}";
        }
        output << "]}";
    }
    output << "]}";
    return output.str();
}

}