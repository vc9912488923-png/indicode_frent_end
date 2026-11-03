#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}

int main() {
    constexpr std::size_t statement_count = 10000;
    std::string contents = "function large() -> Void {\n";
    for (std::size_t index = 0; index < statement_count; ++index) {
        contents += "  let value_" + std::to_string(index) + ": Int = 1\n";
    }
    contents += "}\n";

    const indicode::SourceFile source{"large.indi", std::move(contents)};
    const auto lexed = indicode::Lexer(source).lex();
    require(lexed.diagnostics.empty(), "large AST fixture should lex cleanly");

    const auto parse_start = std::chrono::steady_clock::now();
    auto parsed = indicode::Parser(lexed.tokens).parse();
    const auto parse_end = std::chrono::steady_clock::now();
    require(parsed.diagnostics.empty(), "large AST fixture should parse cleanly");
    require(parsed.program.functions.size() == 1 &&
                parsed.program.functions[0].body.size() == statement_count,
            "large AST should retain every generated statement");

    std::optional<indicode::Program> retained_program;
    retained_program.emplace(std::move(parsed.program));
    const auto destruction_start = std::chrono::steady_clock::now();
    retained_program.reset();
    const auto destruction_end = std::chrono::steady_clock::now();

    const auto parse_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        parse_end - parse_start).count();
    const auto destruction_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        destruction_end - destruction_start).count();
    std::cout << "AST scalability: " << statement_count << " statements, parse "
              << parse_time << " ms, destruction " << destruction_time << " ms\n";
}
