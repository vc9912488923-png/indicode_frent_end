#include "indicode/ast.hpp"
#include "indicode/hir.hpp"
#include "indicode/lexer.hpp"
#include "indicode/llvm_ir.hpp"
#include "indicode/parser.hpp"
#include "indicode/sema.hpp"
#include "indicode/token.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

namespace {

void print_diagnostics(const indicode::SourceManager& sources, indicode::SourceId source_id,
                       const std::vector<indicode::Diagnostic>& diagnostics,
                       bool json_diagnostics) {
    const auto& source = sources.get_source(source_id);
    if (json_diagnostics) {
        std::cerr << indicode::format_diagnostics_json(source, diagnostics) << '\n';
        return;
    }
    for (const auto& diagnostic : diagnostics) {
        std::cerr << indicode::format_diagnostic_text(sources, source_id, diagnostic) << '\n';
    }
}

}

int main(int argc, char** argv) {
    bool json_diagnostics = false;
    int argument = 1;
    if (argument < argc && std::string(argv[argument]) == "--json-diagnostics") {
        json_diagnostics = true;
        ++argument;
    }
    if (argc - argument != 2) {
        std::cerr << "usage: indicode [--json-diagnostics] <check|tokens|ast|ast-json|typed|typed-json|ir> <file.indi>\n";
        return 2;
    }

    const std::string command = argv[argument++];
    if (command != "check" && command != "tokens" && command != "ast" &&
        command != "ast-json" &&
        command != "typed" && command != "typed-json" && command != "ir") {
        std::cerr << "unknown command: " << command << '\n';
        return 2;
    }

    const std::string source_name = argv[argument];
    std::ifstream input(source_name, std::ios::binary);
    if (!input) {
        std::cerr << "cannot open source file: " << source_name << '\n';
        return 2;
    }
    std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    indicode::SourceManager source_manager;
    const auto source_id = source_manager.add_source(source_name, std::move(contents));
    const auto& source = source_manager.get_source(source_id);

    auto lexed = indicode::Lexer(source).lex();
    if (command == "tokens") {
        for (const auto& token : lexed.tokens) {
            std::cout << token.span.start.line << ':' << token.span.start.column << ' '
                      << indicode::token_kind_name(token.kind);
            if (!token.lexeme.empty()) {
                std::cout << " `" << token.lexeme << '`';
            }
            std::cout << '\n';
        }
        print_diagnostics(source_manager, source_id, lexed.diagnostics, json_diagnostics);
        return lexed.diagnostics.empty() ? 0 : 1;
    }
    if (!lexed.diagnostics.empty()) {
        print_diagnostics(source_manager, source_id, lexed.diagnostics, json_diagnostics);
        return 1;
    }

    auto parsed = indicode::Parser(lexed.tokens).parse();
    if (!parsed.diagnostics.empty()) {
        print_diagnostics(source_manager, source_id, parsed.diagnostics, json_diagnostics);
        return 1;
    }
    if (command == "ast") {
        std::cout << indicode::print_program(parsed.program);
        return 0;
    }
    if (command == "ast-json") {
        std::cout << indicode::serialize_ast_json(parsed.program) << '\n';
        return 0;
    }

    auto analyzed = indicode::Analyzer().analyze(parsed.program);
    if (!analyzed.diagnostics.empty()) {
        print_diagnostics(source_manager, source_id, analyzed.diagnostics, json_diagnostics);
        return 1;
    }
    auto hir = indicode::lower_to_hir(parsed.program);
    const auto hir_diagnostics = indicode::verify_hir(hir);
    if (!hir_diagnostics.empty()) {
        print_diagnostics(source_manager, source_id, hir_diagnostics, json_diagnostics);
        return 1;
    }
    if (command == "typed") {
        std::cout << indicode::print_hir(hir);
    } else if (command == "typed-json") {
        std::cout << indicode::serialize_hir_json(hir) << '\n';
    } else if (command == "ir") {
        auto generated = indicode::generate_llvm_ir(hir);
        if (!generated.diagnostics.empty()) {
            print_diagnostics(source_manager, source_id, generated.diagnostics, json_diagnostics);
            return 1;
        }
        std::cout << generated.ir;
    } else {
        std::cout << "ok: " << source.name << '\n';
    }
    return 0;
}