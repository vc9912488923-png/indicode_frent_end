#include "indicode/hir.hpp"
#include "indicode/lexer.hpp"
#include "indicode/llvm_ir.hpp"
#include "indicode/parser.hpp"
#include "indicode/sema.hpp"

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

indicode::LlvmIrResult generate(const std::string& source_text) {
    const indicode::SourceFile source{"llvm_ir.indi", source_text};
    const auto lexed = indicode::Lexer(source).lex();
    require(lexed.diagnostics.empty(), "IR test source should lex");
    auto parsed = indicode::Parser(lexed.tokens).parse();
    require(parsed.diagnostics.empty(), "IR test source should parse");
    const auto semantic = indicode::Analyzer().analyze(parsed.program);
    require(semantic.diagnostics.empty(), "IR test source should pass semantic checks");
    return indicode::generate_llvm_ir(indicode::lower_to_hir(parsed.program));
}

}

int main() {
    const auto supported = generate(
        "function add(left: Int, right: Int) -> Int { return left + right }\n"
        "function below(left: UInt2, right: UInt2) -> Bool { return left < right }\n"
        "function invert(value: Bool) -> Bool { return !value }\n"
        "function choose(value: Bool) -> Int { if value { return 1 } else { return 0 } }\n"
        "function countdown(start: Int) -> Int {\n"
        "    var current: Int = start\n"
        "    while current > 0 { current = current - 1 }\n"
        "    return current\n"
        "}\n"
        "function both(left: Bool, right: Bool) -> Bool { return left && right }\n"
        "function either(left: Bool, right: Bool) -> Bool { return left || right }\n"
        "function call_named(value: Int) -> Int { return add(right: 4, left: value) }\n"
        "function done(value: Int) { return }\n"
        "function invoke(value: Int) { done(value) }\n");
    require(supported.diagnostics.empty(), "scalar functions should generate IR");
    require(supported.ir.find("define i32 @add(i32 %in.") != std::string::npos &&
                supported.ir.find(" = add i32 ") != std::string::npos &&
                supported.ir.find(" = icmp ult i16 ") != std::string::npos &&
                supported.ir.find(" = xor i1 ") != std::string::npos &&
                supported.ir.find(" = icmp sgt i32 ") != std::string::npos &&
                supported.ir.find("phi i1 [ false, %") != std::string::npos &&
                supported.ir.find("phi i1 [ true, %") != std::string::npos &&
                supported.ir.find("call i32 @add(i32 ") != std::string::npos &&
                supported.ir.find("call void @done(i32 ") != std::string::npos &&
                supported.ir.find("while.cond.") != std::string::npos &&
                supported.ir.find("if.then.") != std::string::npos &&
                supported.ir.find("ret i32 ") != std::string::npos,
            "IR should lower scalar operations, calls, short-circuit logic, if, and while");

    const auto unsupported = generate(
        "function repeat_body() { repeat 1 times { } }\n");
    require(unsupported.ir.empty() && unsupported.diagnostics.size() == 1 &&
                unsupported.diagnostics.front().code == "E-IR-001",
            "unsupported repeat statements should fail with a stable IR diagnostic");
}