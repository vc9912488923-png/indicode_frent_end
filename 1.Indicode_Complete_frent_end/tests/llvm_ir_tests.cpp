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
        "function signed_divide(left: Long, right: Long) -> Long { return left / right }\n"
        "function unsigned_remainder(left: ULong, right: ULong) -> ULong { return left % right }\n"
        "function bitwise_not(value: Int) -> Int { return ~value }\n"
        "function invert(value: Bool) -> Bool { return !value }\n"
        "function choose(value: Bool) -> Int { if value { return 1 } else { return 0 } }\n"
        "function countdown(start: Int) -> Int {\n"
        "    var current: Int = start\n"
        "    while current > 0 { current = current - 1 }\n"
        "    return current\n"
        "}\n"
        "function implicit_local() -> Int {\n"
        "    value = 2\n"
        "    value = value + 3\n"
        "    return value\n"
        "}\n"
        "function both(left: Bool, right: Bool) -> Bool { return left && right }\n"
        "function either(left: Bool, right: Bool) -> Bool { return left || right }\n"
        "function call_named(value: Int) -> Int { return add(right: 4, left: value) }\n"
        "function done(value: Int) { return }\n"
        "function invoke(value: Int) { done(value) }\n"
        "function float_sum(left: Float, right: Float) -> Float { return left + right }\n"
        "function half_sum(left: Half, right: Half) -> Half { return left + right }\n"
        "function below_float(left: Double, right: Double) -> Bool { return left < right }\n"
        "function float_literal() -> Float { return 1.25 }\n"
        "function converted(input: Int) -> Long { return to_Long(input, checked: true) }\n"
        "function announces(value: Int) { say(value) }\n"
        "function char_emoji() -> Char { return \"\\u{1F600}\" }\n"
        "function newline_char() -> Char { return \"\\n\" }\n"
        "record Point { x: Int, y: Int }\n"
        "function make_point(x: Int) -> Point { return Point(y: 2, x: x) }\n"
        "function point_x(point: Point) -> Int { return point.x }\n"
        "function first_item(values: List<Int>) -> Int { return values[0] }\n"
        "function make_items() -> List<Int> { return [1, 2, 3] }\n"
        "function first_char(text: Text) -> Char { return text[0] }\n"
        "function same_text(left: Text, right: Text) -> Bool { return left == right }\n"
        "function text_before(left: Text, right: Text) -> Bool { return left <= right }\n"
        "function char_before(left: Char, right: Char) -> Bool { return left < right }\n"
        "function preserve_tensor(input: Tensor<Float, [Batch, 3]>) -> Tensor<Float, [Batch, 3]> "
        "{ return input }\n"
        "function greeting() -> Text { return \"hello\\nworld\" }\n"
        "function range_total() -> Int {\n"
        "    var total: Int = 0\n"
        "    for index in 0..5 { total = total + index }\n"
        "    return total\n"
        "}\n"
        "function repeat_total(count: Int) -> Int {\n"
        "    var total: Int = 0\n"
        "    repeat count times { total = total + 1 }\n"
        "    return total\n"
        "}\n"
        "function list_total(values: List<Int>) -> Int {\n"
        "    var total: Int = 0\n"
        "    for item in values { total = total + item }\n"
        "    return total\n"
        "}\n"
        "function print_chars(text: Text) { for character in text { say(character) } }\n");
    require(supported.diagnostics.empty(), "scalar functions should generate IR");
    require(supported.ir.find("define i32 @add(i32 %in.") != std::string::npos &&
                supported.ir.find(" = add i32 ") != std::string::npos &&
                supported.ir.find(" = icmp ult i16 ") != std::string::npos &&
                supported.ir.find(" = sdiv i64 ") != std::string::npos &&
                supported.ir.find(" = urem i64 ") != std::string::npos &&
                supported.ir.find(" = xor i32 ") != std::string::npos &&
                supported.ir.find(" = fadd float ") != std::string::npos &&
                supported.ir.find(" = fadd half ") != std::string::npos &&
                supported.ir.find(" = fcmp olt double ") != std::string::npos &&
                supported.ir.find("indicode_text_compare") != std::string::npos &&
                supported.ir.find("define ptr @preserve_tensor(ptr ") != std::string::npos &&
                supported.ir.find("ret float 1.250000000e+00") != std::string::npos &&
                supported.ir.find("ret i32 128512") != std::string::npos &&
                supported.ir.find("ret i32 10") != std::string::npos &&
                supported.ir.find("c\"hello\\0Aworld\\00\"") != std::string::npos &&
                supported.ir.find("i64 11)") != std::string::npos &&
                supported.ir.find("declare i64 @indicode_checked_cast_Int_to_Long(i32)") != std::string::npos &&
                supported.ir.find("declare void @indicode_say_Int(i32)") != std::string::npos &&
                supported.ir.find("%rec.Point = type { i32, i32 }") != std::string::npos &&
                supported.ir.find("insertvalue %rec.Point") != std::string::npos &&
                supported.ir.find("extractvalue %rec.Point") != std::string::npos &&
                supported.ir.find("indicode_list_new_Int") != std::string::npos &&
                supported.ir.find("indicode_list_get_Int") != std::string::npos &&
                supported.ir.find("indicode_text_from_literal") != std::string::npos &&
                supported.ir.find("indicode_text_at") != std::string::npos &&
                supported.ir.find("indicode_text_compare") != std::string::npos &&
                supported.ir.find("repeat.cond.") != std::string::npos &&
                supported.ir.find("foreach.cond.") != std::string::npos &&
                supported.ir.find("indicode_list_length") != std::string::npos &&
                supported.ir.find("indicode_text_length") != std::string::npos &&
                supported.ir.find(" = xor i1 ") != std::string::npos &&
                supported.ir.find(" = icmp sgt i32 ") != std::string::npos &&
                supported.ir.find("phi i1 [ false, %") != std::string::npos &&
                supported.ir.find("phi i1 [ true, %") != std::string::npos &&
                supported.ir.find("call i32 @add(i32 ") != std::string::npos &&
                supported.ir.find("call void @done(i32 ") != std::string::npos &&
                supported.ir.find("define i32 @implicit_local()") != std::string::npos &&
                supported.ir.find("while.cond.") != std::string::npos &&
                supported.ir.find("if.then.") != std::string::npos &&
                supported.ir.find("ret i32 ") != std::string::npos,
            "IR should lower scalar operations, calls, short-circuit logic, if, and while");

    const auto global = generate(
        "answer = 1\n"
        "answer = answer + 1\n"
        "function read_answer() -> Int { return answer }\n");
    require(global.diagnostics.empty() &&
                global.ir.find("@.indi.global.") != std::string::npos &&
                global.ir.find(" = global i32", global.ir.find(" = global i32") + 1) ==
                    std::string::npos &&
                global.ir.find("define i32 @indicode_entry()") != std::string::npos &&
                global.ir.find("define i32 @read_answer()") != std::string::npos,
            "top-level assignments should share one global initialized by indicode_entry");

    const auto entry_collision = generate(
        "answer = 1\n"
        "function indicode_entry() -> Int { return answer }\n");
    require(entry_collision.diagnostics.empty() &&
                entry_collision.ir.find("define i32 @indicode.entry.init()") != std::string::npos &&
                entry_collision.ir.find("define i32 @indicode_entry()") != std::string::npos,
            "the generated global initializer should not collide with a user function");

    const auto generic = generate(
        "function identity<T>(value: T) -> T { return value }\n"
        "function sum<T: Numeric>(left: T, right: T) -> T { return left + right }\n"
        "function forwarded<T: Numeric>(value: T) -> T { return sum(value, value) }\n"
        "function first<T>(values: List<T>) -> T { return values[0] }\n"
        "function consume_range<T: Integer>(values: Range<T>) { for item in values { say(item) } }\n"
        "function recursive<T>(value: T) -> T {\n"
        "    if true { return recursive(value) } else { return value }\n"
        "}\n"
        "function generic_int() -> Int { return forwarded(identity(21)) }\n"
        "function generic_float() -> Float { return sum(1.0, 2.0) }\n"
        "function generic_list() -> Int { return first([4, 5]) }\n"
        "function generic_range() { consume_range(0..3) }\n"
        "function generic_recursive() -> Int { return recursive(7) }\n"
        "global_answer = identity(value: 42)\n");
    require(generic.diagnostics.empty() &&
                generic.ir.find("define i32 @indicode.generic.") != std::string::npos &&
                generic.ir.find("define float @indicode.generic.") != std::string::npos &&
                generic.ir.find("add float ") != std::string::npos &&
                generic.ir.find("indicode_list_get_Int") != std::string::npos &&
                generic.ir.find("foreach.cond.") != std::string::npos &&
                generic.ir.find("call i32 @indicode.generic.") != std::string::npos &&
                generic.ir.find("define i32 @indicode.generic.") != std::string::npos &&
                generic.ir.find("define i32 @indicode_entry()") != std::string::npos,
            "generic functions should specialize for scalar and collection arguments, including nested calls");
}