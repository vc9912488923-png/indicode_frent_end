#include "indicode/ast.hpp"
#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"

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

}

int main() {
    const indicode::SourceFile source{
        "ast_printer.indi",
        "record Point { x: Int, y: Int }\n"
        "function project(point: Point, values: List<Int>) -> Int {\n"
        "  let index: Int = 0\n"
        "  var result = -index\n"
        "  result = result + values[index]\n"
        "  if true { return point.x } else if false { return values[0] } else { return 0 }\n"
        "}\n"
        "items = [1, 2]\n"
        "first = items[0]\n"
        "call = project(point: Point(x: 0, y: 1), values: items)\n"
        "while false { call = call - 1 }\n"
        "repeat 2 times { log(call) }\n"
        "for value in 0..3 { log(value) }\n"
        "log(!false && true || false)\n"
        "prefixes = +1 + ~2\n"
        "arithmetic = 6 / 2 % 2 * 1\n"
        "equality = 1 == 2 != 3\n"
        "ordering = 1 < 2 <= 3 > 2 >= 1\n"
    };
    const auto lexed = indicode::Lexer(source).lex();
    const auto parsed = indicode::Parser(lexed.tokens).parse();
    require(lexed.diagnostics.empty() && parsed.diagnostics.empty(),
            "printer fixture should parse without errors");

    const std::string expected =
        "record Point {\n"
        "  x: Int\n"
        "  y: Int\n"
        "}\n"
        "function project(point: Point, values: List<Int>) -> Int\n"
        "  let Int index = Literal(0)\n"
        "  var result = Unary(- Name(index))\n"
        "  result = Binary(+ Name(result) Index(Name(values), Name(index)))\n"
        "  if Literal(true)\n"
        "    return Name(point).x\n"
        "  else\n"
        "    if Literal(false)\n"
        "      return Index(Name(values), Literal(0))\n"
        "    else\n"
        "      return Literal(0)\n"
        "items = List[Literal(1), Literal(2)]\n"
        "first = Index(Name(items), Literal(0))\n"
        "call = Call(project point:Call(Point x:Literal(0) y:Literal(1)) values:Name(items))\n"
        "while Literal(false)\n"
        "  call = Binary(- Name(call) Literal(1))\n"
        "repeat Literal(2) times\n"
        "  Call(log Name(call))\n"
        "for value in Range(Literal(0) .. Literal(3))\n"
        "  Call(log Name(value))\n"
        "Call(log Binary(|| Binary(&& Unary(! Literal(false)) Literal(true)) Literal(false)))\n"
        "prefixes = Binary(+ Unary(+ Literal(1)) Unary(~ Literal(2)))\n"
        "arithmetic = Binary(* Binary(% Binary(/ Literal(6) Literal(2)) Literal(2)) Literal(1))\n"
        "equality = Binary(!= Binary(== Literal(1) Literal(2)) Literal(3))\n"
        "ordering = Binary(>= Binary(> Binary(<= Binary(< Literal(1) Literal(2)) Literal(3)) Literal(2)) Literal(1))\n";
    const auto output = indicode::print_program(parsed.program);
    if (output != expected) {
        std::cerr << "Expected AST output:\n" << expected << "Actual AST output:\n" << output;
    }
    require(output == expected, "AST printer output should match the complete golden snapshot");
    require(indicode::print_program(parsed.program) == output,
            "AST printer output should be deterministic");

    std::cout << "AST printer tests passed\n";
}
