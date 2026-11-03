#include "indicode/incremental_parser.hpp"

#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

indicode::Program parse_fresh(const indicode::SourceFile& source) {
    const auto lexed = indicode::Lexer(source).lex();
    require(lexed.diagnostics.empty(), "full-parse fixture should lex cleanly");
    auto parsed = indicode::Parser(lexed.tokens).parse();
    require(parsed.diagnostics.empty(), "full-parse fixture should parse cleanly");
    return std::move(parsed.program);
}

std::string print_fragments(const indicode::IncrementalParseResult& result) {
    std::string printed;
    for (const auto& item : result.items) {
        printed += indicode::print_program(item.program);
    }
    return printed;
}

}

int main() {
    const std::string initial_source =
        "record Point { x: Int }\n"
        "function identity<T>(value: T) -> T { return value }\n"
        "first = identity(1)\n"
        "second = 2\n"
        "third = 3\n";

    indicode::IncrementalParser parser({"incremental.indi", initial_source});
    const auto& initial = parser.parse();
    require(initial.revision == 1 && initial.items.size() == 5 &&
                initial.parsed_item_count == 5 && initial.reused_item_count == 0 &&
                initial.diagnostics.empty(),
            "initial parse should produce five independent clean top-level fragments");

    const auto number_offset = initial_source.find("second = 2") + std::string("second = ").size();
    const auto& updated = parser.apply_edit({number_offset, 1, "200"});
    require(updated.revision == 2 && updated.parsed_item_count == 1 &&
                updated.reused_item_count == 4 && updated.items.size() == 5 &&
                updated.diagnostics.empty(),
            "editing one declaration should reparse only it and reuse the other fragments");
    require(updated.items[0].reused && updated.items[1].reused && updated.items[2].reused &&
                !updated.items[3].reused && updated.items[4].reused,
            "reuse flags should identify exactly the unchanged top-level fragments");

    const auto third_offset = parser.source_text().find("third = 3");
    require(updated.items[4].program.statements.size() == 1 &&
                updated.items[4].program.statements[0]->span.start.offset == third_offset,
            "reused AST spans after the edit should shift to their new source offsets");
    auto fresh_program = parse_fresh({"incremental.indi", parser.source_text()});
    require(print_fragments(updated) == indicode::print_program(fresh_program),
            "incremental fragments should match a clean full parse after editing");

        const auto malformed_offset = parser.source_text().find("second = 200") +
                      std::string("second = ").size();
        const auto& malformed = parser.apply_edit({malformed_offset, 3, "("});
        std::cerr << "malformed diagnostics=" << malformed.diagnostics.size()
                  << " item diagnostics=" << malformed.items[3].diagnostics.size()
                  << " reused=" << malformed.reused_item_count << '\n';
        require(!malformed.diagnostics.empty() && malformed.items[3].diagnostics.size() > 0 &&
            malformed.items[4].reused && malformed.reused_item_count == 4,
            "an invalid edited fragment should be reparsed without invalidating later clean fragments");

        const auto repair_offset = parser.source_text().find("second = (") +
                       std::string("second = ").size();
        const auto& repaired = parser.apply_edit({repair_offset, 1, "7"});
        require(repaired.diagnostics.empty() && repaired.items[3].diagnostics.empty() &&
            repaired.reused_item_count == 4,
            "repairing an invalid fragment should parse it again and reuse its clean neighbors");
        fresh_program = parse_fresh({"incremental.indi", parser.source_text()});
        require(print_fragments(repaired) == indicode::print_program(fresh_program),
            "repaired incremental fragments should match a clean full parse");

    const auto& unchanged = parser.parse();
    require(unchanged.parsed_item_count == 0 && unchanged.reused_item_count == 5,
            "reparsing an unchanged revision should reuse every clean fragment");

    const auto text_before_invalid_edit = parser.source_text();
    const auto revision_before_invalid_edit = parser.revision();
    const auto& invalid_edit = parser.apply_edit({text_before_invalid_edit.size() + 1, 0, "x"});
    require(!invalid_edit.diagnostics.empty() &&
                invalid_edit.diagnostics.back().code == "E-INCR-001" &&
                parser.source_text() == text_before_invalid_edit &&
                parser.revision() == revision_before_invalid_edit,
            "out-of-range edits should be diagnosed without changing source or revision");

    std::cout << "Incremental parser tests passed\n";
}