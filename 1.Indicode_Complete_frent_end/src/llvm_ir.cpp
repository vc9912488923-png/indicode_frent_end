#include "indicode/llvm_ir.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace indicode {
namespace {

std::optional<std::string> llvm_type(const std::string& type) {
    if (type == "Bool") return "i1";
    if (type == "Byte" || type == "U8") return "i8";
    if (type == "Int2" || type == "UInt2") return "i16";
    if (type == "Int" || type == "UInt") return "i32";
    if (type == "Long" || type == "ULong") return "i64";
    if (type == "Void") return "void";
    return std::nullopt;
}

bool is_signed_integer(const std::string& type) {
    return type == "Int2" || type == "Int" || type == "Long";
}

bool parse_integer_literal(std::string_view spelling, std::uint64_t& value) {
    std::string digits;
    digits.reserve(spelling.size());
    for (const char character : spelling) {
        if (character != '_') digits.push_back(character);
    }
    unsigned base = 10;
    std::size_t prefix_length = 0;
    if (digits.size() > 2 && digits[0] == '0') {
        if (digits[1] == 'b' || digits[1] == 'B') { base = 2; prefix_length = 2; }
        else if (digits[1] == 'o' || digits[1] == 'O') { base = 8; prefix_length = 2; }
        else if (digits[1] == 'x' || digits[1] == 'X') { base = 16; prefix_length = 2; }
    }
    const auto parsed = std::from_chars(digits.data() + prefix_length,
                                        digits.data() + digits.size(), value,
                                        static_cast<int>(base));
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size();
}

struct Binding {
    std::string type;
    std::string slot;
};

class FunctionEmitter {
public:
    using FunctionMap = std::unordered_map<std::uint64_t, const HirFunction*>;

    FunctionEmitter(const HirFunction& function, const FunctionMap& functions,
                    std::vector<Diagnostic>& diagnostics)
        : function_(function), functions_(functions), diagnostics_(diagnostics) {}

    std::string emit() {
        collect_bindings();
        if (!diagnostics_.empty()) return {};

        const auto return_type = llvm_type(function_.return_type);
        if (!return_type) {
            report("E-IR-001", "LLVM IR generation does not support return type '" +
                                     function_.return_type + "'", function_.span);
            return {};
        }

        std::ostringstream output;
        output << "define " << *return_type << " @" << function_.name << '(';
        for (std::size_t index = 0; index < function_.parameters.size(); ++index) {
            const auto& parameter = function_.parameters[index];
            const auto parameter_type = llvm_type(parameter.type);
            if (!parameter_type || parameter.type == "Void") {
                report("E-IR-001", "LLVM IR generation does not support parameter type '" +
                                         parameter.type + "'", parameter.span);
                return {};
            }
            if (index != 0) output << ", ";
            output << *parameter_type << " %in." << parameter.symbol_id;
        }
        output << ") {\nentry:\n";
        current_block_ = "entry";

        for (const auto& parameter : function_.parameters) {
            const auto& binding = bindings_.at(parameter.symbol_id);
            const auto type = *llvm_type(parameter.type);
            output << "  " << binding.slot << " = alloca " << type << '\n'
                   << "  store " << type << " %in." << parameter.symbol_id
                   << ", ptr " << binding.slot << '\n';
        }
        emit_local_allocas(output, function_.body);
        emit_statements(output, function_.body);
        if (!diagnostics_.empty()) return {};
        if (!terminated_ && function_.return_type == "Void") output << "  ret void\n";
        if (!terminated_ && function_.return_type != "Void") {
            report("E-IR-003", "function can reach its end without returning a value", function_.span);
            return {};
        }
        output << "}\n";
        return output.str();
    }

private:
    void report(std::string code, std::string message, SourceSpan span) {
        diagnostics_.emplace_back(DiagnosticSeverity::Error, std::move(code),
                                  std::move(message), span);
    }

    void collect_bindings() {
        for (const auto& parameter : function_.parameters) {
            bindings_.emplace(parameter.symbol_id,
                              Binding{parameter.type, "%slot." + std::to_string(parameter.symbol_id)});
        }
        collect_bindings(function_.body);
    }

    void collect_bindings(const std::vector<std::unique_ptr<HirStmt>>& statements) {
        for (const auto& statement : statements) {
            if (statement->kind == HirStmtKind::Variable) {
                if (!llvm_type(statement->type) || statement->type == "Void") {
                    report("E-IR-001", "LLVM IR generation does not support local type '" +
                                             statement->type + "'", statement->span);
                } else {
                    bindings_.emplace(statement->symbol_id,
                                      Binding{statement->type,
                                              "%slot." + std::to_string(statement->symbol_id)});
                }
            }
            collect_bindings(statement->body);
            collect_bindings(statement->else_body);
        }
    }

    void emit_local_allocas(std::ostringstream& output,
                           const std::vector<std::unique_ptr<HirStmt>>& statements) {
        for (const auto& statement : statements) {
            if (statement->kind == HirStmtKind::Variable) {
                const auto type = llvm_type(statement->type);
                if (type) {
                    const auto& binding = bindings_.at(statement->symbol_id);
                    output << "  " << binding.slot << " = alloca " << *type << '\n';
                }
            }
            emit_local_allocas(output, statement->body);
            emit_local_allocas(output, statement->else_body);
        }
    }

    std::string emit_expression(std::ostringstream& output, const HirExpr& expression) {
        if (expression.kind == HirExprKind::Call) return emit_call(output, expression);
        const auto type = llvm_type(expression.type);
        if (!type || expression.type == "Void") {
            report("E-IR-001", "LLVM IR generation does not support expression type '" +
                                     expression.type + "'", expression.span);
            return {};
        }
        switch (expression.kind) {
        case HirExprKind::Literal: {
            if (expression.type == "Bool") {
                if (expression.value == "true") return "true";
                if (expression.value == "false") return "false";
            } else {
                std::uint64_t value = 0;
                if (parse_integer_literal(expression.value, value)) return std::to_string(value);
            }
            report("E-IR-001", "LLVM IR generation does not support this literal", expression.span);
            return {};
        }
        case HirExprKind::Name: {
            const auto binding = bindings_.find(expression.symbol_id);
            if (binding == bindings_.end()) {
                report("E-IR-002", "no LLVM IR binding exists for this name", expression.span);
                return {};
            }
            const auto value = next_value();
            output << "  " << value << " = load " << *type << ", ptr "
                   << binding->second.slot << '\n';
            return value;
        }
        case HirExprKind::Unary: {
            if (!expression.left) {
                report("E-IR-002", "malformed unary expression in HIR", expression.span);
                return {};
            }
            const auto operand = emit_expression(output, *expression.left);
            if (expression.value == "move" || expression.value == "borrow" ||
                expression.value == "borrow_mut" || expression.value == "+") return operand;
            const auto value = next_value();
            if (expression.value == "-") {
                output << "  " << value << " = sub " << *type << " 0, " << operand << '\n';
            } else if (expression.value == "~") {
                output << "  " << value << " = xor " << *type << ' ' << operand << ", -1\n";
            } else if (expression.value == "!") {
                output << "  " << value << " = xor i1 " << operand << ", true\n";
            } else {
                report("E-IR-001", "LLVM IR generation does not support unary operator '" +
                                         expression.value + "'", expression.span);
                return {};
            }
            return value;
        }
        case HirExprKind::Binary: {
            if (!expression.left || !expression.right) {
                report("E-IR-002", "malformed binary expression in HIR", expression.span);
                return {};
            }
            if (expression.value == "&&" || expression.value == "||") {
                return emit_short_circuit(output, expression);
            }
            const auto left = emit_expression(output, *expression.left);
            const auto right = emit_expression(output, *expression.right);
            const auto value = next_value();
            std::string instruction;
            if (expression.value == "+") instruction = "add";
            else if (expression.value == "-") instruction = "sub";
            else if (expression.value == "*") instruction = "mul";
            else if (expression.value == "/") instruction = is_signed_integer(expression.left->type) ? "sdiv" : "udiv";
            else if (expression.value == "%") instruction = is_signed_integer(expression.left->type) ? "srem" : "urem";
            if (!instruction.empty()) {
                output << "  " << value << " = " << instruction << ' ' << *type << ' '
                       << left << ", " << right << '\n';
                return value;
            }
            if (expression.value == "==" || expression.value == "!=" ||
                expression.value == "<" || expression.value == "<=" ||
                expression.value == ">" || expression.value == ">=") {
                std::string predicate;
                if (expression.value == "==") predicate = "eq";
                else if (expression.value == "!=") predicate = "ne";
                else {
                    const bool signed_type = is_signed_integer(expression.left->type);
                    if (expression.value == "<") predicate = signed_type ? "slt" : "ult";
                    else if (expression.value == "<=") predicate = signed_type ? "sle" : "ule";
                    else if (expression.value == ">") predicate = signed_type ? "sgt" : "ugt";
                    else predicate = signed_type ? "sge" : "uge";
                }
                output << "  " << value << " = icmp " << predicate << ' '
                       << *llvm_type(expression.left->type) << ' ' << left << ", " << right << '\n';
                return value;
            }
            report("E-IR-001", "LLVM IR generation does not support binary operator '" +
                                     expression.value + "'", expression.span);
            return {};
        }
        default:
            report("E-IR-001", "LLVM IR generation does not support this expression", expression.span);
            return {};
        }
    }

    std::string emit_call(std::ostringstream& output, const HirExpr& expression) {
        const auto target = functions_.find(expression.symbol_id);
        if (target == functions_.end() || target->second->name != expression.symbol) {
            report("E-IR-001", "LLVM IR generation only supports direct calls to declared functions",
                   expression.span);
            return {};
        }
        const auto& function = *target->second;
        if (!function.generic_parameters.empty()) {
            report("E-IR-001", "LLVM IR generation does not yet support calls to generic functions",
                   expression.span);
            return {};
        }

        std::vector<std::string> evaluated_arguments;
        evaluated_arguments.reserve(expression.arguments.size());
        for (const auto& argument : expression.arguments) {
            evaluated_arguments.push_back(emit_expression(output, *argument));
            if (!diagnostics_.empty()) return {};
        }

        std::vector<std::string> ordered_arguments(function.parameters.size());
        std::vector<bool> assigned(function.parameters.size(), false);
        std::size_t next_positional = 0;
        for (std::size_t index = 0; index < evaluated_arguments.size(); ++index) {
            const std::string argument_name = index < expression.argument_names.size()
                ? expression.argument_names[index] : "";
            std::size_t parameter_index = function.parameters.size();
            if (argument_name.empty()) {
                while (next_positional < assigned.size() && assigned[next_positional]) {
                    ++next_positional;
                }
                if (next_positional < assigned.size()) parameter_index = next_positional++;
            } else {
                for (std::size_t candidate = 0; candidate < function.parameters.size(); ++candidate) {
                    if (function.parameters[candidate].name == argument_name) {
                        parameter_index = candidate;
                        break;
                    }
                }
            }
            if (parameter_index >= assigned.size() || assigned[parameter_index]) {
                report("E-IR-002", "call arguments do not map to the target function parameters",
                       expression.span);
                return {};
            }
            assigned[parameter_index] = true;
            ordered_arguments[parameter_index] = evaluated_arguments[index];
        }
        for (const bool parameter_assigned : assigned) {
            if (!parameter_assigned) {
                report("E-IR-002", "call is missing an argument for a target parameter", expression.span);
                return {};
            }
        }

        const auto return_type = llvm_type(function.return_type);
        if (!return_type || expression.type != function.return_type) {
            report("E-IR-001", "LLVM IR call has an unsupported return type", expression.span);
            return {};
        }
        std::string result;
        if (function.return_type != "Void") result = next_value();
        output << "  ";
        if (!result.empty()) output << result << " = ";
        output << "call " << *return_type << " @" << function.name << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0) output << ", ";
            const auto parameter_type = llvm_type(function.parameters[index].type);
            if (!parameter_type || function.parameters[index].type == "Void") {
                report("E-IR-001", "LLVM IR call has an unsupported parameter type",
                       function.parameters[index].span);
                return {};
            }
            output << *parameter_type << ' ' << ordered_arguments[index];
        }
        output << ")\n";
        return result;
    }

    std::string emit_short_circuit(std::ostringstream& output, const HirExpr& expression) {
        const auto left = emit_expression(output, *expression.left);
        if (!diagnostics_.empty()) return {};
        const auto left_block = current_block_;
        const auto block_id = block_index_++;
        const auto right_label = "logic.rhs." + std::to_string(block_id);
        const auto merge_label = "logic.end." + std::to_string(block_id);
        const bool is_and = expression.value == "&&";
        output << "  br i1 " << left << ", label %"
               << (is_and ? right_label : merge_label) << ", label %"
               << (is_and ? merge_label : right_label) << '\n';
        terminated_ = true;

        emit_label(output, right_label);
        const auto right = emit_expression(output, *expression.right);
        if (!diagnostics_.empty()) return {};
        const auto right_block = current_block_;
        output << "  br label %" << merge_label << '\n';
        terminated_ = true;

        emit_label(output, merge_label);
        const auto result = next_value();
        output << "  " << result << " = phi i1 [ " << (is_and ? "false" : "true")
               << ", %" << left_block << " ], [ " << right << ", %" << right_block << " ]\n";
        return result;
    }

    void emit_statements(std::ostringstream& output,
                         const std::vector<std::unique_ptr<HirStmt>>& statements) {
        for (const auto& statement : statements) {
            if (terminated_) return;
            switch (statement->kind) {
            case HirStmtKind::Variable:
            case HirStmtKind::Assignment: {
                if (!statement->expression) {
                    report("E-IR-002", "malformed variable statement in HIR", statement->span);
                    return;
                }
                const auto binding = bindings_.find(statement->symbol_id);
                if (binding == bindings_.end()) {
                    report("E-IR-002", "no LLVM IR storage exists for this variable", statement->span);
                    return;
                }
                const auto value = emit_expression(output, *statement->expression);
                const auto type = llvm_type(statement->type);
                if (!type) return;
                output << "  store " << *type << ' ' << value << ", ptr "
                       << binding->second.slot << '\n';
                break;
            }
            case HirStmtKind::Return:
                if (statement->expression) {
                    const auto value = emit_expression(output, *statement->expression);
                    const auto type = llvm_type(function_.return_type);
                    if (!type) return;
                    output << "  ret " << *type << ' ' << value << '\n';
                } else {
                    output << "  ret void\n";
                }
                terminated_ = true;
                break;
            case HirStmtKind::Expression:
                if (!statement->expression) {
                    report("E-IR-002", "malformed expression statement in HIR", statement->span);
                    return;
                }
                static_cast<void>(emit_expression(output, *statement->expression));
                break;
            case HirStmtKind::If:
                emit_if(output, *statement);
                break;
            case HirStmtKind::While:
                emit_while(output, *statement);
                break;
            default:
                report("E-IR-001", "LLVM IR generation does not support this statement in the initial subset",
                       statement->span);
                return;
            }
            if (!diagnostics_.empty()) return;
        }
    }

    std::string make_label(std::string_view stem) {
        return std::string(stem) + "." + std::to_string(block_index_++);
    }

    void emit_label(std::ostringstream& output, const std::string& label) {
        output << label << ":\n";
        current_block_ = label;
        terminated_ = false;
    }

    void emit_if(std::ostringstream& output, const HirStmt& statement) {
        if (!statement.expression) {
            report("E-IR-002", "malformed if condition in HIR", statement.span);
            return;
        }
        const auto condition = emit_expression(output, *statement.expression);
        if (!diagnostics_.empty()) return;
        const auto then_label = make_label("if.then");
        const auto else_label = make_label("if.else");
        const auto end_label = make_label("if.end");
        output << "  br i1 " << condition << ", label %" << then_label
               << ", label %" << else_label << '\n';
        terminated_ = true;

        emit_label(output, then_label);
        emit_statements(output, statement.body);
        const bool then_terminated = terminated_;
        if (!then_terminated) {
            output << "  br label %" << end_label << '\n';
            terminated_ = true;
        }

        emit_label(output, else_label);
        emit_statements(output, statement.else_body);
        const bool else_terminated = terminated_;
        if (!else_terminated) {
            output << "  br label %" << end_label << '\n';
            terminated_ = true;
        }

        emit_label(output, end_label);
        if (then_terminated && else_terminated) {
            output << "  unreachable\n";
            terminated_ = true;
        }
    }

    void emit_while(std::ostringstream& output, const HirStmt& statement) {
        if (!statement.expression) {
            report("E-IR-002", "malformed while condition in HIR", statement.span);
            return;
        }
        const auto condition_label = make_label("while.cond");
        const auto body_label = make_label("while.body");
        const auto end_label = make_label("while.end");
        output << "  br label %" << condition_label << '\n';
        terminated_ = true;

        emit_label(output, condition_label);
        const auto condition = emit_expression(output, *statement.expression);
        if (!diagnostics_.empty()) return;
        output << "  br i1 " << condition << ", label %" << body_label
               << ", label %" << end_label << '\n';
        terminated_ = true;

        emit_label(output, body_label);
        emit_statements(output, statement.body);
        if (!terminated_) {
            output << "  br label %" << condition_label << '\n';
            terminated_ = true;
        }
        emit_label(output, end_label);
    }

    std::string next_value() { return "%v." + std::to_string(value_index_++); }

    const HirFunction& function_;
    const FunctionMap& functions_;
    std::vector<Diagnostic>& diagnostics_;
    std::unordered_map<std::uint64_t, Binding> bindings_;
    std::size_t value_index_ = 0;
    std::size_t block_index_ = 0;
    std::string current_block_;
    bool terminated_ = false;
};

} // namespace

LlvmIrResult generate_llvm_ir(const HirProgram& program) {
    LlvmIrResult result;
    result.diagnostics = verify_hir(program);
    if (!result.diagnostics.empty()) return result;
    if (!program.records.empty() || !program.statements.empty()) {
        const SourceSpan span = !program.records.empty() ? program.records.front().span
                                                         : program.statements.front()->span;
        result.diagnostics.emplace_back(
            DiagnosticSeverity::Error, "E-IR-001",
            "LLVM IR generation does not yet support records or top-level statements", span);
        return result;
    }

    std::ostringstream output;
    output << "; Indicode textual LLVM IR\n";
    FunctionEmitter::FunctionMap functions;
    for (const auto& function : program.functions) {
        functions.emplace(function.symbol_id, &function);
    }
    for (const auto& function : program.functions) {
        if (!function.generic_parameters.empty()) {
            result.diagnostics.emplace_back(
                DiagnosticSeverity::Error, "E-IR-001",
                "LLVM IR generation does not yet support generic functions", function.span);
            break;
        }
        output << FunctionEmitter(function, functions, result.diagnostics).emit();
        if (!result.diagnostics.empty()) break;
        output << '\n';
    }
    if (result.diagnostics.empty()) result.ir = output.str();
    return result;
}

} // namespace indicode