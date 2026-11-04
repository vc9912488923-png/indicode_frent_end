#include "indicode/hir.hpp"
#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"
#include "indicode/sema.hpp"

#include <algorithm>
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
        "hir.indi",
        "function add(a: Int, b: Int) -> Int { return a + b }\n"
        "List<Int> empty = []\n"
        "let fixed: Int = 1\n"
        "items = [20, 22]\n"
        "answer = add(b: items[1], a: items[0])\n"
        "for item in items { say(item) }\n"
    };
    const auto lexed = indicode::Lexer(source).lex();
    require(lexed.diagnostics.empty(), "HIR fixture should lex");
    auto parsed = indicode::Parser(lexed.tokens).parse();
    require(parsed.diagnostics.empty(), "HIR fixture should parse");
    const auto semantic = indicode::Analyzer().analyze(parsed.program);
    require(semantic.diagnostics.empty(), "HIR fixture should pass semantic checks");

    auto hir = indicode::lower_to_hir(parsed.program);
    require(hir.functions.size() == 1 && hir.statements.size() == 5,
            "lowering should preserve function and top-level statement structure");
    require(indicode::verify_hir(hir).empty(), "lowered typed HIR should verify");
    auto void_say_argument_hir = indicode::lower_to_hir(parsed.program);
    auto nested_say = std::make_unique<indicode::HirExpr>();
    nested_say->kind = indicode::HirExprKind::Call;
    nested_say->type = "Void";
    nested_say->symbol = "say";
    nested_say->symbol_id = 0x200001ULL;
    nested_say->argument_names = {""};
    auto nested_argument = std::make_unique<indicode::HirExpr>();
    nested_argument->kind = indicode::HirExprKind::Literal;
    nested_argument->type = "Int";
    nested_argument->value = "1";
    nested_say->arguments.push_back(std::move(nested_argument));
    void_say_argument_hir.statements[4]->body[0]->expression->arguments[0] =
        std::move(nested_say);
    const auto void_say_diagnostics = indicode::verify_hir(void_say_argument_hir);
    require(std::any_of(void_say_diagnostics.begin(), void_say_diagnostics.end(),
                        [](const indicode::Diagnostic& diagnostic) {
                            return diagnostic.code == "E-HIR-010";
                        }),
            "HIR verifier should reject Void as the value passed to say");
    require(hir.functions[0].symbol_id != 0 &&
                hir.functions[0].return_type_symbol_id != 0 &&
                hir.functions[0].parameters[0].type_symbol_id != 0 &&
                hir.statements[3]->expression->symbol_id == hir.functions[0].symbol_id,
            "functions, call references, parameter types, and return types should have stable identities");
    auto invalid_bool_equality_hir = indicode::lower_to_hir(parsed.program);
    auto bool_equality = std::make_unique<indicode::HirExpr>();
    bool_equality->kind = indicode::HirExprKind::Binary;
    bool_equality->type = "Bool";
    bool_equality->value = "==";
    bool_equality->left = std::make_unique<indicode::HirExpr>();
    bool_equality->left->kind = indicode::HirExprKind::Literal;
    bool_equality->left->type = "Bool";
    bool_equality->left->value = "true";
    bool_equality->right = std::make_unique<indicode::HirExpr>();
    bool_equality->right->kind = indicode::HirExprKind::Literal;
    bool_equality->right->type = "Bool";
    bool_equality->right->value = "false";
    invalid_bool_equality_hir.statements.push_back(std::make_unique<indicode::HirStmt>());
    invalid_bool_equality_hir.statements.back()->kind = indicode::HirStmtKind::Expression;
    invalid_bool_equality_hir.statements.back()->expression = std::move(bool_equality);
    require(indicode::verify_hir(invalid_bool_equality_hir).empty(),
            "HIR equality rules should agree with semantic typing for Bool values");
    const auto printed = indicode::print_hir(hir);
    require(printed.find("func @add") != std::string::npos &&
                printed.find("call @add") != std::string::npos &&
                printed.find("b: ") != std::string::npos &&
                printed.find("list<List<Int>>") != std::string::npos &&
                printed.find("for %item#") != std::string::npos &&
                printed.find("func @add#") != std::string::npos &&
                printed.find("%a#") != std::string::npos,
            "typed HIR printer should show stable symbol IDs with functions, collections, loops, and types");

    auto missing_return_hir = indicode::lower_to_hir(parsed.program);
    missing_return_hir.functions[0].body.clear();
    const auto missing_return_diagnostics = indicode::verify_hir(missing_return_hir);
    require(std::any_of(missing_return_diagnostics.begin(), missing_return_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-026"; }),
            "HIR verifier should independently reject a non-Void function without complete return paths");

    auto duplicate_parameter_hir = indicode::lower_to_hir(parsed.program);
    duplicate_parameter_hir.functions[0].parameters[1].name =
        duplicate_parameter_hir.functions[0].parameters[0].name;
    const auto duplicate_parameter_diagnostics = indicode::verify_hir(duplicate_parameter_hir);
    require(std::any_of(duplicate_parameter_diagnostics.begin(), duplicate_parameter_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-030"; }),
            "HIR verifier should reject duplicate parameter names");

    auto misaligned_call_hir = indicode::lower_to_hir(parsed.program);
    misaligned_call_hir.statements[3]->expression->argument_names.clear();
    const auto misaligned_call_diagnostics = indicode::verify_hir(misaligned_call_hir);
    require(std::any_of(misaligned_call_diagnostics.begin(), misaligned_call_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-031"; }),
            "HIR verifier should reject call metadata that does not align with arguments");

    auto incorrect_function_identity_hir = indicode::lower_to_hir(parsed.program);
    incorrect_function_identity_hir.statements[3]->expression->symbol_id++;
    const auto incorrect_function_identity_diagnostics =
        indicode::verify_hir(incorrect_function_identity_hir);
    require(std::any_of(incorrect_function_identity_diagnostics.begin(),
                        incorrect_function_identity_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-042"; }),
            "HIR verifier should reject function calls with mismatched symbol identities");

    auto top_level_return_hir = indicode::lower_to_hir(parsed.program);
    auto top_level_return = std::make_unique<indicode::HirStmt>();
    top_level_return->kind = indicode::HirStmtKind::Return;
    top_level_return_hir.statements.push_back(std::move(top_level_return));
    const auto top_level_return_diagnostics = indicode::verify_hir(top_level_return_hir);
    require(std::any_of(top_level_return_diagnostics.begin(), top_level_return_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-032"; }),
            "HIR verifier should reject return statements outside functions");

    auto unknown_name_hir = indicode::lower_to_hir(parsed.program);
    unknown_name_hir.statements[3]->expression->arguments[0]->left->symbol = "missing";
    const auto unknown_name_diagnostics = indicode::verify_hir(unknown_name_hir);
    require(std::any_of(unknown_name_diagnostics.begin(), unknown_name_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-006"; }),
            "HIR verifier should reject names that are not bound in the current lexical scope");

    auto immutable_assignment_hir = indicode::lower_to_hir(parsed.program);
    auto immutable_assignment = std::make_unique<indicode::HirStmt>();
    immutable_assignment->kind = indicode::HirStmtKind::Assignment;
    immutable_assignment->name = "fixed";
    immutable_assignment->type = "Int";
    immutable_assignment->symbol_id = immutable_assignment_hir.statements[1]->symbol_id;
    immutable_assignment->expression = std::make_unique<indicode::HirExpr>();
    immutable_assignment->expression->kind = indicode::HirExprKind::Literal;
    immutable_assignment->expression->type = "Int";
    immutable_assignment->expression->value = "2";
    immutable_assignment_hir.statements.push_back(std::move(immutable_assignment));
    const auto immutable_assignment_diagnostics = indicode::verify_hir(immutable_assignment_hir);
    require(std::any_of(immutable_assignment_diagnostics.begin(), immutable_assignment_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-035"; }),
            "HIR verifier should reject assignment to an immutable binding");

    auto out_of_scope_loop_name_hir = indicode::lower_to_hir(parsed.program);
    auto out_of_scope_name = std::make_unique<indicode::HirStmt>();
    out_of_scope_name->kind = indicode::HirStmtKind::Expression;
    out_of_scope_name->expression = std::make_unique<indicode::HirExpr>();
    out_of_scope_name->expression->kind = indicode::HirExprKind::Name;
    out_of_scope_name->expression->type = "Int";
    out_of_scope_name->expression->symbol = "item";
    out_of_scope_loop_name_hir.statements.push_back(std::move(out_of_scope_name));
    const auto out_of_scope_diagnostics = indicode::verify_hir(out_of_scope_loop_name_hir);
    require(std::any_of(out_of_scope_diagnostics.begin(), out_of_scope_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-006"; }),
            "for-each bindings should not escape their lexical scope in HIR");

    hir.statements[0]->expression->type = "";
    const auto invalid = indicode::verify_hir(hir);
    require(!invalid.empty() && invalid[0].code == "E-HIR-004",
            "verifier should reject expressions without a valid type annotation");

    const indicode::SourceFile records_source{
        "records.indi",
        "record Point { x: Int, y: Int }\n"
        "origin = Point(x: 20, y: 22)\n"
        "answer = origin.x\n"
        "small = to_Int2(20, checked: true)\n"
        "weight = to_Float(0.5, checked: true)\n"
        "for index in 1..4 { say(index) }\n"
    };
    const auto records_lexed = indicode::Lexer(records_source).lex();
    auto records_parsed = indicode::Parser(records_lexed.tokens).parse();
    require(records_lexed.diagnostics.empty() && records_parsed.diagnostics.empty(),
            "record HIR fixture should lex and parse");
    require(indicode::Analyzer().analyze(records_parsed.program).diagnostics.empty(),
            "record HIR fixture should pass semantic checks");
    const auto records_hir = indicode::lower_to_hir(records_parsed.program);
    require(records_hir.records.size() == 1 && indicode::verify_hir(records_hir).empty(),
            "typed HIR should preserve and verify record declarations and access");
    require(records_hir.records[0].symbol_id != 0 &&
                records_hir.records[0].fields[0].type_symbol_id != 0 &&
                records_hir.records[0].fields[0].symbol_id != 0 &&
                records_hir.statements[0]->expression->symbol_id ==
                    records_hir.records[0].symbol_id &&
                records_hir.statements[1]->expression->symbol_id ==
                    records_hir.records[0].fields[0].symbol_id,
            "record types, fields, constructor and member references should retain stable identities");
    auto incorrect_record_identity_hir = indicode::lower_to_hir(records_parsed.program);
    incorrect_record_identity_hir.statements[0]->expression->symbol_id++;
    const auto incorrect_record_identity_diagnostics =
        indicode::verify_hir(incorrect_record_identity_hir);
    require(std::any_of(incorrect_record_identity_diagnostics.begin(),
                        incorrect_record_identity_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-041"; }),
            "HIR verifier should reject record constructors with mismatched symbol identities");
    auto incorrect_field_identity_hir = indicode::lower_to_hir(records_parsed.program);
    incorrect_field_identity_hir.statements[1]->expression->symbol_id++;
    const auto incorrect_field_identity_diagnostics =
        indicode::verify_hir(incorrect_field_identity_hir);
    require(std::any_of(incorrect_field_identity_diagnostics.begin(),
                        incorrect_field_identity_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-046"; }),
            "HIR verifier should reject member references with mismatched field identities");
    const auto records_printed = indicode::print_hir(records_hir);
    require(records_printed.find("record @Point") != std::string::npos &&
                records_printed.find(".x : Int") != std::string::npos &&
                records_printed.find("call @to_Int2") != std::string::npos &&
                records_printed.find("call @to_Float") != std::string::npos &&
                records_printed.find("range 1:Int .. 4:Int : Range<Int>") != std::string::npos,
            "typed HIR printer should show records, member access, conversions, and ranges");

    const indicode::SourceFile tensor_source{
        "tensor.indi",
        "function preserve(input: Tensor<Float, [Batch, 3]>) -> Tensor<Float, [Batch, 3]> "
        "{ return input }\n"
    };
    const auto tensor_lexed = indicode::Lexer(tensor_source).lex();
    auto tensor_parsed = indicode::Parser(tensor_lexed.tokens).parse();
    require(tensor_lexed.diagnostics.empty() && tensor_parsed.diagnostics.empty() &&
                indicode::Analyzer().analyze(tensor_parsed.program).diagnostics.empty(),
            "tensor HIR fixture should parse and pass semantic checks");
    const auto tensor_hir = indicode::lower_to_hir(tensor_parsed.program);
    require(indicode::verify_hir(tensor_hir).empty(),
            "typed HIR should preserve and verify tensor shape annotations");

    const indicode::SourceFile else_if_source{
        "else_if.indi",
        "function classify(value: Int) -> Int {\n"
        "  if value == 0 { return 0 } else if value == 1 { return 1 } else { return 2 }\n"
        "}\n"
    };
    const auto else_if_lexed = indicode::Lexer(else_if_source).lex();
    auto else_if_parsed = indicode::Parser(else_if_lexed.tokens).parse();
    require(else_if_lexed.diagnostics.empty() && else_if_parsed.diagnostics.empty() &&
                indicode::Analyzer().analyze(else_if_parsed.program).diagnostics.empty(),
            "else-if chain should pass parsing and semantic analysis");
    const auto else_if_hir = indicode::lower_to_hir(else_if_parsed.program);
    require(indicode::verify_hir(else_if_hir).empty(),
            "else-if chain should lower to valid HIR with complete return coverage");

    const indicode::SourceFile symbols_source{
        "symbols.indi",
        "outer = 1\n"
        "function read() -> Int {\n"
        "  if true { let outer: Int = 2; say(outer) }\n"
        "  return outer\n"
        "}\n"
    };
    const auto symbols_lexed = indicode::Lexer(symbols_source).lex();
    auto symbols_parsed = indicode::Parser(symbols_lexed.tokens).parse();
    require(symbols_lexed.diagnostics.empty() && symbols_parsed.diagnostics.empty() &&
                indicode::Analyzer().analyze(symbols_parsed.program).diagnostics.empty(),
            "global lookup and local shadowing should resolve during semantic analysis");
    const auto& shadowed_name = *symbols_parsed.program.functions[0].body[0]->body[1]
                                     ->expression->arguments[0];
    const auto& global_name = *symbols_parsed.program.functions[0].body[1]->expression;
    require(shadowed_name.resolved_symbol_id != 0 && global_name.resolved_symbol_id != 0 &&
                shadowed_name.resolved_symbol_id != global_name.resolved_symbol_id,
            "shadowed and global references should retain distinct stable symbol identities");
    const auto symbols_hir = indicode::lower_to_hir(symbols_parsed.program);
    require(indicode::verify_hir(symbols_hir).empty(),
            "HIR verifier should resolve globals and shadowed locals by symbol identity");

    auto wrong_shadow_identity_hir = indicode::lower_to_hir(symbols_parsed.program);
    wrong_shadow_identity_hir.functions[0].body[0]->body[1]
        ->expression->arguments[0]->symbol_id = global_name.resolved_symbol_id;
    const auto wrong_identity_diagnostics = indicode::verify_hir(wrong_shadow_identity_hir);
    require(std::any_of(wrong_identity_diagnostics.begin(), wrong_identity_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-033"; }),
            "HIR verifier should reject a reference to the wrong shadowed symbol");

    const indicode::SourceFile generic_source{
        "generic_hir.indi",
        "function add<T: Numeric>(left: T, right: T) -> T { return left + right }\n"
        "function forwarded<T: Numeric>(value: T) -> T { return add(value, value) }\n"
        "answer = forwarded(20)\n"
    };
    const auto generic_lexed = indicode::Lexer(generic_source).lex();
    auto generic_parsed = indicode::Parser(generic_lexed.tokens).parse();
    require(generic_lexed.diagnostics.empty() && generic_parsed.diagnostics.empty() &&
                indicode::Analyzer().analyze(generic_parsed.program).diagnostics.empty(),
            "generic function should parse and pass semantic inference");
    const auto generic_hir = indicode::lower_to_hir(generic_parsed.program);
    require(generic_hir.functions[0].generic_parameters.size() == 1 &&
                indicode::verify_hir(generic_hir).empty(),
            "typed HIR should preserve and independently verify generic parameters and calls");
    const auto generic_hir_json = indicode::serialize_hir_json(generic_hir);
    require(generic_hir_json.find("\"schemaVersion\":2") != std::string::npos &&
                generic_hir_json.find("\"constraint\":\"Numeric\"") != std::string::npos,
            "typed HIR JSON should preserve generic constraints in schema 2");

    const indicode::SourceFile ownership_source{
        "ownership_hir.indi",
        "function consume(value: move List<Int>) {}\n"
        "function observe(value: borrow List<Int>) {}\n"
        "function run() { var items = [1]; observe(borrow items); consume(move items) }\n"
        "function announce() effects(io) { say(\"hello\") }\n"
    };
    const auto ownership_lexed = indicode::Lexer(ownership_source).lex();
    auto ownership_parsed = indicode::Parser(ownership_lexed.tokens).parse();
    require(ownership_lexed.diagnostics.empty() && ownership_parsed.diagnostics.empty() &&
                indicode::Analyzer().analyze(ownership_parsed.program).diagnostics.empty(),
            "valid ownership source should pass parsing and semantic analysis");
    const auto ownership_hir = indicode::lower_to_hir(ownership_parsed.program);
    require(ownership_hir.functions[0].parameters[0].ownership_mode ==
                indicode::OwnershipMode::Move && ownership_hir.functions[3].has_effect_annotation &&
                ownership_hir.functions[3].inferred_effects == std::vector<std::string>{"io"} &&
                indicode::verify_hir(ownership_hir).empty(),
            "typed HIR should preserve and independently verify ownership modes and effects");
    const auto ownership_hir_json = indicode::serialize_hir_json(ownership_hir);
    require(ownership_hir_json.find("\"ownershipMode\":\"move\"") != std::string::npos &&
                ownership_hir_json.find("\"inferredEffects\":[\"io\"]") != std::string::npos,
            "typed HIR JSON should serialize ownership modes and inferred effects");
    auto tampered_effect_hir = indicode::lower_to_hir(ownership_parsed.program);
    tampered_effect_hir.functions[3].inferred_effects.clear();
    const auto tampered_effect_diagnostics = indicode::verify_hir(tampered_effect_hir);
    require(std::any_of(tampered_effect_diagnostics.begin(), tampered_effect_diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.code == "E-HIR-055"; }),
            "HIR verifier should reject a serialized effect summary that disagrees with the body");

    std::cout << "HIR tests passed\n";
}