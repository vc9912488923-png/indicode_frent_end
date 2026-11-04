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
    FunctionEmitter(const HirFunction& function, std::vector<Diagnostic>& diagnostics)
        : function_(function), diagnostics_(diagnostics) {}

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
        for (const auto& statement : function_.body) {
            if (statement->kind != HirStmtKind::Variable) continue;
            if (!llvm_type(statement->type) || statement->type == "Void") {
                report("E-IR-001", "LLVM IR generation does not support local type '" +
                                         statement->type + "'", statement->span);
                continue;
            }
            bindings_.emplace(statement->symbol_id,
                              Binding{statement->type, "%slot." + std::to_string(statement->symbol_id)});
        }
    }

    void emit_local_allocas(std::ostringstream& output,
                           const std::vector<std::unique_ptr<HirStmt>>& statements) {
        for (const auto& statement : statements) {
            if (statement->kind != HirStmtKind::Variable) continue;
            const auto type = llvm_type(statement->type);
            if (!type) continue;
            const auto& binding = bindings_.at(statement->symbol_id);
            output << "  " << binding.slot << " = alloca " << *type << '\n';
        }
    }

    std::string emit_expression(std::ostringstream& output, const HirExpr& expression) {
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
                report("E-IR-001", "short-circuit Boolean operators are not in the initial LLVM IR subset",
                       expression.span);
                return {};
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

    void emit_statements(std::ostringstream& output,
                         const std::vector<std::unique_ptr<HirStmt>>& statements) {
        for (const auto& statement : statements) {
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
            default:
                report("E-IR-001", "LLVM IR generation does not support this statement in the initial subset",
                       statement->span);
                return;
            }
            if (!diagnostics_.empty()) return;
        }
    }

    std::string next_value() { return "%v." + std::to_string(value_index_++); }

    const HirFunction& function_;
    std::vector<Diagnostic>& diagnostics_;
    std::unordered_map<std::uint64_t, Binding> bindings_;
    std::size_t value_index_ = 0;
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
    for (const auto& function : program.functions) {
        if (!function.generic_parameters.empty()) {
            result.diagnostics.emplace_back(
                DiagnosticSeverity::Error, "E-IR-001",
                "LLVM IR generation does not yet support generic functions", function.span);
            break;
        }
        output << FunctionEmitter(function, result.diagnostics).emit();
        if (!result.diagnostics.empty()) break;
        output << '\n';
    }
    if (result.diagnostics.empty()) result.ir = output.str();
    return result;
}

} // namespace indicode