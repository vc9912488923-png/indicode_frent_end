#pragma once

#include "indicode/diagnostic.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace indicode {

struct ModuleImport {
    std::string module_name;
    std::string alias;
    SourceSpan span;
};

struct ModuleExport {
    std::string name;
    std::string kind;
    bool is_public = true;
};

struct ModuleManifest {
    std::string name;
    std::filesystem::path source_path;
    std::vector<ModuleImport> imports;
    std::vector<ModuleExport> exports;
};

struct ResolvedImport {
    std::string module_name;
    std::string alias;
    std::filesystem::path source_path;
    std::vector<ModuleExport> visible_exports;
    SourceSpan span;
};

struct ImportResolution {
    std::vector<ResolvedImport> imports;
    std::vector<Diagnostic> diagnostics;
};

class ModuleResolver {
public:
    explicit ModuleResolver(std::vector<std::filesystem::path> search_roots);
    std::vector<Diagnostic> resolve_path(const std::string& module_name,
                                         std::filesystem::path& resolved_path) const;

private:
    std::vector<std::filesystem::path> search_roots_;
};

class ImportResolver {
public:
    ImportResolution resolve(const ModuleManifest& root,
                             const std::unordered_map<std::string, ModuleManifest>& modules) const;
};

struct FunctionParameterType {
    std::string name;
    std::string type;
};

struct FunctionCandidate {
    std::uint64_t symbol_id = 0;
    std::string name;
    std::vector<FunctionParameterType> parameters;
    std::string return_type;
};

struct CallArgumentType {
    std::string name;
    std::string type;
};

enum class OverloadResolutionKind {
    Selected,
    NoMatch,
    Ambiguous
};

struct OverloadResolution {
    OverloadResolutionKind kind = OverloadResolutionKind::NoMatch;
    std::uint64_t selected_symbol_id = 0;
    std::vector<std::uint64_t> candidate_symbol_ids;
};

OverloadResolution resolve_overload(const std::vector<FunctionCandidate>& candidates,
                                    const std::vector<CallArgumentType>& arguments);

}