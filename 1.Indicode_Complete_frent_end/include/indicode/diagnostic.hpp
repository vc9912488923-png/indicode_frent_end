#pragma once

#include "indicode/source.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace indicode {

enum class DiagnosticSeverity {
    Error,
    Warning
};

struct DiagnosticFixIt {
    SourceSpan span;
    std::string replacement;
    std::string message;
};

struct DiagnosticNote {
    SourceSpan span;
    std::string message;
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    SourceSpan span;
    std::optional<DiagnosticFixIt> fix_it;
    std::vector<DiagnosticNote> notes;

    Diagnostic() = default;
    Diagnostic(DiagnosticSeverity severity, std::string code, std::string message,
               SourceSpan span, std::optional<DiagnosticFixIt> fix_it = std::nullopt)
        : severity(severity), code(std::move(code)), message(std::move(message)),
          span(span), fix_it(std::move(fix_it)) {}
};

std::string format_diagnostic_text(const SourceFile& source, const Diagnostic& diagnostic);
std::string format_diagnostic_text(const SourceManager& sources, SourceId source_id,
                                   const Diagnostic& diagnostic);
std::string format_diagnostics_json(const SourceFile& source,
                                    const std::vector<Diagnostic>& diagnostics);

}