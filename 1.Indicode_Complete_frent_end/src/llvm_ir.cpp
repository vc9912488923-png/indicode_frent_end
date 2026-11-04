#include "indicode/llvm_ir.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace indicode {
namespace {

using RecordMap = std::unordered_map<std::string, const HirRecord*>;

std::string generic_argument(const std::string& type, std::string_view prefix) {
    if (!type.starts_with(prefix) || type.size() <= prefix.size() || type.back() != '>') return {};
    return type.substr(prefix.size(), type.size() - prefix.size() - 1);
}

std::optional<std::string> llvm_type(const std::string& type, const RecordMap& records) {
    if (type == "Bool") return "i1";
    if (type == "Byte" || type == "U8") return "i8";
    if (type == "Int2" || type == "UInt2") return "i16";
    if (type == "Int" || type == "UInt") return "i32";
    if (type == "Long" || type == "ULong") return "i64";
    if (type == "Half") return "half";
    if (type == "Float") return "float";
    if (type == "Double") return "double";
    if (type == "Char") return "i32";
    if (type == "Text" || !generic_argument(type, "List<").empty() || type.starts_with("Tensor<")) {
        return "ptr";
    }
    if (const auto range_element = generic_argument(type, "Range<"); !range_element.empty()) {
        const auto element_type = llvm_type(range_element, records);
        if (element_type && *element_type != "void") return "{ " + *element_type + ", " + *element_type + " }";
    }
    if (records.contains(type)) return "%rec." + type;
    if (type == "Void") return "void";
    return std::nullopt;
}

bool is_signed_integer(const std::string& type) {
    return type == "Int2" || type == "Int" || type == "Long";
}

bool is_floating_type(const std::string& type) {
    return type == "Half" || type == "Float" || type == "Double";
}

std::string type_key(const std::string& type) {
    std::string key;
    key.reserve(type.size());
    for (const unsigned char character : type) {
        key.push_back(std::isalnum(character) ? static_cast<char>(character) : '_');
    }
    return key;
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

bool format_float_literal(const std::string& spelling, const std::string& type,
                          std::string& result) {
    double value = 0;
    const auto parsed = std::from_chars(spelling.data(), spelling.data() + spelling.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != spelling.data() + spelling.size()) return false;
    std::ostringstream formatted;
    if (type == "Float" || type == "Half") {
        formatted << std::scientific << std::setprecision(9) << static_cast<float>(value);
    } else {
        formatted << std::scientific << std::setprecision(17) << value;
    }
    result = formatted.str();
    return true;
}

bool decode_string_literal(std::string_view spelling, std::vector<std::uint32_t>& scalars) {
    if (spelling.size() < 2 || spelling.front() != '"' || spelling.back() != '"') return false;

    for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
        const auto first = static_cast<unsigned char>(spelling[index]);
        if (first == '\\') {
            if (++index + 1 >= spelling.size()) return false;
            const char escape = spelling[index];
            switch (escape) {
            case '"': scalars.push_back('"'); break;
            case '\\': scalars.push_back('\\'); break;
            case 'n': scalars.push_back('\n'); break;
            case 'r': scalars.push_back('\r'); break;
            case 't': scalars.push_back('\t'); break;
            case '0': scalars.push_back('\0'); break;
            case 'u': {
                if (index + 1 >= spelling.size() - 1 || spelling[index + 1] != '{') return false;
                index += 2;
                std::uint32_t scalar = 0;
                bool has_digits = false;
                while (index < spelling.size() - 1 && spelling[index] != '}') {
                    const char character = spelling[index];
                    unsigned digit = 0;
                    if (character >= '0' && character <= '9') digit = static_cast<unsigned>(character - '0');
                    else if (character >= 'a' && character <= 'f') digit = static_cast<unsigned>(character - 'a' + 10);
                    else if (character >= 'A' && character <= 'F') digit = static_cast<unsigned>(character - 'A' + 10);
                    else return false;
                    if (scalar > (0x10ffffU - digit) / 16U) return false;
                    scalar = scalar * 16U + digit;
                    has_digits = true;
                    ++index;
                }
                if (!has_digits || index >= spelling.size() - 1 ||
                    (scalar >= 0xd800U && scalar <= 0xdfffU)) {
                    return false;
                }
                scalars.push_back(scalar);
                break;
            }
            default: return false;
            }
            continue;
        }

        std::size_t length = 1;
        std::uint32_t scalar = first;
        if ((first & 0xe0U) == 0xc0U) {
            length = 2;
            scalar = first & 0x1fU;
        } else if ((first & 0xf0U) == 0xe0U) {
            length = 3;
            scalar = first & 0x0fU;
        } else if ((first & 0xf8U) == 0xf0U) {
            length = 4;
            scalar = first & 0x07U;
        } else if (first >= 0x80U) {
            return false;
        }
        if (index + length > spelling.size() - 1) return false;
        for (std::size_t continuation = 1; continuation < length; ++continuation) {
            const auto byte = static_cast<unsigned char>(spelling[index + continuation]);
            if ((byte & 0xc0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (byte & 0x3fU);
        }
        if ((length == 2 && scalar < 0x80U) || (length == 3 && scalar < 0x800U) ||
            (length == 4 && scalar < 0x10000U) || scalar > 0x10ffffU ||
            (scalar >= 0xd800U && scalar <= 0xdfffU)) {
            return false;
        }
        scalars.push_back(scalar);
        index += length - 1;
    }
    return true;
}

void append_utf8(std::string& output, std::uint32_t scalar) {
    if (scalar <= 0x7fU) {
        output.push_back(static_cast<char>(scalar));
    } else if (scalar <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (scalar >> 6U)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    } else if (scalar <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (scalar >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (scalar >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3fU)));
    }
}

struct Binding {
    std::string type;
    std::string slot;
};

class FunctionEmitter {
public:
    using FunctionMap = std::unordered_map<std::uint64_t, const HirFunction*>;
    using BindingMap = std::unordered_map<std::uint64_t, Binding>;

        FunctionEmitter(const HirFunction& function, const FunctionMap& functions,
                        const RecordMap& records,
                    const BindingMap& global_bindings,
                                        std::set<std::string>& declarations,
                                        std::vector<std::string>& globals, std::size_t& global_index,
                    std::vector<Diagnostic>& diagnostics)
                : function_(function), functions_(functions), records_(records),
                    global_bindings_(global_bindings), declarations_(declarations),
                    globals_(globals), global_index_(global_index), diagnostics_(diagnostics) {}

    std::string emit() {
        collect_bindings();
        if (!diagnostics_.empty()) return {};

        const auto return_type = llvm_type(function_.return_type, records_);
        if (!return_type) {
            report("E-IR-001", "LLVM IR generation does not support return type '" +
                                     function_.return_type + "'", function_.span);
            return {};
        }

        std::ostringstream output;
        output << "define " << *return_type << " @" << function_.name << '(';
        for (std::size_t index = 0; index < function_.parameters.size(); ++index) {
            const auto& parameter = function_.parameters[index];
            const auto parameter_type = llvm_type(parameter.type, records_);
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
            const auto type = *llvm_type(parameter.type, records_);
            output << "  " << binding.slot << " = alloca " << type << '\n'
                   << "  store " << type << " %in." << parameter.symbol_id
                   << ", ptr " << binding.slot << '\n';
            allocated_slots_.insert(parameter.symbol_id);
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
        bindings_ = global_bindings_;
        for (const auto& parameter : function_.parameters) {
            bindings_.emplace(parameter.symbol_id,
                              Binding{parameter.type, "%slot." + std::to_string(parameter.symbol_id)});
        }
        collect_bindings(function_.body);
    }

    void collect_bindings(const std::vector<std::unique_ptr<HirStmt>>& statements) {
        for (const auto& statement : statements) {
            if ((statement->kind == HirStmtKind::Variable ||
                 statement->kind == HirStmtKind::Assignment ||
                 statement->kind == HirStmtKind::ForEach) &&
                !global_bindings_.contains(statement->symbol_id)) {
                if (!llvm_type(statement->type, records_) || statement->type == "Void") {
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
            if ((statement->kind == HirStmtKind::Variable ||
                 statement->kind == HirStmtKind::Assignment ||
                 statement->kind == HirStmtKind::ForEach) &&
                !global_bindings_.contains(statement->symbol_id)) {
                const auto type = llvm_type(statement->type, records_);
                if (type && allocated_slots_.insert(statement->symbol_id).second) {
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
        const auto type = llvm_type(expression.type, records_);
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
            } else if (expression.type == "Char") {
                std::vector<std::uint32_t> scalars;
                if (decode_string_literal(expression.value, scalars) && scalars.size() == 1) {
                    return std::to_string(scalars.front());
                }
            } else if (expression.type == "Text") {
                return emit_text_literal(output, expression);
            } else if (is_floating_type(expression.type)) {
                std::string literal;
                if (format_float_literal(expression.value, expression.type, literal)) return literal;
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
                if (is_floating_type(expression.type)) {
                    output << "  " << value << " = fneg " << *type << ' ' << operand << '\n';
                } else {
                    output << "  " << value << " = sub " << *type << " 0, " << operand << '\n';
                }
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
            if (is_floating_type(expression.left->type)) {
                if (expression.value == "+") instruction = "fadd";
                else if (expression.value == "-") instruction = "fsub";
                else if (expression.value == "*") instruction = "fmul";
                else if (expression.value == "/") instruction = "fdiv";
                else if (expression.value == "%") instruction = "frem";
            } else {
                if (expression.value == "+") instruction = "add";
                else if (expression.value == "-") instruction = "sub";
                else if (expression.value == "*") instruction = "mul";
                else if (expression.value == "/") instruction = is_signed_integer(expression.left->type) ? "sdiv" : "udiv";
                else if (expression.value == "%") instruction = is_signed_integer(expression.left->type) ? "srem" : "urem";
            }
            if (!instruction.empty()) {
                output << "  " << value << " = " << instruction << ' ' << *type << ' '
                       << left << ", " << right << '\n';
                return value;
            }
            if (expression.value == "==" || expression.value == "!=" ||
                expression.value == "<" || expression.value == "<=" ||
                expression.value == ">" || expression.value == ">=") {
                  if (expression.left->type == "Text") {
                      declarations_.insert("declare i32 @indicode_text_compare(ptr, ptr)");
                      const auto comparison = next_value();
                      output << "  " << comparison << " = call i32 @indicode_text_compare(ptr "
                          << left << ", ptr " << right << ")\n";
                      std::string text_predicate;
                      if (expression.value == "==") text_predicate = "eq";
                      else if (expression.value == "!=") text_predicate = "ne";
                      else if (expression.value == "<") text_predicate = "slt";
                      else if (expression.value == "<=") text_predicate = "sle";
                      else if (expression.value == ">") text_predicate = "sgt";
                      else text_predicate = "sge";
                      output << "  " << value << " = icmp " << text_predicate
                          << " i32 " << comparison << ", 0\n";
                      return value;
                  }
                std::string predicate;
                if (is_floating_type(expression.left->type)) {
                    if (expression.value == "==") predicate = "oeq";
                    else if (expression.value == "!=") predicate = "une";
                    else if (expression.value == "<") predicate = "olt";
                    else if (expression.value == "<=") predicate = "ole";
                    else if (expression.value == ">") predicate = "ogt";
                    else predicate = "oge";
                } else if (expression.value == "==") predicate = "eq";
                else if (expression.value == "!=") predicate = "ne";
                else {
                    const bool signed_type = is_signed_integer(expression.left->type);
                    if (expression.value == "<") predicate = signed_type ? "slt" : "ult";
                    else if (expression.value == "<=") predicate = signed_type ? "sle" : "ule";
                    else if (expression.value == ">") predicate = signed_type ? "sgt" : "ugt";
                    else predicate = signed_type ? "sge" : "uge";
                }
                  output << "  " << value << " = "
                      << (is_floating_type(expression.left->type) ? "fcmp " : "icmp ")
                       << predicate << ' ' << *llvm_type(expression.left->type, records_) << ' '
                      << left << ", " << right << '\n';
                return value;
            }
            report("E-IR-001", "LLVM IR generation does not support binary operator '" +
                                     expression.value + "'", expression.span);
            return {};
        }
        case HirExprKind::Range: {
            if (!expression.left || !expression.right) {
                report("E-IR-002", "malformed range expression in HIR", expression.span);
                return {};
            }
            const auto start = emit_expression(output, *expression.left);
            const auto end = emit_expression(output, *expression.right);
            const auto range_type = llvm_type(expression.type, records_);
            if (!range_type || diagnostics_.size() != 0) return {};
            const auto with_start = next_value();
            output << "  " << with_start << " = insertvalue " << *range_type
                   << " zeroinitializer, " << *llvm_type(expression.left->type, records_)
                   << ' ' << start << ", 0\n";
            const auto result = next_value();
            output << "  " << result << " = insertvalue " << *range_type << ' '
                   << with_start << ", " << *llvm_type(expression.right->type, records_)
                   << ' ' << end << ", 1\n";
            return result;
        }
        case HirExprKind::Member: {
            if (!expression.left) {
                report("E-IR-002", "malformed record member expression in HIR", expression.span);
                return {};
            }
            const auto record = records_.find(expression.left->type);
            if (record == records_.end()) {
                report("E-IR-001", "member access base is not a supported record", expression.span);
                return {};
            }
            std::size_t field_index = record->second->fields.size();
            for (std::size_t index = 0; index < record->second->fields.size(); ++index) {
                if (record->second->fields[index].name == expression.value) {
                    field_index = index;
                    break;
                }
            }
            if (field_index == record->second->fields.size()) {
                report("E-IR-002", "record field is missing from verified HIR", expression.span);
                return {};
            }
            const auto base = emit_expression(output, *expression.left);
            const auto value = next_value();
            output << "  " << value << " = extractvalue %rec." << expression.left->type
                   << ' ' << base << ", " << field_index << '\n';
            return value;
        }
        case HirExprKind::ListLiteral:
            return emit_list_literal(output, expression);
        case HirExprKind::Index:
            return emit_index(output, expression);
        default:
            report("E-IR-001", "LLVM IR generation does not support this expression", expression.span);
            return {};
        }
    }

    std::string emit_call(std::ostringstream& output, const HirExpr& expression) {
        const auto record = records_.find(expression.symbol);
        if (record != records_.end() && expression.symbol_id == record->second->symbol_id) {
            return emit_record_constructor(output, expression, *record->second);
        }
        if (expression.symbol == "say") return emit_say(output, expression);
        if (expression.symbol.starts_with("to_")) return emit_conversion(output, expression);
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

        const auto return_type = llvm_type(function.return_type, records_);
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
            const auto parameter_type = llvm_type(function.parameters[index].type, records_);
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

    std::string emit_record_constructor(std::ostringstream& output, const HirExpr& expression,
                                       const HirRecord& record) {
        std::vector<std::string> evaluated;
        evaluated.reserve(expression.arguments.size());
        for (const auto& argument : expression.arguments) {
            evaluated.push_back(emit_expression(output, *argument));
            if (!diagnostics_.empty()) return {};
        }
        std::vector<std::string> fields(record.fields.size());
        for (std::size_t index = 0; index < evaluated.size(); ++index) {
            const auto name = index < expression.argument_names.size()
                ? expression.argument_names[index] : "";
            std::size_t field_index = record.fields.size();
            for (std::size_t candidate = 0; candidate < record.fields.size(); ++candidate) {
                if (record.fields[candidate].name == name) {
                    field_index = candidate;
                    break;
                }
            }
            if (field_index == record.fields.size() || !fields[field_index].empty()) {
                report("E-IR-002", "record constructor fields do not match the verified declaration",
                       expression.span);
                return {};
            }
            fields[field_index] = evaluated[index];
        }
        std::string aggregate = "zeroinitializer";
        for (std::size_t index = 0; index < record.fields.size(); ++index) {
            const auto field_type = llvm_type(record.fields[index].type, records_);
            if (!field_type || fields[index].empty()) {
                report("E-IR-001", "record field type is unsupported in LLVM IR", record.fields[index].span);
                return {};
            }
            const auto next = next_value();
            output << "  " << next << " = insertvalue %rec." << record.name << ' '
                   << aggregate << ", " << *field_type << ' ' << fields[index]
                   << ", " << index << '\n';
            aggregate = next;
        }
        return aggregate;
    }

    std::string emit_say(std::ostringstream& output, const HirExpr& expression) {
        if (expression.arguments.size() != 1) {
            report("E-IR-002", "malformed say call in HIR", expression.span);
            return {};
        }
        const auto argument_type = llvm_type(expression.arguments[0]->type, records_);
        if (!argument_type || expression.arguments[0]->type == "Void") {
            report("E-IR-001", "say does not support this value type in LLVM IR", expression.span);
            return {};
        }
        const auto value = emit_expression(output, *expression.arguments[0]);
        if (!diagnostics_.empty()) return {};
        const auto helper = "indicode_say_" + type_key(expression.arguments[0]->type);
        declarations_.insert("declare void @" + helper + "(" + *argument_type + ")");
        output << "  call void @" << helper << '(' << *argument_type << ' ' << value << ")\n";
        return {};
    }

    std::string emit_conversion(std::ostringstream& output, const HirExpr& expression) {
        if (expression.arguments.size() != 2 || expression.argument_names.size() != 2 ||
            expression.argument_names[0] != "" || expression.argument_names[1] != "checked") {
            report("E-IR-002", "malformed checked conversion call in HIR", expression.span);
            return {};
        }
        const auto source_type = llvm_type(expression.arguments[0]->type, records_);
        const auto target_type = llvm_type(expression.type, records_);
        if (!source_type || !target_type || expression.arguments[0]->type == "Void" ||
            expression.type == "Void") {
            report("E-IR-001", "checked conversion has an unsupported LLVM type", expression.span);
            return {};
        }
        const auto value = emit_expression(output, *expression.arguments[0]);
        if (!diagnostics_.empty()) return {};
        const auto helper = "indicode_checked_cast_" + type_key(expression.arguments[0]->type) +
                            "_to_" + type_key(expression.type);
        declarations_.insert("declare " + *target_type + " @" + helper + "(" + *source_type + ")");
        const auto result = next_value();
        output << "  " << result << " = call " << *target_type << " @" << helper
               << '(' << *source_type << ' ' << value << ")\n";
        return result;
    }

    std::string emit_text_literal(std::ostringstream& output, const HirExpr& expression) {
        std::vector<std::uint32_t> scalars;
        if (!decode_string_literal(expression.value, scalars)) {
            report("E-IR-002", "malformed text literal in HIR", expression.span);
            return {};
        }
        std::string raw;
        for (const auto scalar : scalars) append_utf8(raw, scalar);
        const auto global_name = "@.indi.str." + std::to_string(global_index_++);
        std::ostringstream encoded;
        encoded << global_name << " = private unnamed_addr constant [" << raw.size() + 1
                << " x i8] c\"";
        for (const unsigned char character : raw) {
            if (character >= 0x20 && character <= 0x7e && character != '"' && character != '\\') {
                encoded << static_cast<char>(character);
            } else {
                encoded << '\\' << std::uppercase << std::hex << std::setw(2)
                        << std::setfill('0') << static_cast<unsigned>(character) << std::dec;
            }
        }
        encoded << "\\00\", align 1";
        globals_.push_back(encoded.str());

        const auto pointer = next_value();
        output << "  " << pointer << " = getelementptr inbounds [" << raw.size() + 1
               << " x i8], ptr " << global_name << ", i64 0, i64 0\n";
        declarations_.insert("declare ptr @indicode_text_from_literal(ptr, i64)");
        const auto result = next_value();
        output << "  " << result << " = call ptr @indicode_text_from_literal(ptr "
               << pointer << ", i64 " << raw.size() << ")\n";
        return result;
    }

    std::string emit_list_literal(std::ostringstream& output, const HirExpr& expression) {
        const auto element_type_name = generic_argument(expression.type, "List<");
        const auto element_type = llvm_type(element_type_name, records_);
        if (element_type_name.empty() || !element_type || element_type_name == "Void") {
            report("E-IR-001", "list literal has an unsupported element type", expression.span);
            return {};
        }
        const auto key = type_key(element_type_name);
        declarations_.insert("declare ptr @indicode_list_new_" + key + "(i64)");
        declarations_.insert("declare void @indicode_list_set_" + key + "(ptr, i64, " +
                             *element_type + ")");
        const auto list = next_value();
        output << "  " << list << " = call ptr @indicode_list_new_" << key
               << "(i64 " << expression.arguments.size() << ")\n";
        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
            const auto value = emit_expression(output, *expression.arguments[index]);
            if (!diagnostics_.empty()) return {};
            output << "  call void @indicode_list_set_" << key << "(ptr " << list
                   << ", i64 " << index << ", " << *element_type << ' ' << value << ")\n";
        }
        return list;
    }

    std::string emit_index(std::ostringstream& output, const HirExpr& expression) {
        if (!expression.left || !expression.right) {
            report("E-IR-002", "malformed index expression in HIR", expression.span);
            return {};
        }
        const auto collection = emit_expression(output, *expression.left);
        const auto index = emit_expression(output, *expression.right);
        if (!diagnostics_.empty()) return {};
        const auto index64 = next_value();
        output << "  " << index64 << " = sext i32 " << index << " to i64\n";
        if (expression.left->type == "Text") {
            declarations_.insert("declare i32 @indicode_text_at(ptr, i64)");
            const auto value = next_value();
            output << "  " << value << " = call i32 @indicode_text_at(ptr " << collection
                   << ", i64 " << index64 << ")\n";
            return value;
        }
        const auto element_type = generic_argument(expression.left->type, "List<");
        const auto llvm_element_type = llvm_type(element_type, records_);
        if (element_type.empty() || !llvm_element_type) {
            report("E-IR-001", "indexing this collection type is not supported", expression.span);
            return {};
        }
        const auto helper = "indicode_list_get_" + type_key(element_type);
        declarations_.insert("declare " + *llvm_element_type + " @" + helper + "(ptr, i64)");
        const auto value = next_value();
        output << "  " << value << " = call " << *llvm_element_type << " @" << helper
               << "(ptr " << collection << ", i64 " << index64 << ")\n";
        return value;
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
                const auto type = llvm_type(statement->type, records_);
                if (!type) return;
                output << "  store " << *type << ' ' << value << ", ptr "
                       << binding->second.slot << '\n';
                break;
            }
            case HirStmtKind::Return:
                if (statement->expression) {
                    const auto value = emit_expression(output, *statement->expression);
                    const auto type = llvm_type(function_.return_type, records_);
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
            case HirStmtKind::Repeat:
                emit_repeat(output, *statement);
                break;
            case HirStmtKind::ForEach:
                emit_for_each(output, *statement);
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

    void emit_repeat(std::ostringstream& output, const HirStmt& statement) {
        if (!statement.expression) {
            report("E-IR-002", "malformed repeat count in HIR", statement.span);
            return;
        }
        const auto limit = emit_expression(output, *statement.expression);
        if (!diagnostics_.empty()) return;
        const auto type = llvm_type(statement.expression->type, records_);
        if (!type) {
            report("E-IR-001", "repeat count type is unsupported", statement.span);
            return;
        }
        const auto preheader = current_block_;
        const auto condition_label = make_label("repeat.cond");
        const auto body_label = make_label("repeat.body");
        const auto latch_label = make_label("repeat.latch");
        const auto end_label = make_label("repeat.end");
        output << "  br label %" << condition_label << '\n';
        terminated_ = true;
        emit_label(output, condition_label);
        const auto index = next_value();
        output << "  " << index << " = phi " << *type << " [ 0, %" << preheader
               << " ], [ %repeat.next." << block_index_ << ", %" << latch_label << " ]\n";
        const auto condition = next_value();
        output << "  " << condition << " = icmp "
               << (is_signed_integer(statement.expression->type) ? "slt" : "ult")
               << ' ' << *type << ' ' << index << ", " << limit << '\n'
               << "  br i1 " << condition << ", label %" << body_label
               << ", label %" << end_label << '\n';
        terminated_ = true;
        emit_label(output, body_label);
        emit_statements(output, statement.body);
        if (!terminated_) output << "  br label %" << latch_label << '\n';
        emit_label(output, latch_label);
        const auto next = "%repeat.next." + std::to_string(block_index_);
        output << "  " << next << " = add " << *type << ' ' << index << ", 1\n"
               << "  br label %" << condition_label << '\n';
        terminated_ = true;
        emit_label(output, end_label);
    }

    void emit_for_each(std::ostringstream& output, const HirStmt& statement) {
        if (!statement.expression) {
            report("E-IR-002", "malformed for-each iterable in HIR", statement.span);
            return;
        }
        const auto iterable_type = statement.expression->type;
        const auto iterable = emit_expression(output, *statement.expression);
        if (!diagnostics_.empty()) return;
        const auto range_element = generic_argument(iterable_type, "Range<");
        const auto list_element = generic_argument(iterable_type, "List<");
        const bool is_range = !range_element.empty();
        const bool is_list = !list_element.empty();
        const bool is_text = iterable_type == "Text";
        if (!is_range && !is_list && !is_text) {
            report("E-IR-001", "for-each iterable type is unsupported", statement.span);
            return;
        }

        std::string index_type = "i64";
        std::string start_value = "0";
        std::string end_value;
        std::string element_type_name = is_range ? range_element : (is_list ? list_element : "Char");
        auto element_type = llvm_type(element_type_name, records_);
        if (!element_type) {
            report("E-IR-001", "for-each element type is unsupported", statement.span);
            return;
        }
        if (is_range) {
            index_type = *element_type;
            const auto start = next_value();
            output << "  " << start << " = extractvalue " << *llvm_type(iterable_type, records_)
                   << ' ' << iterable << ", 0\n";
            start_value = start;
            end_value = next_value();
            output << "  " << end_value << " = extractvalue " << *llvm_type(iterable_type, records_)
                   << ' ' << iterable << ", 1\n";
        } else {
            const auto length_helper = is_text ? "indicode_text_length" : "indicode_list_length";
            declarations_.insert("declare i64 @" + std::string(length_helper) + "(ptr)");
            end_value = next_value();
            output << "  " << end_value << " = call i64 @" << length_helper
                   << "(ptr " << iterable << ")\n";
        }

        const auto preheader = current_block_;
        const auto condition_label = make_label("foreach.cond");
        const auto body_label = make_label("foreach.body");
        const auto latch_label = make_label("foreach.latch");
        const auto end_label = make_label("foreach.end");
        output << "  br label %" << condition_label << '\n';
        terminated_ = true;
        emit_label(output, condition_label);
        const auto index = next_value();
        const auto next_name = "%foreach.next." + std::to_string(block_index_);
        output << "  " << index << " = phi " << index_type << " [ " << start_value
               << ", %" << preheader << " ], [ " << next_name << ", %" << latch_label << " ]\n";
        const auto condition = next_value();
        const auto predicate = is_range && is_signed_integer(element_type_name) ? "slt" : "ult";
        output << "  " << condition << " = icmp " << predicate << ' ' << index_type
               << ' ' << index << ", " << end_value << '\n'
               << "  br i1 " << condition << ", label %" << body_label
               << ", label %" << end_label << '\n';
        terminated_ = true;
        emit_label(output, body_label);
        std::string element_value = index;
        if (!is_range) {
            const auto load_name = is_text ? "indicode_text_at"
                                           : "indicode_list_get_" + type_key(element_type_name);
            declarations_.insert("declare " + *element_type + " @" + std::string(load_name) +
                                 "(ptr, i64)");
            element_value = next_value();
            output << "  " << element_value << " = call " << *element_type << " @" << load_name
                   << "(ptr " << iterable << ", i64 " << index << ")\n";
        }
        const auto binding = bindings_.find(statement.symbol_id);
        if (binding == bindings_.end()) {
            report("E-IR-002", "for-each binding has no allocated storage", statement.span);
            return;
        }
        output << "  store " << *element_type << ' ' << element_value << ", ptr "
               << binding->second.slot << '\n';
        emit_statements(output, statement.body);
        if (!terminated_) output << "  br label %" << latch_label << '\n';
        emit_label(output, latch_label);
        output << "  " << next_name << " = add " << index_type << ' ' << index << ", 1\n"
               << "  br label %" << condition_label << '\n';
        terminated_ = true;
        emit_label(output, end_label);
    }

    std::string next_value() { return "%v." + std::to_string(value_index_++); }

    const HirFunction& function_;
    const FunctionMap& functions_;
    const RecordMap& records_;
    const BindingMap& global_bindings_;
    std::set<std::string>& declarations_;
    std::vector<std::string>& globals_;
    std::size_t& global_index_;
    std::vector<Diagnostic>& diagnostics_;
    std::unordered_map<std::uint64_t, Binding> bindings_;
    std::set<std::uint64_t> allocated_slots_;
    std::size_t value_index_ = 0;
    std::size_t block_index_ = 0;
    std::string current_block_;
    bool terminated_ = false;
};

std::unique_ptr<HirExpr> clone_expression(const HirExpr& expression) {
    auto clone = std::make_unique<HirExpr>();
    clone->kind = expression.kind;
    clone->type = expression.type;
    clone->span = expression.span;
    clone->value = expression.value;
    clone->symbol = expression.symbol;
    clone->symbol_id = expression.symbol_id;
    clone->type_symbol_id = expression.type_symbol_id;
    if (expression.left) clone->left = clone_expression(*expression.left);
    if (expression.right) clone->right = clone_expression(*expression.right);
    for (const auto& argument : expression.arguments) {
        clone->arguments.push_back(clone_expression(*argument));
    }
    clone->argument_names = expression.argument_names;
    return clone;
}

std::unique_ptr<HirStmt> clone_statement(const HirStmt& statement) {
    auto clone = std::make_unique<HirStmt>();
    clone->kind = statement.kind;
    clone->span = statement.span;
    clone->name = statement.name;
    clone->type = statement.type;
    clone->symbol_id = statement.symbol_id;
    clone->type_symbol_id = statement.type_symbol_id;
    clone->mutable_binding = statement.mutable_binding;
    if (statement.expression) clone->expression = clone_expression(*statement.expression);
    for (const auto& child : statement.body) clone->body.push_back(clone_statement(*child));
    for (const auto& child : statement.else_body) {
        clone->else_body.push_back(clone_statement(*child));
    }
    return clone;
}

std::unique_ptr<HirFunction> clone_function(const HirFunction& function) {
    auto clone = std::make_unique<HirFunction>();
    clone->name = function.name;
    clone->symbol_id = function.symbol_id;
    clone->generic_parameters = function.generic_parameters;
    clone->has_effect_annotation = function.has_effect_annotation;
    clone->declared_effects = function.declared_effects;
    clone->inferred_effects = function.inferred_effects;
    clone->parameters = function.parameters;
    clone->return_type = function.return_type;
    clone->return_type_symbol_id = function.return_type_symbol_id;
    clone->span = function.span;
    for (const auto& statement : function.body) {
        clone->body.push_back(clone_statement(*statement));
    }
    return clone;
}

using TypeSubstitution = std::unordered_map<std::string, std::string>;

std::string substitute_type(const std::string& type, const TypeSubstitution& substitutions) {
    if (const auto found = substitutions.find(type); found != substitutions.end()) {
        return found->second;
    }
    for (const auto& constructor : {std::string("List<"), std::string("Range<")}) {
        if (const auto element = generic_argument(type, constructor); !element.empty()) {
            return constructor + substitute_type(element, substitutions) + ">";
        }
    }
    return type;
}

void substitute_expression_types(HirExpr& expression, const TypeSubstitution& substitutions) {
    expression.type = substitute_type(expression.type, substitutions);
    if (expression.left) substitute_expression_types(*expression.left, substitutions);
    if (expression.right) substitute_expression_types(*expression.right, substitutions);
    for (auto& argument : expression.arguments) {
        substitute_expression_types(*argument, substitutions);
    }
}

void substitute_statement_types(HirStmt& statement, const TypeSubstitution& substitutions) {
    statement.type = substitute_type(statement.type, substitutions);
    if (statement.expression) substitute_expression_types(*statement.expression, substitutions);
    for (auto& child : statement.body) substitute_statement_types(*child, substitutions);
    for (auto& child : statement.else_body) substitute_statement_types(*child, substitutions);
}

void substitute_function_types(HirFunction& function, const TypeSubstitution& substitutions) {
    function.return_type = substitute_type(function.return_type, substitutions);
    for (auto& parameter : function.parameters) {
        parameter.type = substitute_type(parameter.type, substitutions);
    }
    for (auto& statement : function.body) {
        substitute_statement_types(*statement, substitutions);
    }
}

bool bind_generic_type(const std::string& pattern, const std::string& actual,
                       const std::set<std::string>& generic_names,
                       TypeSubstitution& substitutions) {
    if (generic_names.contains(pattern)) {
        const auto [found, inserted] = substitutions.emplace(pattern, actual);
        return inserted || found->second == actual;
    }
    for (const auto& constructor : {std::string("List<"), std::string("Range<")}) {
        const auto pattern_element = generic_argument(pattern, constructor);
        if (!pattern_element.empty()) {
            const auto actual_element = generic_argument(actual, constructor);
            return !actual_element.empty() &&
                   bind_generic_type(pattern_element, actual_element, generic_names, substitutions);
        }
    }
    return pattern == actual;
}

class FunctionSpecializer {
public:
    using FunctionMap = FunctionEmitter::FunctionMap;

    FunctionSpecializer(const HirProgram& program, std::vector<Diagnostic>& diagnostics)
        : program_(program), diagnostics_(diagnostics) {
        for (const auto& function : program.functions) {
            original_functions_.emplace(function.symbol_id, &function);
            next_symbol_id_ = std::max(next_symbol_id_, function.symbol_id);
        }
        ++next_symbol_id_;
    }

    std::vector<std::unique_ptr<HirFunction>>& prepare_functions() {
        for (const auto& function : program_.functions) {
            if (!function.generic_parameters.empty()) continue;
            auto lowered = clone_function(function);
            auto* pointer = lowered.get();
            functions_.push_back(std::move(lowered));
            rewrite_function(*pointer);
            if (!diagnostics_.empty()) return functions_;
        }
        finish_specializations();
        return functions_;
    }

    void rewrite_function(HirFunction& function) {
        for (auto& statement : function.body) rewrite_statement(*statement);
    }

    void finish_specializations() {
        while (next_pending_ < pending_specializations_.size() && diagnostics_.empty()) {
            auto* function = pending_specializations_[next_pending_++];
            rewrite_function(*function);
        }
    }

private:
    static std::string specialization_key(std::uint64_t symbol_id,
                                         const TypeSubstitution& substitutions,
                                         const std::vector<GenericParameter>& parameters) {
        std::string key = std::to_string(symbol_id);
        for (const auto& parameter : parameters) {
            const auto& type = substitutions.at(parameter.name);
            key += ":" + std::to_string(type.size()) + ":" + type;
        }
        return key;
    }

    static std::string specialized_name(std::uint64_t symbol_id,
                                        const TypeSubstitution& substitutions,
                                        const std::vector<GenericParameter>& parameters) {
        std::ostringstream name;
        name << "indicode.generic." << symbol_id;
        for (const auto& parameter : parameters) {
            name << '.';
            for (const unsigned char character : substitutions.at(parameter.name)) {
                name << std::hex << std::setw(2) << std::setfill('0')
                     << static_cast<unsigned>(character);
            }
        }
        return name.str();
    }

    bool infer_substitutions(const HirFunction& function, const HirExpr& call,
                             TypeSubstitution& substitutions) {
        if (function.parameters.size() != call.arguments.size() ||
            call.argument_names.size() != call.arguments.size()) {
            return false;
        }
        std::set<std::string> generic_names;
        for (const auto& parameter : function.generic_parameters) {
            generic_names.insert(parameter.name);
        }
        std::vector<std::string> argument_types(function.parameters.size());
        std::vector<bool> assigned(function.parameters.size(), false);
        std::size_t next_positional = 0;
        for (std::size_t index = 0; index < call.arguments.size(); ++index) {
            const auto& argument_name = call.argument_names[index];
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
            if (parameter_index >= assigned.size() || assigned[parameter_index]) return false;
            assigned[parameter_index] = true;
            argument_types[parameter_index] = call.arguments[index]->type;
        }
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (!assigned[index] ||
                !bind_generic_type(function.parameters[index].type, argument_types[index],
                                  generic_names, substitutions)) {
                return false;
            }
        }
        return substitutions.size() == generic_names.size();
    }

    HirFunction* specialize(const HirFunction& function, const HirExpr& call) {
        TypeSubstitution substitutions;
        if (!infer_substitutions(function, call, substitutions)) {
            diagnostics_.emplace_back(
                DiagnosticSeverity::Error, "E-IR-002",
                "generic call arguments do not determine a consistent specialization",
                call.span);
            return nullptr;
        }
        const auto key = specialization_key(function.symbol_id, substitutions,
                                           function.generic_parameters);
        if (const auto found = specializations_.find(key); found != specializations_.end()) {
            return found->second;
        }

        auto instance = clone_function(function);
        instance->name = specialized_name(function.symbol_id, substitutions,
                                          function.generic_parameters);
        instance->symbol_id = next_symbol_id_++;
        instance->generic_parameters.clear();
        substitute_function_types(*instance, substitutions);
        auto* pointer = instance.get();
        specializations_.emplace(key, pointer);
        functions_.push_back(std::move(instance));
        pending_specializations_.push_back(pointer);
        return pointer;
    }

    void rewrite_expression(HirExpr& expression) {
        if (expression.left) rewrite_expression(*expression.left);
        if (expression.right) rewrite_expression(*expression.right);
        for (auto& argument : expression.arguments) rewrite_expression(*argument);
        const auto target = original_functions_.find(expression.symbol_id);
        if (expression.kind != HirExprKind::Call || target == original_functions_.end() ||
            target->second->generic_parameters.empty()) {
            return;
        }
        auto* instance = specialize(*target->second, expression);
        if (instance == nullptr) return;
        expression.symbol = instance->name;
        expression.symbol_id = instance->symbol_id;
    }

    void rewrite_statement(HirStmt& statement) {
        if (statement.expression) rewrite_expression(*statement.expression);
        for (auto& child : statement.body) rewrite_statement(*child);
        for (auto& child : statement.else_body) rewrite_statement(*child);
    }

    const HirProgram& program_;
    std::vector<Diagnostic>& diagnostics_;
    FunctionMap original_functions_;
    std::vector<std::unique_ptr<HirFunction>> functions_;
    std::vector<HirFunction*> pending_specializations_;
    std::unordered_map<std::string, HirFunction*> specializations_;
    std::size_t next_pending_ = 0;
    std::uint64_t next_symbol_id_ = 0;
};

} // namespace

LlvmIrResult generate_llvm_ir(const HirProgram& program) {
    LlvmIrResult result;
    result.diagnostics = verify_hir(program);
    if (!result.diagnostics.empty()) return result;
    std::set<std::string> declarations;
    std::vector<std::string> globals;
    std::size_t global_index = 0;
    std::vector<std::string> function_bodies;
    FunctionEmitter::FunctionMap functions;
    RecordMap records;
    FunctionEmitter::BindingMap global_bindings;
    std::vector<std::string> global_definitions;
    FunctionSpecializer specializer(program, result.diagnostics);
    auto& lowered_functions = specializer.prepare_functions();
    if (!result.diagnostics.empty()) return result;
    for (const auto& record : program.records) {
        records.emplace(record.name, &record);
    }
    for (const auto& statement : program.statements) {
        if (statement->kind != HirStmtKind::Variable &&
            statement->kind != HirStmtKind::Assignment) {
            continue;
        }
        if (global_bindings.contains(statement->symbol_id)) continue;
        const auto type = llvm_type(statement->type, records);
        if (!type || statement->type == "Void") {
            result.diagnostics.emplace_back(
                DiagnosticSeverity::Error, "E-IR-001",
                "LLVM IR generation does not support global type '" + statement->type + "'",
                statement->span);
            return result;
        }
        const auto name = "@.indi.global." + std::to_string(statement->symbol_id);
        global_bindings.emplace(statement->symbol_id, Binding{statement->type, name});
        global_definitions.push_back(name + " = global " + *type + " zeroinitializer");
    }
    std::unique_ptr<HirFunction> initializer;
    if (!program.statements.empty()) {
        initializer = std::make_unique<HirFunction>();
        const bool entry_name_taken = std::any_of(
            program.functions.begin(), program.functions.end(),
            [](const HirFunction& function) { return function.name == "indicode_entry"; });
        initializer->name = entry_name_taken ? "indicode.entry.init" : "indicode_entry";
        initializer->return_type = "Int";
        initializer->span = program.statements.front()->span;
        for (const auto& statement : program.statements) {
            initializer->body.push_back(clone_statement(*statement));
        }
        specializer.rewrite_function(*initializer);
        specializer.finish_specializations();
        if (!result.diagnostics.empty()) return result;
        auto return_statement = std::make_unique<HirStmt>();
        return_statement->kind = HirStmtKind::Return;
        return_statement->span = initializer->span;
        return_statement->expression = std::make_unique<HirExpr>();
        return_statement->expression->kind = HirExprKind::Literal;
        return_statement->expression->type = "Int";
        return_statement->expression->value = "0";
        return_statement->expression->span = initializer->span;
        initializer->body.push_back(std::move(return_statement));
    }
    for (const auto& function : lowered_functions) {
        functions.emplace(function->symbol_id, function.get());
    }
    for (const auto& function : lowered_functions) {
        function_bodies.push_back(
            FunctionEmitter(*function, functions, records, global_bindings, declarations,
                            globals, global_index, result.diagnostics).emit());
        if (!result.diagnostics.empty()) break;
    }
    if (result.diagnostics.empty() && initializer) {
        function_bodies.push_back(
            FunctionEmitter(*initializer, functions, records, global_bindings, declarations,
                            globals, global_index, result.diagnostics).emit());
    }
    if (result.diagnostics.empty()) {
        std::ostringstream output;
        output << "; Indicode textual LLVM IR\n";
        for (const auto& record : program.records) {
            output << "%rec." << record.name << " = type { ";
            for (std::size_t index = 0; index < record.fields.size(); ++index) {
                if (index != 0) output << ", ";
                const auto field_type = llvm_type(record.fields[index].type, records);
                if (!field_type) {
                    result.diagnostics.emplace_back(
                        DiagnosticSeverity::Error, "E-IR-001",
                        "LLVM IR generation does not support record field type '" +
                            record.fields[index].type + "'", record.fields[index].span);
                    return result;
                }
                output << *field_type;
            }
            output << " }\n";
        }
        if (!program.records.empty()) output << '\n';
        for (const auto& global : global_definitions) output << global << '\n';
        if (!global_definitions.empty()) output << '\n';
        for (const auto& global : globals) output << global << '\n';
        if (!globals.empty()) output << '\n';
        for (const auto& declaration : declarations) output << declaration << '\n';
        if (!declarations.empty()) output << '\n';
        for (const auto& function_body : function_bodies) output << function_body << '\n';
        result.ir = output.str();
    }
    return result;
}

} // namespace indicode