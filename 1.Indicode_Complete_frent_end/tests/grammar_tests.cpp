#include "indicode/grammar.hpp"
#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::unique_ptr<indicode::Expr> parse_expression(const std::string& expression) {
    const indicode::SourceFile source{"grammar.indi", "value = " + expression + "\n"};
    const auto lexed = indicode::Lexer(source).lex();
    auto parsed = indicode::Parser(lexed.tokens).parse();
    require(lexed.diagnostics.empty() && parsed.diagnostics.empty() &&
                parsed.program.statements.size() == 1,
            "documented expression should parse cleanly");
    return std::move(parsed.program.statements[0]->expression);
}

}

int main() {
    require(indicode::grammar_version == "1.1.0",
            "public grammar version should match the versioned language specification");
    std::ifstream specification(INDICODE_SPEC_PATH);
    require(specification.good(), "versioned language specification should be present");
    const std::string specification_text{
        std::istreambuf_iterator<char>(specification), std::istreambuf_iterator<char>()};
    require(specification_text.find("Grammar version:** 1.1.0") != std::string::npos,
            "public grammar version and normative specification version should agree");
    for (const auto* documented_operator : {"||", "&&", "..", "==", "!=", "<=", ">=", "+",
                                             "-", "*", "/", "%"}) {
        require(specification_text.find(documented_operator) != std::string::npos,
                "normative specification should list every Pratt binary operator");
    }
    require(specification_text.find("Modules/imports") != std::string::npos,
            "syntax outside grammar 1.1 should be explicitly scoped out");

    constexpr std::array operators{
        std::pair{"||", indicode::ExprKind::Binary},
        std::pair{"&&", indicode::ExprKind::Binary},
        std::pair{"..", indicode::ExprKind::Range},
        std::pair{"==", indicode::ExprKind::Binary},
        std::pair{"!=", indicode::ExprKind::Binary},
        std::pair{"<", indicode::ExprKind::Binary},
        std::pair{"<=", indicode::ExprKind::Binary},
        std::pair{">", indicode::ExprKind::Binary},
        std::pair{">=", indicode::ExprKind::Binary},
        std::pair{"+", indicode::ExprKind::Binary},
        std::pair{"-", indicode::ExprKind::Binary},
        std::pair{"*", indicode::ExprKind::Binary},
        std::pair{"/", indicode::ExprKind::Binary},
        std::pair{"%", indicode::ExprKind::Binary}
    };
    for (const auto& [operation, kind] : operators) {
        auto root = parse_expression(std::string("a ") + operation + " b " + operation + " c");
        require(root->kind == kind && root->value == operation && root->left &&
                    root->left->kind == kind && root->left->value == operation,
                "every binary operator should associate left as specified");
    }

    auto precedence = parse_expression("a || b && c == d < e + f * g");
    require(precedence->kind == indicode::ExprKind::Binary && precedence->value == "||" &&
                precedence->right->kind == indicode::ExprKind::Binary &&
                precedence->right->value == "&&" &&
                precedence->right->right->kind == indicode::ExprKind::Binary &&
                precedence->right->right->value == "==" &&
                precedence->right->right->right->kind == indicode::ExprKind::Binary &&
                precedence->right->right->right->value == "<" &&
                precedence->right->right->right->right->kind == indicode::ExprKind::Binary &&
                precedence->right->right->right->right->value == "+" &&
                precedence->right->right->right->right->right->value == "*",
            "binary precedence should match the normative table from || through multiplication");
    auto range_precedence = parse_expression("a .. b == c");
    require(range_precedence->kind == indicode::ExprKind::Range &&
                range_precedence->value == ".." && range_precedence->right &&
                range_precedence->right->kind == indicode::ExprKind::Binary &&
                range_precedence->right->value == "==",
            "equality precedence should bind more tightly than range");

    auto prefix = parse_expression("!-~a");
    require(prefix->kind == indicode::ExprKind::Unary && prefix->value == "!" &&
                prefix->left->kind == indicode::ExprKind::Unary && prefix->left->value == "-" &&
                prefix->left->left->kind == indicode::ExprKind::Unary &&
                prefix->left->left->value == "~",
            "prefix operators should nest right-to-left");
    auto unary_plus = parse_expression("+a");
    require(unary_plus->kind == indicode::ExprKind::Unary && unary_plus->value == "+",
            "unary plus should be accepted as specified");

    auto postfix = parse_expression("items[0].value");
    require(postfix->kind == indicode::ExprKind::Member && postfix->value == "value" &&
                postfix->left->kind == indicode::ExprKind::Index,
            "index and member postfix forms should bind before binary operators");

    const indicode::SourceFile constructs{
        "grammar_constructs.indi",
        "record Pair { first: Int, second: Int }\n"
        "function identity(value: List<Int>) -> List<Int> { return value }\n"
        "let values: List<Int> = [1, 2]\n"
        "var current = identity(values)[0]\n"
        "if true { current = current + 1 } else if false { current = 0 } else { return }\n"
        "unless false { current = current + 1 } else unless true { current = 0 }\n"
        "while false { current = current - 1 }\n"
        "repeat 2 times { current = current + 1 }\n"
        "for item in 0..3 { current = current + item }\n"
    };
    const auto lexed = indicode::Lexer(constructs).lex();
    const auto parsed = indicode::Parser(lexed.tokens).parse();
    require(lexed.diagnostics.empty() && parsed.diagnostics.empty() &&
                parsed.program.records.size() == 1 && parsed.program.functions.size() == 1 &&
                parsed.program.statements.size() == 7,
            "every declaration and statement form in grammar 1.1.0 should parse");

    const indicode::SourceFile unsupported{"unsupported.indi", "import library\n"};
    const auto unsupported_lexed = indicode::Lexer(unsupported).lex();
    const auto unsupported_parsed = indicode::Parser(unsupported_lexed.tokens).parse();
    require(!unsupported_parsed.diagnostics.empty(),
            "module and import syntax should be rejected without a module declaration");

    const indicode::SourceFile generic_source{
        "generic.indi",
        "function identity<T>(value: T) -> T { return value }\n"
        "function add<T: Numeric>(left: T, right: T) -> T { return left + right }\n"
    };
    const auto generic_lexed = indicode::Lexer(generic_source).lex();
    const auto generic_parsed = indicode::Parser(generic_lexed.tokens).parse();
    require(generic_lexed.diagnostics.empty() && generic_parsed.diagnostics.empty() &&
                generic_parsed.program.functions.size() == 2 &&
                generic_parsed.program.functions[0].generic_parameters.size() == 1 &&
                generic_parsed.program.functions[1].generic_parameters[0].constraint == "Numeric",
            "generic function parameters and constraints should parse");
    const auto generic_ast_json = indicode::serialize_ast_json(generic_parsed.program);
    require(generic_ast_json.find("\"schemaVersion\":2") != std::string::npos &&
                generic_ast_json.find("\"constraint\":\"Numeric\"") != std::string::npos &&
                indicode::print_program(generic_parsed.program).find("add<T: Numeric>") !=
                    std::string::npos,
            "AST JSON and printer should preserve generic parameter constraints");

    const indicode::SourceFile modes_source{
        "modes.indi",
        "function consume(value: move List<Int>) -> Void effects(io, mutation) { return }\n"
        "function inspect(value: borrow List<Int>) {}\n"
        "function update(value: borrow_mut List<Int>) {}\n"
        "function caller(value: List<Int>) { consume(move value) }\n"
    };
    const auto modes_lexed = indicode::Lexer(modes_source).lex();
    const auto modes_parsed = indicode::Parser(modes_lexed.tokens).parse();
    require(modes_lexed.diagnostics.empty() && modes_parsed.diagnostics.empty() &&
                modes_parsed.program.functions.size() == 4 &&
                modes_parsed.program.functions[0].parameters[0].ownership_mode ==
                    indicode::OwnershipMode::Move &&
                modes_parsed.program.functions[0].has_effect_annotation &&
                modes_parsed.program.functions[0].declared_effects.size() == 2 &&
                modes_parsed.program.functions[3].body[0]->expression->arguments[0]->value == "move",
            "parameter ownership modes, effect annotations, and move expressions should parse");

    std::cout << "Grammar tests passed\n";
}
