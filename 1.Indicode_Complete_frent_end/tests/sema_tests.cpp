#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"
#include "indicode/const_eval.hpp"
#include "indicode/sema.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

indicode::Program parse_source(const indicode::SourceFile& source) {
    const auto lexed = indicode::Lexer(source).lex();
    require(lexed.diagnostics.empty(), "semantic fixture should lex cleanly");
    auto parsed = indicode::Parser(lexed.tokens).parse();
    require(parsed.diagnostics.empty(), "semantic fixture should parse cleanly");
    return std::move(parsed.program);
}

}

int main() {
    const indicode::SourceFile valid{
        "valid.indi",
        "function add(a: Int, b: Int) -> Int { return a + b }\n"
        "let fixed: Int = 3\n"
        "Int2 small = 30000\n"
            "Int2 hexLimit = 0x7fff\n"
            "UInt2 binaryLimit = 0b1111111111111111\n"
            "Long grouped = 1_000_000\n"
        "Int2 minimum = -32768\n"
        "List<Int> empty = []\n"
        "counter = add(fixed, 4)\n"
        "counter = counter + 1\n"
        "items = [1, 2, 3]\n"
        "first = items[0]\n"
        "for item in items { counter = counter + item }\n"
        "for character in \"ok\" { say(character) }\n"
        "accent = \"\xC3\xA9\"\n"
        "escaped = \"\\u{1F600}\"\n"
        "combined = \"e\\u{301}\"\n"
        "namedAnswer = add(b: fixed, a: 4)\n"
        "small = to_Int2(30000, checked: true)\n"
        "halfValue = to_Half(1.5, checked: true)\n"
        "floatValue = to_Float(0x10, checked: true)\n"
        "doubleValue = to_Double(1e40, checked: true)\n"
        "for index in 0..10 { say(index) }\n"
        "say(\"ok\")\n"
        "sameTruth = true == false\n"
    };
    auto valid_program = parse_source(valid);
    const auto valid_result = indicode::Analyzer().analyze(valid_program);
    require(valid_result.diagnostics.empty(), "valid declarations and calls should type-check");
        require(valid_program.statements[6]->expression->inferred_type == "List<Int>",
            "an empty list should receive its declared element type");
                require(valid_program.statements[7]->declared_type == "Int",
            "inferred assignment should retain its resolved static type");
                require(valid_program.statements[7]->expression->inferred_type == "Int",
            "expression should be annotated with inferred type");
                require(valid_program.statements[10]->expression->inferred_type == "Int",
                "list indexing should infer the list element type");
                require(valid_program.statements[11]->declared_type == "Int" &&
                    valid_program.statements[12]->declared_type == "Char",
                "for-each should infer list and text element types");
                require(valid_program.statements[13]->declared_type == "Char" &&
                    valid_program.statements[14]->declared_type == "Char" &&
                    valid_program.statements[15]->declared_type == "Text",
                    "string literals should infer Char/Text by Unicode scalar count");

    const indicode::SourceFile invalid{
        "invalid.indi",
        "function add(a: Int, b: Int) -> Int { return a + b }\n"
        "let fixed: Int = 3\n"
        "mixed = [1, \"two\"]\n"
        "invalid_index = mixed[false]\n"
        "for item in 5 { say(item) }\n"
        "unknown_named = add(c: 1, a: 2)\n"
        "duplicate_named = add(a: 1, a: 2)\n"
        "Int2 radixTooLarge = 0x8000\n"
        "Int2 tooLarge = 40000\n"
        "Int2 tooSmall = -32769\n"
        "fixed = 4\n"
        "count = \"wrong\"\n"
        "missing = unknown_name\n"
        "function broken(value: Int) -> Bool { return value }\n"
    };
    auto invalid_program = parse_source(invalid);
    const auto invalid_result = indicode::Analyzer().analyze(invalid_program);
        require(invalid_result.diagnostics.size() >= 8,
            "invalid lists/indexes/iterables and prior type errors should be diagnosed");
    bool saw_range_error = false;
    for (const auto& diagnostic : invalid_result.diagnostics) {
        saw_range_error = saw_range_error || diagnostic.code == "E-SEMA-022";
    }
    require(saw_range_error, "out-of-range Int2 literals should use the stable range diagnostic");
    bool saw_unknown_named = false;
    bool saw_duplicate_named = false;
    for (const auto& diagnostic : invalid_result.diagnostics) {
        saw_unknown_named = saw_unknown_named || diagnostic.code == "E-SEMA-028";
        saw_duplicate_named = saw_duplicate_named || diagnostic.code == "E-SEMA-030";
    }
    require(saw_unknown_named && saw_duplicate_named,
            "unknown and duplicate named arguments should be rejected");
    require(valid_program.statements[17]->declared_type == "Int2",
            "checked integer conversions should infer their destination type");
    require(valid_program.statements[18]->declared_type == "Half" &&
                valid_program.statements[19]->declared_type == "Float" &&
                valid_program.statements[20]->declared_type == "Double",
            "checked floating-point conversions should infer their destination types");
    require(valid_program.statements[21]->expression->inferred_type == "Range<Int>" &&
                valid_program.statements[21]->declared_type == "Int",
            "integer range loops should infer their element type");

    const indicode::SourceFile else_if_source{
        "else_if.indi",
        "function classify(value: Int) -> Int {\n"
        "  if value == 0 { return 0 } else if value == 1 { return 1 } else { return 2 }\n"
        "}\n"
    };
    auto else_if_program = parse_source(else_if_source);
    require(indicode::Analyzer().analyze(else_if_program).diagnostics.empty(),
            "nested else-if branches that all return should satisfy return-path analysis");

    const indicode::SourceFile flow_source{
        "flow.indi",
        "function returns_all(value: Bool) -> Int {\n"
        "  if value { return 1 } else { return 2 }\n"
        "  unreachable = 3\n"
        "}\n"
        "function loop_is_not_a_return(value: Bool) -> Int {\n"
        "  while value { return 1 }\n"
        "}\n"
    };
    auto flow_program = parse_source(flow_source);
    const auto flow_result = indicode::Analyzer().analyze(flow_program);
    bool saw_unreachable = false;
    bool saw_missing_loop_return = false;
    for (const auto& diagnostic : flow_result.diagnostics) {
        saw_unreachable = saw_unreachable || diagnostic.code == "E-SEMA-050";
        saw_missing_loop_return = saw_missing_loop_return || diagnostic.code == "E-SEMA-011";
    }
    require(saw_unreachable && saw_missing_loop_return,
            "control-flow analysis should reject unreachable statements and not assume loops execute");

    const indicode::SourceFile records_source{
        "records.indi",
        "record Point { x: Int, y: Int }\n"
        "function horizontal(point: Point) -> Int { return point.x }\n"
        "origin = Point(x: 0, y: 0)\n"
        "coordinate = horizontal(origin)\n"
    };
    auto records_program = parse_source(records_source);
    const auto records_result = indicode::Analyzer().analyze(records_program);
    require(records_result.diagnostics.empty(),
            "record fields, named construction, and member types should pass semantic checks");
    require(records_program.statements[0]->declared_type == "Point" &&
                records_program.statements[1]->declared_type == "Int",
            "record values and field accesses should retain their resolved types");

    const indicode::SourceFile invalid_records_source{
        "invalid_records.indi",
        "record Point { x: Int }\n"
        "point = Point(x: 1)\n"
        "missing = point.z\n"
        "bad = Point(x: \"bad\")\n"
        "one = 1\n"
        "not_a_record = one.x\n"
        "incomplete = Point()\n"
        "unchecked = to_Int2(1)\n"
        "overflow = to_Int2(40000, checked: true)\n"
        "wrong_source = to_Int2(\"bad\", checked: true)\n"
        "half_overflow = to_Half(65520, checked: true)\n"
        "float_overflow = to_Float(1e40, checked: true)\n"
        "float_literal_overflow = 1e999\n"
        "float_literal_underflow = 1e-9999\n"
        "fractional_integer = to_Int2(2.5, checked: true)\n"
        "mixed_range = 1..2.0\n"
        "same_point = point == point\n"
    };
    auto invalid_records_program = parse_source(invalid_records_source);
    const auto invalid_records_result = indicode::Analyzer().analyze(invalid_records_program);
    bool saw_field_type_error = false;
    bool saw_missing_field = false;
    bool saw_non_record_access = false;
    bool saw_invalid_conversion = false;
    bool saw_conversion_range_error = false;
    std::size_t floating_literal_range_errors = 0;
    bool saw_invalid_range = false;
    bool saw_invalid_record_comparison = false;
    std::size_t conversion_range_errors = 0;
    for (const auto& diagnostic : invalid_records_result.diagnostics) {
        saw_field_type_error = saw_field_type_error || diagnostic.code == "E-SEMA-041";
        saw_missing_field = saw_missing_field || diagnostic.code == "E-SEMA-043";
        saw_non_record_access = saw_non_record_access || diagnostic.code == "E-SEMA-042";
        saw_invalid_conversion = saw_invalid_conversion || diagnostic.code == "E-SEMA-044";
        saw_conversion_range_error = saw_conversion_range_error || diagnostic.code == "E-SEMA-022";
        if (diagnostic.code == "E-SEMA-012" &&
            (diagnostic.message.find("floating-point literal") != std::string::npos)) {
            ++floating_literal_range_errors;
        }
        saw_invalid_range = saw_invalid_range || diagnostic.code == "E-SEMA-045";
        saw_invalid_record_comparison = saw_invalid_record_comparison ||
            (diagnostic.code == "E-SEMA-016" &&
             diagnostic.message == "comparison operands have incompatible types");
        if (diagnostic.code == "E-SEMA-022") ++conversion_range_errors;
    }
    require(saw_field_type_error && saw_missing_field && saw_non_record_access &&
                saw_invalid_conversion && saw_conversion_range_error,
            "invalid records, member accesses, and integer conversions should be diagnosed");
    require(conversion_range_errors >= 4,
            "integer and floating conversion constants outside the target range should be diagnosed");
    require(floating_literal_range_errors == 2,
            "floating literals that overflow or underflow the supported range should be rejected");
    require(saw_invalid_range && saw_invalid_record_comparison,
            "range mismatches and unsupported record equality should be rejected");

    const indicode::SourceFile constants_source{
        "constants.indi",
        "folded = 1 + 2 * 3\n"
        "overflow = 2147483647 + 1\n"
        "division = 12 / 0\n"
        "converted = to_Int2(30000 + 10000, checked: true)\n"
        "void_output = say(say(1))\n"
    };
    auto constants_program = parse_source(constants_source);
    auto folded = indicode::evaluate_constant_expression(
        *constants_program.statements[0]->expression);
    require(folded.value && std::get<std::int64_t>(*folded.value) == 7,
            "constant evaluator should honor arithmetic precedence");
    std::string over_budget_source_text = "over_budget = 1";
    for (std::size_t index = 0; index < 130; ++index) {
        over_budget_source_text += " + 1";
    }
    const indicode::SourceFile over_budget_source{
        "over_budget.indi", std::move(over_budget_source_text)
    };
    auto over_budget_program = parse_source(over_budget_source);
    const auto limited = indicode::evaluate_constant_expression(
        *over_budget_program.statements[0]->expression);
    require(limited.error == indicode::ConstantEvaluationError::StepLimit,
            "constant evaluator should enforce its deterministic step budget");
    const auto division_result = indicode::evaluate_constant_expression(
        *constants_program.statements[2]->expression);
    require(division_result.error == indicode::ConstantEvaluationError::DivisionByZero,
            "constant evaluator should distinguish division by zero");
    const auto constants_result = indicode::Analyzer().analyze(constants_program);
    bool saw_integer_overflow = false;
    bool saw_division_by_zero = false;
    bool saw_constant_conversion_range = false;
    for (const auto& diagnostic : constants_result.diagnostics) {
        saw_integer_overflow = saw_integer_overflow || diagnostic.code == "E-SEMA-022";
        saw_division_by_zero = saw_division_by_zero ||
            (diagnostic.code == "E-SEMA-051" &&
             diagnostic.message == "constant expression divides by zero");
        saw_constant_conversion_range = saw_constant_conversion_range ||
            (diagnostic.code == "E-SEMA-022" &&
             diagnostic.message == "integer literal is outside the conversion target range");
    }
    require(saw_integer_overflow && saw_division_by_zero && saw_constant_conversion_range,
            "semantic analysis should reject overflowing and zero-dividing constant expressions");
    require(std::any_of(constants_result.diagnostics.begin(),
                        constants_result.diagnostics.end(),
                        [](const indicode::Diagnostic& diagnostic) {
                            return diagnostic.code == "E-SEMA-017";
                        }),
            "the output builtin should reject a Void expression as its value");
    const indicode::SourceFile short_circuit_source{
        "short_circuit.indi",
        "result = false && (1 / 0 == 0)\n"
    };
    auto short_circuit_program = parse_source(short_circuit_source);
    const auto short_circuit = indicode::evaluate_constant_expression(
        *short_circuit_program.statements[0]->expression);
    require(short_circuit.value && std::get<bool>(*short_circuit.value) == false,
            "constant Boolean operators should short-circuit their unused operand");
    const indicode::SourceFile integer_boundaries_source{
        "integer_boundaries.indi",
        "minimum = -9223372036854775808\n"
        "overflow = 9223372036854775807 + 1\n"
    };
    auto integer_boundaries_program = parse_source(integer_boundaries_source);
    const auto minimum = indicode::evaluate_constant_expression(
        *integer_boundaries_program.statements[0]->expression);
    const auto signed_overflow = indicode::evaluate_constant_expression(
        *integer_boundaries_program.statements[1]->expression);
    require(minimum.value &&
                std::get<std::int64_t>(*minimum.value) == std::numeric_limits<std::int64_t>::min() &&
                signed_overflow.error == indicode::ConstantEvaluationError::Overflow,
            "constant evaluator should handle the signed minimum and detect signed overflow");

    const indicode::SourceFile duplicate_source{
        "duplicate.indi",
        "let item: Int = 1\nvar item = 2\n"
    };
    auto duplicate_program = parse_source(duplicate_source);
    const auto duplicate_result = indicode::Analyzer().analyze(duplicate_program);
    require(duplicate_result.diagnostics.size() == 1 &&
                duplicate_result.diagnostics[0].code == "E-SEMA-004" &&
                duplicate_result.diagnostics[0].notes.size() == 1 &&
                duplicate_result.diagnostics[0].notes[0].span.start.line == 1,
            "duplicate binding diagnostics should point to the earlier declaration");

    const indicode::SourceFile namespace_source{
        "namespaces.indi",
        "record Item { value: Int }\n"
        "function identity(value: Item) -> Item { return value }\n"
        "Item = Item(value: 1)\n"
        "function identity(value: Int) -> Int { return value }\n"
    };
    auto namespace_program = parse_source(namespace_source);
    const auto namespace_result = indicode::Analyzer().analyze(namespace_program);
    require(namespace_result.diagnostics.size() == 1 &&
                namespace_result.diagnostics[0].code == "E-SEMA-001" &&
                namespace_result.diagnostics[0].notes.size() == 1 &&
                namespace_result.diagnostics[0].notes[0].span.start.line == 2 &&
                namespace_program.statements[0]->declared_type == "Item",
            "type and value namespaces should remain separate while duplicate functions cite the prior declaration");

    const indicode::SourceFile void_value_source{
        "void_value.indi",
        "Void invalid_value = say(\"no value\")\n"
        "function invalid_parameter(value: Void) {}\n"
    };
    auto void_value_program = parse_source(void_value_source);
    const auto void_value_result = indicode::Analyzer().analyze(void_value_program);
    require(void_value_result.diagnostics.size() == 2 &&
                std::all_of(void_value_result.diagnostics.begin(),
                            void_value_result.diagnostics.end(),
                            [](const indicode::Diagnostic& diagnostic) {
                                return diagnostic.code == "E-SEMA-002";
                            }),
            "Void must not be used as a variable or parameter type");

    const indicode::SourceFile tensor_source{
        "tensor.indi",
        "function preserve(input: Tensor<Float, [Batch, 3]>) -> Tensor<Float, [Batch, 3]> "
        "{ return input }\n"
        "function mismatch(input: Tensor<Float, [Batch, 3]>) -> Tensor<Float, [Batch, 4]> "
        "{ return input }\n"
    };
    auto tensor_program = parse_source(tensor_source);
    const auto tensor_result = indicode::Analyzer().analyze(tensor_program);
    require(tensor_result.diagnostics.size() == 1 &&
                tensor_result.diagnostics[0].code == "E-SEMA-008",
            "tensor return checking should reject statically incompatible dimensions");

    const indicode::SourceFile tensor_mismatches{
        "tensor_mismatches.indi",
        "function rank(input: Tensor<Float, [Batch, 3]>) -> Tensor<Float, [Batch]> "
        "{ return input }\n"
        "function element(input: Tensor<Float, [4]>) -> Tensor<Int, [4]> { return input }\n"
        "function symbol(input: Tensor<Float, [Batch, 3]>) -> Tensor<Float, [Other, 3]> "
        "{ return input }\n"
    };
    auto tensor_mismatch_program = parse_source(tensor_mismatches);
    const auto tensor_mismatch_result =
        indicode::Analyzer().analyze(tensor_mismatch_program);
    require(tensor_mismatch_result.diagnostics.size() == 3 &&
                std::all_of(tensor_mismatch_result.diagnostics.begin(),
                            tensor_mismatch_result.diagnostics.end(),
                            [](const indicode::Diagnostic& diagnostic) {
                                return diagnostic.code == "E-SEMA-008";
                            }),
            "tensor checking should reject rank, element-type, and symbolic-dimension mismatches");

    std::cout << "Semantic tests passed\n";
}