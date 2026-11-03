#include "indicode/resolution.hpp"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace indicode {
namespace {

bool identifier_segment(const std::string& value) {
    if (value.empty() || !(value[0] == '_' ||
        (value[0] >= 'A' && value[0] <= 'Z') ||
        (value[0] >= 'a' && value[0] <= 'z'))) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](char character) {
        return character == '_' || (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9');
    });
}

bool module_name_is_valid(const std::string& module_name) {
    std::size_t start = 0;
    while (start < module_name.size()) {
        const auto separator = module_name.find('.', start);
        const auto end = separator == std::string::npos ? module_name.size() : separator;
        if (!identifier_segment(module_name.substr(start, end - start))) return false;
        if (separator == std::string::npos) return true;
        start = separator + 1;
    }
    return false;
}

std::string default_alias(const std::string& module_name) {
    const auto separator = module_name.rfind('.');
    return separator == std::string::npos ? module_name : module_name.substr(separator + 1);
}

bool candidate_matches(const FunctionCandidate& candidate,
                       const std::vector<CallArgumentType>& arguments) {
    if (candidate.parameters.size() != arguments.size()) return false;
    std::vector<bool> assigned(candidate.parameters.size(), false);
    bool saw_named_argument = false;
    std::size_t next_positional = 0;
    for (const auto& argument : arguments) {
        std::size_t parameter_index = 0;
        if (argument.name.empty()) {
            if (saw_named_argument) return false;
            parameter_index = next_positional++;
        } else {
            saw_named_argument = true;
            const auto parameter = std::find_if(
                candidate.parameters.begin(), candidate.parameters.end(),
                [&argument](const FunctionParameterType& item) {
                    return item.name == argument.name;
                });
            if (parameter == candidate.parameters.end()) return false;
            parameter_index = static_cast<std::size_t>(parameter - candidate.parameters.begin());
        }
        if (parameter_index >= candidate.parameters.size() || assigned[parameter_index] ||
            candidate.parameters[parameter_index].type != argument.type) {
            return false;
        }
        assigned[parameter_index] = true;
    }
    return std::all_of(assigned.begin(), assigned.end(), [](bool value) { return value; });
}

}

ModuleResolver::ModuleResolver(std::vector<std::filesystem::path> search_roots)
    : search_roots_(std::move(search_roots)) {}

std::vector<Diagnostic> ModuleResolver::resolve_path(
    const std::string& module_name, std::filesystem::path& resolved_path) const {
    if (!module_name_is_valid(module_name)) {
        return {{DiagnosticSeverity::Error, "E-MOD-001",
                 "invalid dotted module name", {}}};
    }

    std::filesystem::path relative_path;
    std::size_t start = 0;
    while (start < module_name.size()) {
        const auto separator = module_name.find('.', start);
        const auto end = separator == std::string::npos ? module_name.size() : separator;
        relative_path /= module_name.substr(start, end - start);
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    relative_path += ".indi";

    std::vector<std::filesystem::path> matches;
    for (const auto& root : search_roots_) {
        const auto candidate = (root / relative_path).lexically_normal();
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            matches.push_back(candidate);
        }
    }
    if (matches.empty()) {
        return {{DiagnosticSeverity::Error, "E-MOD-002",
                 "module source was not found in the configured search roots", {}}};
    }
    if (matches.size() != 1) {
        return {{DiagnosticSeverity::Error, "E-MOD-003",
                 "module source is ambiguous across the configured search roots", {}}};
    }
    resolved_path = matches.front();
    return {};
}

ImportResolution ImportResolver::resolve(
    const ModuleManifest& root,
    const std::unordered_map<std::string, ModuleManifest>& modules) const {
    ImportResolution result;
    std::unordered_set<std::string> aliases;
    for (const auto& import : root.imports) {
        const auto alias = import.alias.empty() ? default_alias(import.module_name) : import.alias;
        bool valid_alias = true;
        if (!identifier_segment(alias)) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, "E-IMP-001",
                                          "invalid import alias", import.span});
            valid_alias = false;
        }
        if (valid_alias && !aliases.insert(alias).second) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, "E-IMP-002",
                                          "import alias is ambiguous", import.span});
            valid_alias = false;
        }
        const auto target = modules.find(import.module_name);
        if (target == modules.end()) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, "E-IMP-003",
                                          "imported module was not resolved", import.span});
            continue;
        }
        if (!valid_alias) {
            continue;
        }
        ResolvedImport resolved{import.module_name, alias, target->second.source_path, {}, import.span};
        for (const auto& exported : target->second.exports) {
            if (exported.is_public) resolved.visible_exports.push_back(exported);
        }
        result.imports.push_back(std::move(resolved));
    }

    enum class VisitState { Visiting, Visited };
    std::unordered_map<std::string, VisitState> states;
    std::function<void(const ModuleManifest&)> visit;
    visit = [&](const ModuleManifest& module) {
        states[module.name] = VisitState::Visiting;
        for (const auto& import : module.imports) {
            const auto target = modules.find(import.module_name);
            if (target == modules.end()) continue;
            const auto state = states.find(target->first);
            if (state != states.end() && state->second == VisitState::Visiting) {
                result.diagnostics.push_back({DiagnosticSeverity::Error, "E-MOD-004",
                                              "cyclic module import is not allowed", import.span});
            } else if (state == states.end()) {
                visit(target->second);
            }
        }
        states[module.name] = VisitState::Visited;
    };
    visit(root);
    return result;
}

OverloadResolution resolve_overload(const std::vector<FunctionCandidate>& candidates,
                                    const std::vector<CallArgumentType>& arguments) {
    std::vector<std::uint64_t> matches;
    for (const auto& candidate : candidates) {
        if (candidate_matches(candidate, arguments)) {
            matches.push_back(candidate.symbol_id);
        }
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    if (matches.empty()) return {OverloadResolutionKind::NoMatch, 0, {}};
    if (matches.size() == 1) return {OverloadResolutionKind::Selected, matches[0], matches};
    return {OverloadResolutionKind::Ambiguous, 0, std::move(matches)};
}

}