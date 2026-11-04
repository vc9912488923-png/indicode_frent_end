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
        "function invert(value: Bool) -> Bool { return !value }\n");
    require(supported.diagnostics.empty(), "scalar functions should generate IR");
    require(supported.ir.find("define i32 @add(i32 %in.") != std::string::npos &&
                supported.ir.find(" = add i32 ") != std::string::npos &&
                supported.ir.find(" = icmp ult i16 ") != std::string::npos &&
                supported.ir.find(" = xor i1 ") != std::string::npos &&
                supported.ir.find("ret i32 ") != std::string::npos,
            "IR should map scalar types, arithmetic, unsigned comparisons, and returns");

    const auto unsupported = generate(
        "function choose(value: Bool) -> Int {\n"
        "    if value { return 1 } else { return 0 }\n"
        "}\n");
    require(unsupported.ir.empty() && unsupported.diagnostics.size() == 1 &&
                unsupported.diagnostics.front().code == "E-IR-001",
            "unsupported control flow should fail with a stable IR diagnostic");
}