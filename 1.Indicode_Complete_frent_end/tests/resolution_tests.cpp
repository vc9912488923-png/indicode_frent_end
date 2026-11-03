#include "indicode/resolution.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool has_code(const std::vector<indicode::Diagnostic>& diagnostics, const char* code) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "indicode-resolution-tests";
    fs::remove_all(root);
    fs::create_directories(root / "math");
    fs::create_directories(root / "duplicate" / "math");
    {
        std::ofstream(root / "math" / "vector.indi") << "module math.vector\n";
        std::ofstream(root / "duplicate" / "math" / "vector.indi") << "module math.vector\n";
        std::ofstream(root / "math" / "scalar.indi") << "module math.scalar\n";
    }

    indicode::ModuleResolver one_root({root});
    std::filesystem::path resolved_path;
    require(one_root.resolve_path("math.vector", resolved_path).empty() &&
                resolved_path == root / "math" / "vector.indi",
            "module resolver should map dotted identities to deterministic source paths");
    require(has_code(one_root.resolve_path("../escape", resolved_path), "E-MOD-001"),
            "module resolver should reject traversal-shaped module identities");
    require(has_code(one_root.resolve_path("missing.module", resolved_path), "E-MOD-002"),
            "module resolver should diagnose missing modules");
    indicode::ModuleResolver ambiguous_roots({root, root / "duplicate"});
    require(has_code(ambiguous_roots.resolve_path("math.vector", resolved_path), "E-MOD-003"),
            "module resolver should diagnose duplicate matches across search roots");

    const indicode::SourceSpan import_span{{10, 2, 1}, {20, 2, 11}};
    indicode::ModuleManifest root_module{
        "app.main", root / "main.indi",
        {{"math.vector", "vec", import_span}, {"math.scalar", "num", import_span}}, {}};
    std::unordered_map<std::string, indicode::ModuleManifest> modules{
        {"math.vector", {"math.vector", root / "math" / "vector.indi", {},
                         {{"Point", "record", true}, {"Internal", "record", false}}}},
        {"math.scalar", {"math.scalar", root / "math" / "scalar.indi", {},
                         {{"add", "function", true}}}}
    };
    indicode::ImportResolver import_resolver;
    const auto imports = import_resolver.resolve(root_module, modules);
    require(imports.diagnostics.empty() && imports.imports.size() == 2,
            "import resolver should resolve module aliases");
    require(imports.imports[0].alias == "vec" &&
                imports.imports[0].visible_exports.size() == 1 &&
                imports.imports[0].visible_exports[0].name == "Point",
            "imports should expose public exports only");

    auto invalid_imports = root_module;
    invalid_imports.imports.push_back({"missing.module", "vec", import_span});
    const auto invalid_result = import_resolver.resolve(invalid_imports, modules);
    require(has_code(invalid_result.diagnostics, "E-IMP-002") &&
                has_code(invalid_result.diagnostics, "E-IMP-003"),
            "import resolver should diagnose duplicate aliases and missing modules");

    auto cyclic_modules = modules;
    cyclic_modules["math.vector"].imports.push_back({"math.scalar", "num", import_span});
    cyclic_modules["math.scalar"].imports.push_back({"math.vector", "vec", import_span});
    const auto cyclic_result = import_resolver.resolve(root_module, cyclic_modules);
    require(has_code(cyclic_result.diagnostics, "E-MOD-004"),
            "import resolver should reject deterministic import cycles");

    const std::vector<indicode::FunctionCandidate> candidates{
        {101, "combine", {{"left", "Int"}, {"right", "Int"}}, "Int"},
        {102, "combine", {{"left", "Text"}, {"right", "Text"}}, "Text"}
    };
    const auto int_match = indicode::resolve_overload(
        candidates, {{"", "Int"}, {"", "Int"}});
    require(int_match.kind == indicode::OverloadResolutionKind::Selected &&
                int_match.selected_symbol_id == 101,
            "overload resolution should select the exact positional signature");
    const auto named_match = indicode::resolve_overload(
        candidates, {{"right", "Text"}, {"left", "Text"}});
    require(named_match.kind == indicode::OverloadResolutionKind::Selected &&
                named_match.selected_symbol_id == 102,
            "overload resolution should bind named arguments independent of their order");
    const auto no_match = indicode::resolve_overload(candidates, {{"", "Bool"}});
    require(no_match.kind == indicode::OverloadResolutionKind::NoMatch,
            "overload resolution should report no viable candidate");
    const std::vector<indicode::FunctionCandidate> ambiguous_candidates{
        {201, "same", {{"value", "Int"}}, "Int"},
        {202, "same", {{"value", "Int"}}, "Text"}
    };
    const auto ambiguous = indicode::resolve_overload(ambiguous_candidates, {{"", "Int"}});
    require(ambiguous.kind == indicode::OverloadResolutionKind::Ambiguous &&
                ambiguous.candidate_symbol_ids.size() == 2,
            "overload resolution should report all equally viable candidates");
    require(indicode::resolve_overload(candidates, {{"right", "Int"}, {"", "Int"}}).kind ==
                indicode::OverloadResolutionKind::NoMatch,
            "overload resolution should reject positional arguments after named arguments");

    fs::remove_all(root);
    std::cout << "Module, import, and overload resolution tests passed\n";
}