#include "indicode/hir.hpp"
#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"
#include "indicode/sema.hpp"
#include "indicode/symbol_index.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

indicode::HirProgram compile_source(const std::string& contents) {
    const indicode::SourceFile source{"index.indi", contents};
    const auto lexed = indicode::Lexer(source).lex();
    require(lexed.diagnostics.empty(), "symbol-index fixture should lex cleanly");
    auto parsed = indicode::Parser(lexed.tokens).parse();
    require(parsed.diagnostics.empty(), "symbol-index fixture should parse cleanly");
    const auto semantic = indicode::Analyzer().analyze(parsed.program);
    require(semantic.diagnostics.empty(), "symbol-index fixture should pass semantic checks");
    auto hir = indicode::lower_to_hir(parsed.program);
    require(indicode::verify_hir(hir).empty(), "symbol-index fixture HIR should verify");
    return hir;
}

}

int main() {
    const std::string source =
        "record Point { x: Int }\n"
        "function get(point: Point) -> Int { return point.x }\n"
        "origin = Point(x: 1)\n"
        "answer = get(point: origin)\n"
        "for item in 0..2 { say(item) }\n";
    const auto hir = compile_source(source);

    indicode::SymbolIndex index;
    index.rebuild(hir, 1);
    require(index.is_current_for(1) && !index.is_current_for(2),
            "index should report the source revision it represents");

    const auto function_id = hir.functions[0].symbol_id;
    const auto* function = index.find_symbol(function_id);
    require(function != nullptr && function->has_definition && function->name == "get" &&
                function->kind == indicode::IndexedSymbolKind::Function,
            "index should expose function definition metadata");
    require(index.references_to(function_id).size() == 1,
            "function call should be indexed as a reference to its definition");

    const auto parameter_id = hir.functions[0].parameters[0].symbol_id;
    const auto* parameter = index.find_symbol(parameter_id);
    require(parameter != nullptr && parameter->kind == indicode::IndexedSymbolKind::Parameter &&
                index.references_to(parameter_id).size() == 1,
            "parameter definition and member-base reference should be indexed");

    const auto field_id = hir.records[0].fields[0].symbol_id;
    require(index.find_symbol(field_id) != nullptr &&
                index.references_to(field_id).size() == 1,
            "record field definition and member reference should be indexed");

    const auto loop_binding_id = hir.statements[2]->symbol_id;
    const auto* loop_binding = index.find_symbol(loop_binding_id);
    require(loop_binding != nullptr &&
                loop_binding->kind == indicode::IndexedSymbolKind::LoopBinding &&
                index.references_to(loop_binding_id).size() == 1,
            "loop binding should have a scoped definition and indexed use");

    const auto loop_body_offset = source.find("say(item)");
    const auto* loop_scope = index.scope_at(loop_body_offset);
    require(loop_scope != nullptr && loop_scope->parent_id != 0 &&
                loop_scope->depth >= 1 && loop_binding->scope_id == loop_scope->id,
            "scope lookup should identify the loop binding's lexical block");

    const auto replacement_hir = compile_source("replacement = 4\n");
    index.rebuild(replacement_hir, 2);
    require(index.is_current_for(2) && index.find_symbol(function_id) == nullptr &&
                index.references_to(function_id).empty(),
            "rebuilding for a new revision should invalidate stale definitions and references");

    std::cout << "Symbol index tests passed\n";
}