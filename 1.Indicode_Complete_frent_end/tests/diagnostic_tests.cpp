#include "indicode/diagnostic.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}

int main() {
    const indicode::SourceFile source{"sample\".indi", "Int x = \"bad\"\n"};
    const indicode::Diagnostic diagnostic{
        indicode::DiagnosticSeverity::Error,
        "E-TYPE-014",
        "cannot assign \"bad\" to Int\ncheck the value",
        {{8, 1, 9}, {13, 1, 14}}
    };

    const auto text = indicode::format_diagnostic_text(source, diagnostic);
    require(text.find("sample\".indi:1:9: error[E-TYPE-014]") != std::string::npos,
            "text diagnostic should include path, location, severity, and stable code");
    require(text.find("Int x = \"bad\"") != std::string::npos &&
                text.find("^^^^^") != std::string::npos,
            "text diagnostic should include the source line and underline");

    const auto json = indicode::format_diagnostics_json(source, {diagnostic});
    require(json.starts_with("{\"schemaVersion\":1,\"diagnostics\":[{") &&
                json.ends_with("}]}"),
            "JSON diagnostics should use the versioned schema envelope");
    require(json.find("sample\\\".indi") != std::string::npos,
            "JSON path should escape quotation marks");
    require(json.find("cannot assign \\\"bad\\\" to Int\\ncheck the value") != std::string::npos,
            "JSON message should escape quotes and line breaks");
    require(json.find("\"start\":{\"offset\":8,\"line\":1,\"column\":9}") != std::string::npos,
            "JSON should preserve structured start position");
    require(json.find("\"notes\":[]") != std::string::npos,
            "JSON should include an explicit notes array on each diagnostic");

    auto fixable_diagnostic = diagnostic;
    fixable_diagnostic.fix_it = indicode::DiagnosticFixIt{
        {{13, 1, 14}, {13, 1, 14}}, ": ", "insert ':' after the parameter name"
    };
    const auto fixable_text = indicode::format_diagnostic_text(source, fixable_diagnostic);
    require(fixable_text.find("help: insert ':' after the parameter name") != std::string::npos,
            "text diagnostics should display available fix-it guidance");
    const auto fixable_json = indicode::format_diagnostics_json(source, {fixable_diagnostic});
    require(fixable_json.find("\"fixIt\":{\"message\":\"insert ':' after the parameter name\"") !=
                std::string::npos &&
                fixable_json.find("\"replacement\":\": \"") != std::string::npos,
            "JSON diagnostics should encode fix-it text, replacement, and source span");
    require(indicode::format_diagnostics_json(source, {diagnostic}).find("\"fixIt\":null") !=
                std::string::npos,
            "JSON diagnostics without a fix-it should explicitly encode null");

    auto noted_diagnostic = fixable_diagnostic;
    noted_diagnostic.notes.push_back({
        {{0, 1, 1}, {3, 1, 4}}, "related declaration is here"
    });
    const auto noted_text = indicode::format_diagnostic_text(source, noted_diagnostic);
    require(noted_text.find("sample\".indi:1:1: note: related declaration is here") !=
                std::string::npos,
            "text diagnostics should report secondary source spans");
    const auto noted_json = indicode::format_diagnostics_json(source, {noted_diagnostic});
    require(noted_json.find("\"notes\":[{\"message\":\"related declaration is here\"") !=
                std::string::npos &&
                noted_json.find("\"start\":{\"offset\":0,\"line\":1,\"column\":1}") !=
                    std::string::npos,
            "JSON diagnostics should serialize note messages and source positions");
    const auto multiple_json = indicode::format_diagnostics_json(
        source, {diagnostic, noted_diagnostic});
    require(multiple_json.find("\"diagnostics\":[{") != std::string::npos &&
                multiple_json.find("},{\"severity\":\"error\"") != std::string::npos,
            "versioned JSON schema should preserve multiple diagnostic order");

    const indicode::SourceFile golden_source{"diag.indi", "x"};
    indicode::Diagnostic golden_diagnostic{
        indicode::DiagnosticSeverity::Warning,
        "W-TEST-001",
        "bad\nvalue",
        {{0, 1, 1}, {1, 1, 2}},
        indicode::DiagnosticFixIt{
            {{0, 1, 1}, {1, 1, 2}}, "y", "replace"
        }
    };
    golden_diagnostic.notes.push_back({{{0, 1, 1}, {1, 1, 2}}, "prior"});
    require(indicode::format_diagnostics_json(golden_source, {golden_diagnostic}) ==
                "{\"schemaVersion\":1,\"diagnostics\":[{\"severity\":\"warning\","
                "\"code\":\"W-TEST-001\",\"message\":\"bad\\nvalue\",\"file\":\"diag.indi\","
                "\"start\":{\"offset\":0,\"line\":1,\"column\":1},"
                "\"end\":{\"offset\":1,\"line\":1,\"column\":2},"
                "\"fixIt\":{\"message\":\"replace\",\"replacement\":\"y\","
                "\"start\":{\"offset\":0,\"line\":1,\"column\":1},"
                "\"end\":{\"offset\":1,\"line\":1,\"column\":2}},"
                "\"notes\":[{\"message\":\"prior\","
                "\"start\":{\"offset\":0,\"line\":1,\"column\":1},"
                "\"end\":{\"offset\":1,\"line\":1,\"column\":2}}]}]}",
            "diagnostic schema v1 should preserve every field in deterministic golden output");

    const indicode::SourceFile unicode_source{"unicode.indi", "\tπ = value\n"};
    const indicode::Diagnostic unicode_diagnostic{
        indicode::DiagnosticSeverity::Error,
        "E-TEST-001",
        "unknown name",
        {{6, 1, 7}, {11, 1, 12}}
    };
    const auto unicode_text = indicode::format_diagnostic_text(unicode_source, unicode_diagnostic);
    require(unicode_text.find("    π = value\n          ^^^^^") != std::string::npos,
            "diagnostic caret should align after tabs and multi-byte UTF-8 scalars");
    indicode::SourceManager unicode_sources;
    const auto unicode_source_id =
        unicode_sources.add_source(unicode_source.name, unicode_source.contents);
    require(indicode::format_diagnostic_text(unicode_sources, unicode_source_id,
                                             unicode_diagnostic) == unicode_text,
            "diagnostic rendering should use the source manager's indexed line and column mapping");

    std::cout << "Diagnostic tests passed\n";
}