#include "indicode/hir.hpp"

#include <charconv>
#include <cstdint>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace indicode {
namespace {

std::unique_ptr<HirExpr> lower_expression(const Expr& expression) {
    auto result = std::make_unique<HirExpr>();
    result->kind = static_cast<HirExprKind>(expression.kind);
    result->type = expression.inferred_type;
    result->span = expression.span;
    result->value = expression.value;
    result->symbol = expression.resolved_symbol;
    result->symbol_id = expression.resolved_symbol_id;
    result->type_symbol_id = expression.resolved_type_symbol_id;
    if (expression.left) {
        result->left = lower_expression(*expression.left);
    }
    if (expression.right) {
        result->right = lower_expression(*expression.right);
    }
    for (const auto& argument : expression.arguments) {
        result->arguments.push_back(lower_expression(*argument));
    }
    result->argument_names = expression.argument_names;
    if (result->kind == HirExprKind::Call && result->symbol.empty()) {
        result->symbol = result->value;
    }
    return result;
}

std::unique_ptr<HirStmt> lower_statement(const Stmt& statement) {
    auto result = std::make_unique<HirStmt>();
    result->kind = static_cast<HirStmtKind>(statement.kind);
    result->span = statement.span;
    result->name = statement.name;
    result->type = statement.declared_type;
    result->symbol_id = statement.resolved_symbol_id;
    result->type_symbol_id = statement.resolved_type_symbol_id;
    result->mutable_binding = statement.mutable_binding;
    if (statement.expression) {
        result->expression = lower_expression(*statement.expression);
    }
    for (const auto& child : statement.body) {
        result->body.push_back(lower_statement(*child));
    }
    for (const auto& child : statement.else_body) {
        result->else_body.push_back(lower_statement(*child));
    }
    return result;
}

void lower_statements(const std::vector<std::unique_ptr<Stmt>>& statements,
                      std::vector<std::unique_ptr<HirStmt>>& output) {
    for (const auto& statement : statements) {
        output.push_back(lower_statement(*statement));
    }
}

bool is_integer_type(const std::string& type) {
    return type == "Byte" || type == "U8" || type == "Int2" || type == "UInt2" ||
           type == "Int" || type == "UInt" || type == "Long" || type == "ULong";
}

bool is_numeric_type(const std::string& type) {
    return is_integer_type(type) || type == "Half" || type == "Float" || type == "Double";
}

std::string numeric_conversion_target(const std::string& name) {
    static const std::unordered_map<std::string, std::string> conversions{
        {"to_Byte", "Byte"}, {"to_U8", "U8"}, {"to_Int2", "Int2"},
        {"to_UInt2", "UInt2"}, {"to_Int", "Int"}, {"to_UInt", "UInt"},
        {"to_Long", "Long"}, {"to_ULong", "ULong"}, {"to_Half", "Half"},
        {"to_Float", "Float"}, {"to_Double", "Double"}
    };
    const auto conversion = conversions.find(name);
    return conversion == conversions.end() ? std::string{} : conversion->second;
}

std::uint64_t builtin_function_symbol_id(const std::string& name) {
    static const std::unordered_map<std::string, std::uint64_t> ids{
        {"say", 0x200001ULL}, {"to_Byte", 0x200002ULL},
        {"to_U8", 0x200003ULL}, {"to_Int2", 0x200004ULL},
        {"to_UInt2", 0x200005ULL}, {"to_Int", 0x200006ULL},
        {"to_UInt", 0x200007ULL}, {"to_Long", 0x200008ULL},
        {"to_ULong", 0x200009ULL}, {"to_Half", 0x20000aULL},
        {"to_Float", 0x20000bULL}, {"to_Double", 0x20000cULL}
    };
    const auto found = ids.find(name);
    return found == ids.end() ? 0 : found->second;
}

bool valid_tensor_type(const std::string& type) {
    constexpr std::string_view prefix = "Tensor<";
    if (!type.starts_with(prefix) || type.size() <= prefix.size() + 3 ||
        !type.ends_with("]>")) {
        return false;
    }
    const auto dimensions_start = type.find(",[", prefix.size());
    if (dimensions_start == std::string::npos) return false;
    const auto element_type = type.substr(prefix.size(), dimensions_start - prefix.size());
    if (!is_numeric_type(element_type)) return false;

    const auto dimensions_end = type.size() - 2;
    std::size_t start = dimensions_start + 2;
    while (start < dimensions_end) {
        const auto end = type.find(',', start);
        const auto stop = end == std::string::npos || end > dimensions_end ? dimensions_end : end;
        if (stop == start) return false;
        const auto dimension = type.substr(start, stop - start);
        if (dimension.front() >= '0' && dimension.front() <= '9') {
            std::string digits;
            for (const char character : dimension) {
                if (character != '_') digits.push_back(character);
            }
            unsigned base = 10;
            std::size_t prefix_length = 0;
            if (digits.size() > 2 && digits[0] == '0') {
                if (digits[1] == 'b' || digits[1] == 'B') { base = 2; prefix_length = 2; }
                else if (digits[1] == 'o' || digits[1] == 'O') { base = 8; prefix_length = 2; }
                else if (digits[1] == 'x' || digits[1] == 'X') { base = 16; prefix_length = 2; }
            }
            std::uint64_t value = 0;
            const auto parsed = std::from_chars(digits.data() + prefix_length,
                                                digits.data() + digits.size(), value,
                                                static_cast<int>(base));
            if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) return false;
        } else {
            if (!(dimension.front() == '_' || (dimension.front() >= 'A' && dimension.front() <= 'Z') ||
                  (dimension.front() >= 'a' && dimension.front() <= 'z'))) return false;
            for (const char character : dimension) {
                if (!(character == '_' || (character >= 'A' && character <= 'Z') ||
                      (character >= 'a' && character <= 'z') ||
                      (character >= '0' && character <= '9'))) return false;
            }
        }
        start = stop + 1;
    }
    return start == dimensions_end + 1;
}

std::string range_element_type(const std::string& type);

bool always_returns(const std::vector<std::unique_ptr<HirStmt>>& statements) {
    for (const auto& statement : statements) {
        if (statement->kind == HirStmtKind::Return) {
            return true;
        }
        if (statement->kind == HirStmtKind::If && !statement->else_body.empty() &&
            always_returns(statement->body) && always_returns(statement->else_body)) {
            return true;
        }
    }
    return false;
}

bool is_known_type(const std::string& type, const std::unordered_set<std::string>& record_names) {
    if (type.starts_with("Tensor<")) return valid_tensor_type(type);
    if (const auto range_type = range_element_type(type); !range_type.empty()) return true;
    constexpr std::string_view list_prefix = "List<";
    if (type.compare(0, list_prefix.size(), list_prefix) == 0 &&
        type.size() > list_prefix.size() && type.back() == '>') {
        const auto element_type = type.substr(list_prefix.size(), type.size() - list_prefix.size() - 1);
        return is_known_type(element_type, record_names) && element_type != "Void";
    }
    static const std::unordered_set<std::string> types{
        "Bool", "Byte", "U8", "Int2", "UInt2", "Int", "UInt", "Long", "ULong",
        "Half", "Float", "Double", "Char", "Text", "Void"
    };
    return types.find(type) != types.end() || record_names.contains(type);
}

std::string list_element_type(const std::string& type) {
    constexpr std::string_view list_prefix = "List<";
    if (type.compare(0, list_prefix.size(), list_prefix) == 0 &&
        type.size() > list_prefix.size() && type.back() == '>') {
        return type.substr(list_prefix.size(), type.size() - list_prefix.size() - 1);
    }
    return {};
}

std::string range_element_type(const std::string& type) {
    constexpr std::string_view range_prefix = "Range<";
    if (type.compare(0, range_prefix.size(), range_prefix) == 0 &&
        type.size() > range_prefix.size() && type.back() == '>') {
        const auto element_type = type.substr(range_prefix.size(),
                                              type.size() - range_prefix.size() - 1);
        return is_integer_type(element_type) ? element_type : std::string{};
    }
    return {};
}

}

HirProgram lower_to_hir(const Program& program) {
    HirProgram result;
    for (const auto& record : program.records) {
        result.records.push_back({record.name, record.symbol_id, record.fields, record.span});
    }
    for (const auto& function : program.functions) {
        HirFunction lowered;
        lowered.name = function.name;
        lowered.symbol_id = function.symbol_id;
        lowered.return_type = function.return_type.empty() ? "Void" : function.return_type;
        lowered.return_type_symbol_id = function.return_type_symbol_id;
        lowered.span = function.span;
        for (const auto& parameter : function.parameters) {
            lowered.parameters.push_back(
                {parameter.name, parameter.type, parameter.span, parameter.resolved_symbol_id,
                 parameter.resolved_type_symbol_id});
        }
        lower_statements(function.body, lowered.body);
        result.functions.push_back(std::move(lowered));
    }
    lower_statements(program.statements, result.statements);
    return result;
}

std::vector<Diagnostic> verify_hir(const HirProgram& program) {
    std::vector<Diagnostic> diagnostics;
    std::unordered_map<std::string, const HirRecord*> records;
    std::unordered_set<std::string> record_names;
    std::unordered_set<std::uint64_t> declaration_symbol_ids;
    for (const auto& record : program.records) {
        if (!records.emplace(record.name, &record).second) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-023",
                                   "duplicate record in typed HIR", record.span});
            continue;
        }
        if (record.symbol_id == 0 || !declaration_symbol_ids.insert(record.symbol_id).second) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-036",
                                   "record must have a unique stable symbol identity", record.span});
        }
        record_names.insert(record.name);
    }
    for (const auto& record : program.records) {
        std::unordered_set<std::string> field_names;
        for (const auto& field : record.fields) {
            if (field.symbol_id == 0 ||
                !declaration_symbol_ids.insert(field.symbol_id).second) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-045",
                                       "record field must have a unique stable symbol identity",
                                       field.span});
            }
            if (!field_names.insert(field.name).second) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-024",
                                       "duplicate field in typed HIR record", field.span});
            }
            if (!is_known_type(field.type, record_names) || field.type == "Void") {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-025",
                                       "record field has an invalid type", field.span});
            }
            if (const auto found = records.find(field.type);
                found != records.end() && field.type_symbol_id != found->second->symbol_id) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-037",
                                       "record field type identity does not match its type", field.span});
            }
        }
    }
    std::unordered_map<std::string, const HirFunction*> functions;
    for (const auto& function : program.functions) {
        if (!functions.emplace(function.name, &function).second) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-001",
                                   "duplicate function in typed HIR", function.span});
        }
        if (function.symbol_id == 0 ||
            !declaration_symbol_ids.insert(function.symbol_id).second) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-038",
                                   "function must have a unique stable symbol identity", function.span});
        }
        if (!is_known_type(function.return_type, record_names)) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-002",
                                   "function has an unknown return type", function.span});
        }
        if (const auto found = records.find(function.return_type);
            found != records.end() &&
            function.return_type_symbol_id != found->second->symbol_id) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-039",
                                   "function return type identity does not match its type",
                                   function.span});
        }
        for (const auto& parameter : function.parameters) {
            if (!is_known_type(parameter.type, record_names) || parameter.type == "Void") {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-003",
                                       "parameter has an invalid type", parameter.span});
            }
            if (const auto found = records.find(parameter.type);
                found != records.end() && parameter.type_symbol_id != found->second->symbol_id) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-040",
                                       "parameter type identity does not match its type",
                                       parameter.span});
            }
        }
    }

    std::function<std::uint64_t(const std::string&)> record_type_symbol_id;
    record_type_symbol_id = [&](const std::string& type) -> std::uint64_t {
        if (const auto found = records.find(type); found != records.end()) {
            return found->second->symbol_id;
        }
        for (const auto& prefix : {std::string("List<"), std::string("Range<")}) {
            if (type.starts_with(prefix) && type.size() > prefix.size() && type.back() == '>') {
                return record_type_symbol_id(type.substr(prefix.size(),
                                                         type.size() - prefix.size() - 1));
            }
        }
        if (type.starts_with("Tensor<")) {
            const auto dimensions_start = type.find(",[", 7);
            if (dimensions_start != std::string::npos) {
                return record_type_symbol_id(type.substr(7, dimensions_start - 7));
            }
        }
        return 0;
    };

    struct HirBinding {
        std::string type;
        bool mutable_binding = false;
        std::uint64_t symbol_id = 0;
    };
    std::vector<std::unordered_map<std::string, HirBinding>> binding_scopes(1);
    std::unordered_set<std::uint64_t> declared_symbol_ids = declaration_symbol_ids;
    auto declare_hir_binding = [&](const std::string& name, HirBinding binding,
                                   SourceSpan span, const char* duplicate_code) {
        if (name.empty() || binding.symbol_id == 0 ||
            !declared_symbol_ids.insert(binding.symbol_id).second ||
            !binding_scopes.back().emplace(name, std::move(binding)).second) {
            diagnostics.push_back({DiagnosticSeverity::Error, duplicate_code,
                                   "HIR binding must have a unique name and symbol identity in scope", span});
            return false;
        }
        return true;
    };
    auto find_binding = [&](const std::string& name) -> const HirBinding* {
        for (auto scope = binding_scopes.rbegin(); scope != binding_scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        return nullptr;
    };

    std::function<std::string(const HirExpr&)> verify_expression;
    verify_expression = [&](const HirExpr& expression) -> std::string {
        if (!is_known_type(expression.type, record_names)) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-004",
                                   "expression is missing a valid type annotation", expression.span});
            return "<error>";
        }
        const auto expected_type_symbol_id = record_type_symbol_id(expression.type);
        if (expected_type_symbol_id != 0 &&
            expression.type_symbol_id != expected_type_symbol_id) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-043",
                                   "expression type identity does not match its record type",
                                   expression.span});
        }

        auto child_type = [&](const std::unique_ptr<HirExpr>& child) {
            if (!child) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-005",
                                       "expression node is missing a required child", expression.span});
                return std::string("<error>");
            }
            return verify_expression(*child);
        };

        switch (expression.kind) {
        case HirExprKind::Name: {
            const auto* binding = find_binding(expression.symbol);
            if (expression.symbol.empty() || expression.symbol_id == 0 || binding == nullptr) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-006",
                                       "name expression has no resolved binding", expression.span});
            } else if (binding->symbol_id != expression.symbol_id ||
                       binding->type != expression.type) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-033",
                                       "name expression identity or type does not match its resolved binding",
                                       expression.span});
            }
            break;
        }
        case HirExprKind::Literal:
            if (expression.type == "Void") {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-007",
                                       "literal cannot have type Void", expression.span});
            }
            break;
        case HirExprKind::Unary: {
            const auto operand_type = child_type(expression.left);
            const bool valid = (expression.value == "!" && operand_type == "Bool" && expression.type == "Bool") ||
                               ((expression.value == "+" || expression.value == "-") &&
                                is_numeric_type(operand_type) && expression.type == operand_type) ||
                               (expression.value == "~" && is_integer_type(operand_type) &&
                                expression.type == operand_type);
            if (!valid && operand_type != "<error>") {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-008",
                                       "unary operation has inconsistent operand/result types", expression.span});
            }
            break;
        }
        case HirExprKind::Range: {
            const auto start_type = child_type(expression.left);
            const auto end_type = child_type(expression.right);
            if (!is_integer_type(start_type) || start_type != end_type ||
                expression.type != "Range<" + start_type + ">") {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-029",
                                       "range bounds and result type are inconsistent", expression.span});
            }
            break;
        }
        case HirExprKind::Member: {
            const auto base_type = child_type(expression.left);
            const auto record = records.find(base_type);
            bool matches = false;
            if (record != records.end()) {
                for (const auto& field : record->second->fields) {
                    if (field.name == expression.value && field.type == expression.type) {
                        matches = true;
                        if (expression.symbol_id != field.symbol_id) {
                            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-046",
                                                   "member reference identity does not match its field",
                                                   expression.span});
                        }
                        break;
                    }
                }
            }
            if (!matches) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-027",
                                       "member access does not match a declared record field", expression.span});
            }
            break;
        }
        case HirExprKind::Binary: {
            const auto left_type = child_type(expression.left);
            const auto right_type = child_type(expression.right);
            if (left_type == "<error>" || right_type == "<error>") {
                break;
            }
            const bool arithmetic = expression.value == "+" || expression.value == "-" ||
                                    expression.value == "*" || expression.value == "/" || expression.value == "%";
            const bool logical = expression.value == "&&" || expression.value == "||";
            const bool equality = expression.value == "==" || expression.value == "!=";
            const bool ordering = expression.value == "<" || expression.value == "<=" ||
                                  expression.value == ">" || expression.value == ">=";
            const bool valid = arithmetic
                ? left_type == right_type && is_numeric_type(left_type) && expression.type == left_type
                : logical
                    ? left_type == "Bool" && right_type == "Bool" && expression.type == "Bool"
                    : equality
                        ? left_type == right_type &&
                          (is_numeric_type(left_type) || left_type == "Bool" ||
                           left_type == "Char" || left_type == "Text") &&
                          expression.type == "Bool"
                        : ordering
                            ? left_type == right_type &&
                              (is_numeric_type(left_type) || left_type == "Char" || left_type == "Text") &&
                              expression.type == "Bool"
                            : false;
            if (!valid) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-009",
                                       "binary operation has inconsistent operand/result types", expression.span});
            }
            break;
        }
        case HirExprKind::Call: {
            if (expression.argument_names.size() != expression.arguments.size()) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-031",
                                       "call argument names must align with call arguments",
                                       expression.span});
            }
            std::vector<std::string> argument_types;
            for (const auto& argument : expression.arguments) {
                argument_types.push_back(verify_expression(*argument));
            }
            if (expression.symbol == "say") {
                const bool has_named_argument = !expression.argument_names.empty() &&
                                                !expression.argument_names.front().empty();
                if (argument_types.size() != 1 || argument_types[0] == "Void" ||
                    expression.type != "Void" || has_named_argument ||
                    expression.symbol_id != 0x200001ULL) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-010",
                                           "say call has an invalid signature", expression.span});
                }
                break;
            }
            const auto target = functions.find(expression.symbol);
            if (target == functions.end()) {
                const auto conversion_type = numeric_conversion_target(expression.symbol);
                if (!conversion_type.empty()) {
                    const bool valid = argument_types.size() == 2 &&
                        expression.argument_names.size() == 2 &&
                        expression.argument_names[0].empty() &&
                        expression.argument_names[1] == "checked" &&
                        argument_types[0] != "Void" && is_numeric_type(argument_types[0]) &&
                        expression.arguments[1]->kind == HirExprKind::Literal &&
                        expression.arguments[1]->value == "true" &&
                        argument_types[1] == "Bool" && expression.type == conversion_type &&
                        expression.symbol_id == builtin_function_symbol_id(expression.symbol);
                    if (!valid) {
                        diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-028",
                                               "checked numeric conversion has an invalid signature",
                                               expression.span});
                    }
                    break;
                }
                const auto record = records.find(expression.symbol);
                if (record == records.end()) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-011",
                                           "call target is not present in the typed HIR", expression.span});
                    break;
                }
                if (expression.symbol_id != record->second->symbol_id) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-041",
                                           "record constructor identity does not match its declaration",
                                           expression.span});
                }
                const auto& fields = record->second->fields;
                bool matches = expression.type == expression.symbol &&
                               fields.size() == argument_types.size() &&
                               expression.argument_names.size() == argument_types.size();
                std::vector<bool> assigned(fields.size(), false);
                for (std::size_t index = 0; index < argument_types.size(); ++index) {
                    const auto field_name = index < expression.argument_names.size()
                        ? expression.argument_names[index] : "";
                    std::size_t field_index = fields.size();
                    for (std::size_t candidate = 0; candidate < fields.size(); ++candidate) {
                        if (fields[candidate].name == field_name) {
                            field_index = candidate;
                            break;
                        }
                    }
                    if (field_index == fields.size() || assigned[field_index] ||
                        argument_types[index] != fields[field_index].type) {
                        matches = false;
                    } else {
                        assigned[field_index] = true;
                    }
                }
                for (const bool field_assigned : assigned) {
                    matches = matches && field_assigned;
                }
                if (!matches) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-026",
                                           "record construction does not match its field types", expression.span});
                }
                break;
            }
            if (expression.symbol_id != target->second->symbol_id) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-042",
                                       "call target identity does not match its function declaration",
                                       expression.span});
            }
            const auto& parameters = target->second->parameters;
            bool matches = parameters.size() == argument_types.size() &&
                           expression.type == target->second->return_type;
            std::vector<bool> assigned(parameters.size(), false);
            std::size_t next_positional = 0;
            for (std::size_t index = 0; matches && index < argument_types.size(); ++index) {
                std::size_t parameter_index = parameters.size();
                const std::string argument_name = index < expression.argument_names.size()
                    ? expression.argument_names[index]
                    : "";
                if (argument_name.empty()) {
                    while (next_positional < assigned.size() && assigned[next_positional]) ++next_positional;
                    if (next_positional < assigned.size()) parameter_index = next_positional++;
                } else {
                    for (std::size_t candidate = 0; candidate < parameters.size(); ++candidate) {
                        if (parameters[candidate].name == argument_name) {
                            parameter_index = candidate;
                            break;
                        }
                    }
                }
                if (parameter_index >= parameters.size() || assigned[parameter_index] ||
                    argument_types[index] != parameters[parameter_index].type) {
                    matches = false;
                } else {
                    assigned[parameter_index] = true;
                }
            }
            for (const bool parameter_assigned : assigned) {
                matches = matches && parameter_assigned;
            }
            if (!matches) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-012",
                                       "call signature does not match its typed HIR target", expression.span});
            }
            break;
        }
        case HirExprKind::ListLiteral: {
            if (expression.arguments.empty()) {
                if (list_element_type(expression.type).empty()) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-019",
                                           "empty list has no declared element type", expression.span});
                }
                break;
            }
            const auto element_type = verify_expression(*expression.arguments.front());
            bool matches = !element_type.empty() && element_type != "Void" &&
                           expression.type == "List<" + element_type + ">";
            for (std::size_t index = 1; index < expression.arguments.size(); ++index) {
                matches = matches && verify_expression(*expression.arguments[index]) == element_type;
            }
            if (!matches) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-020",
                                       "list elements do not match the list's declared element type", expression.span});
            }
            break;
        }
        case HirExprKind::Index: {
            const auto collection_type = child_type(expression.left);
            const auto index_type = child_type(expression.right);
            const auto element_type = list_element_type(collection_type);
            const bool valid_list = !element_type.empty() && expression.type == element_type;
            const bool valid_text = collection_type == "Text" && expression.type == "Char";
            if (index_type != "Int" || (!valid_list && !valid_text)) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-021",
                                       "index operation has inconsistent collection, index, or result types",
                                       expression.span});
            }
            break;
        }
        }
        return expression.type;
    };

    std::function<void(const std::vector<std::unique_ptr<HirStmt>>&,
                       const std::string&, bool)> verify_statements;
    verify_statements = [&](const std::vector<std::unique_ptr<HirStmt>>& statements,
                            const std::string& return_type, bool in_function) {
        for (const auto& statement : statements) {
            const bool needs_expression = statement->kind != HirStmtKind::Return || statement->expression != nullptr;
            std::string value_type;
            if (needs_expression && statement->expression) {
                value_type = verify_expression(*statement->expression);
            } else if (needs_expression && statement->kind != HirStmtKind::Return) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-013",
                                       "statement is missing its expression", statement->span});
            }

            const auto expected_type_symbol_id = record_type_symbol_id(statement->type);
            if (expected_type_symbol_id != 0 &&
                statement->type_symbol_id != expected_type_symbol_id) {
                diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-044",
                                       "statement type identity does not match its record type",
                                       statement->span});
            }
            switch (statement->kind) {
            case HirStmtKind::Variable: {
                if (!is_known_type(statement->type, record_names) || value_type != statement->type) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-014",
                                           "variable initializer does not match its declared HIR type", statement->span});
                }
                declare_hir_binding(statement->name,
                    HirBinding{statement->type, statement->mutable_binding, statement->symbol_id},
                    statement->span, "E-HIR-034");
                break;
            }
            case HirStmtKind::Assignment: {
                const auto* binding = find_binding(statement->name);
                if (binding == nullptr) {
                    if (statement->name.empty() || statement->symbol_id == 0 ||
                        !is_known_type(statement->type, record_names) ||
                        value_type != statement->type) {
                        diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-015",
                                               "assignment does not match its resolved HIR binding type",
                                               statement->span});
                    } else {
                        declare_hir_binding(statement->name,
                            HirBinding{statement->type, true, statement->symbol_id},
                            statement->span, "E-HIR-034");
                    }
                } else if (binding->symbol_id != statement->symbol_id) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-033",
                                           "assignment symbol identity does not match its resolved binding",
                                           statement->span});
                } else if (!binding->mutable_binding) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-035",
                                           "assignment targets an immutable HIR binding", statement->span});
                } else if (binding->type != statement->type || value_type != binding->type) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-015",
                                           "assignment does not match its resolved HIR binding type", statement->span});
                }
                break;
            }
            case HirStmtKind::Return:
                if (!in_function) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-032",
                                           "return statement appears outside a function", statement->span});
                } else if ((statement->expression ? value_type : "Void") != return_type) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-016",
                                           "return type does not match the containing function", statement->span});
                }
                break;
            case HirStmtKind::Expression:
                break;
            case HirStmtKind::If:
            case HirStmtKind::While:
                if (value_type != "Bool") {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-017",
                                           "condition must have type Bool", statement->span});
                }
                break;
            case HirStmtKind::Repeat:
                if (!is_integer_type(value_type)) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-018",
                                           "repeat count must have an integer type", statement->span});
                }
                break;
            case HirStmtKind::ForEach: {
                auto element_type = value_type == "Text" ? "Char" : list_element_type(value_type);
                if (element_type.empty()) element_type = range_element_type(value_type);
                if (statement->name.empty() || element_type.empty() || statement->type != element_type) {
                    diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-022",
                                           "for-each binding does not match its iterable element type",
                                           statement->span});
                }
                binding_scopes.emplace_back();
                declare_hir_binding(statement->name,
                    HirBinding{statement->type, false, statement->symbol_id},
                    statement->span, "E-HIR-034");
                verify_statements(statement->body, return_type, in_function);
                binding_scopes.pop_back();
                break;
            }
            }
            if (statement->kind == HirStmtKind::If) {
                binding_scopes.emplace_back();
                verify_statements(statement->body, return_type, in_function);
                binding_scopes.pop_back();
                if (!statement->else_body.empty()) {
                    binding_scopes.emplace_back();
                    verify_statements(statement->else_body, return_type, in_function);
                    binding_scopes.pop_back();
                }
            } else if (statement->kind == HirStmtKind::While ||
                       statement->kind == HirStmtKind::Repeat) {
                binding_scopes.emplace_back();
                verify_statements(statement->body, return_type, in_function);
                binding_scopes.pop_back();
            }
        }
    };

    verify_statements(program.statements, "Void", false);
    for (const auto& function : program.functions) {
        binding_scopes.emplace_back();
        for (const auto& parameter : function.parameters) {
            declare_hir_binding(parameter.name,
                HirBinding{parameter.type, false, parameter.symbol_id},
                parameter.span, "E-HIR-030");
        }
        verify_statements(function.body, function.return_type, true);
        if (function.return_type != "Void" && !always_returns(function.body)) {
            diagnostics.push_back({DiagnosticSeverity::Error, "E-HIR-026",
                                   "non-Void function does not return on every path", function.span});
        }
        binding_scopes.pop_back();
    }
    return diagnostics;
}

std::string print_hir(const HirProgram& program) {
    std::ostringstream output;
    std::function<void(const HirExpr&)> print_expression;
    print_expression = [&](const HirExpr& expression) {
        switch (expression.kind) {
        case HirExprKind::Name:
            output << '%' << expression.symbol << '#' << expression.symbol_id << ':' << expression.type;
            break;
        case HirExprKind::Literal:
            output << expression.value << ':' << expression.type;
            break;
        case HirExprKind::Unary:
            output << '(' << expression.value << ' ';
            print_expression(*expression.left);
            output << ") : " << expression.type;
            break;
        case HirExprKind::Range:
            output << "range ";
            print_expression(*expression.left);
            output << " .. ";
            print_expression(*expression.right);
            output << " : " << expression.type;
            break;
        case HirExprKind::Binary:
            output << '(' << expression.value << ' ';
            print_expression(*expression.left);
            output << ' ';
            print_expression(*expression.right);
            output << ") : " << expression.type;
            break;
        case HirExprKind::Call:
            output << "call @" << expression.symbol << '#' << expression.symbol_id << '(';
            for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                if (index != 0) output << ", ";
                if (index < expression.argument_names.size() && !expression.argument_names[index].empty()) {
                    output << expression.argument_names[index] << ": ";
                }
                print_expression(*expression.arguments[index]);
            }
            output << ") : " << expression.type;
            break;
        case HirExprKind::ListLiteral:
            output << "list<" << expression.type << ">[";
            for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                if (index != 0) output << ", ";
                print_expression(*expression.arguments[index]);
            }
            output << ']';
            break;
        case HirExprKind::Index:
            output << "index ";
            print_expression(*expression.left);
            output << '[';
            print_expression(*expression.right);
            output << "] : " << expression.type;
            break;
        case HirExprKind::Member:
            print_expression(*expression.left);
            output << '.' << expression.value << " : " << expression.type;
            break;
        }
    };

    std::function<void(const std::vector<std::unique_ptr<HirStmt>>&, std::size_t)> print_statements;
    print_statements = [&](const std::vector<std::unique_ptr<HirStmt>>& statements, std::size_t depth) {
        for (const auto& statement : statements) {
            for (std::size_t index = 0; index < depth; ++index) output << "  ";
            switch (statement->kind) {
            case HirStmtKind::Variable:
                output << (statement->mutable_binding ? "var " : "let ") << '%' << statement->name
                       << '#' << statement->symbol_id << ": " << statement->type << " = ";
                print_expression(*statement->expression);
                output << '\n';
                break;
            case HirStmtKind::Assignment:
                output << '%' << statement->name << '#' << statement->symbol_id
                       << ": " << statement->type << " = ";
                print_expression(*statement->expression);
                output << '\n';
                break;
            case HirStmtKind::Return:
                output << "return";
                if (statement->expression) {
                    output << ' ';
                    print_expression(*statement->expression);
                }
                output << '\n';
                break;
            case HirStmtKind::Expression:
                print_expression(*statement->expression);
                output << '\n';
                break;
            case HirStmtKind::If:
                output << "if "; print_expression(*statement->expression); output << '\n';
                print_statements(statement->body, depth + 1);
                if (!statement->else_body.empty()) {
                    for (std::size_t index = 0; index < depth; ++index) output << "  ";
                    output << "else\n";
                    print_statements(statement->else_body, depth + 1);
                }
                break;
            case HirStmtKind::While:
                output << "while "; print_expression(*statement->expression); output << '\n';
                print_statements(statement->body, depth + 1);
                break;
            case HirStmtKind::Repeat:
                output << "repeat "; print_expression(*statement->expression); output << " times\n";
                print_statements(statement->body, depth + 1);
                break;
            case HirStmtKind::ForEach:
                output << "for %" << statement->name << '#' << statement->symbol_id
                       << ": " << statement->type << " in ";
                print_expression(*statement->expression);
                output << '\n';
                print_statements(statement->body, depth + 1);
                break;
            }
        }
    };

    for (const auto& record : program.records) {
        output << "record @" << record.name << '#' << record.symbol_id << " {\n";
        for (const auto& field : record.fields) {
            output << "  " << field.name << ": " << field.type << '\n';
        }
        output << "}\n";
    }
    for (const auto& function : program.functions) {
        output << "func @" << function.name << '#' << function.symbol_id << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0) output << ", ";
            output << '%' << function.parameters[index].name << '#'
                   << function.parameters[index].symbol_id << ": " << function.parameters[index].type;
        }
        output << ") -> " << function.return_type << '\n';
        print_statements(function.body, 1);
    }
    print_statements(program.statements, 0);
    return output.str();
}

std::string serialize_hir_json(const HirProgram& program) {
    std::ostringstream output;
    const auto expression_kind_name = [](HirExprKind kind) -> const char* {
        switch (kind) {
        case HirExprKind::Name: return "name";
        case HirExprKind::Literal: return "literal";
        case HirExprKind::Unary: return "unary";
        case HirExprKind::Binary: return "binary";
        case HirExprKind::Range: return "range";
        case HirExprKind::Call: return "call";
        case HirExprKind::ListLiteral: return "list";
        case HirExprKind::Index: return "index";
        case HirExprKind::Member: return "member";
        }
        return "invalid";
    };
    const auto statement_kind_name = [](HirStmtKind kind) -> const char* {
        switch (kind) {
        case HirStmtKind::Variable: return "variable";
        case HirStmtKind::Assignment: return "assignment";
        case HirStmtKind::Return: return "return";
        case HirStmtKind::Expression: return "expression";
        case HirStmtKind::If: return "if";
        case HirStmtKind::While: return "while";
        case HirStmtKind::Repeat: return "repeat";
        case HirStmtKind::ForEach: return "forEach";
        }
        return "invalid";
    };
    const auto quote = [](const std::string& value) {
        std::string escaped{"\""};
        constexpr char hex[] = "0123456789abcdef";
        for (const unsigned char character : value) {
            switch (character) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20) {
                    escaped += "\\u00";
                    escaped += hex[(character >> 4) & 0x0f];
                    escaped += hex[character & 0x0f];
                } else {
                    escaped += static_cast<char>(character);
                }
            }
        }
        escaped += '"';
        return escaped;
    };
    const auto write_location = [&](const SourceLocation& location) {
        output << "{\"offset\":" << location.offset << ",\"line\":" << location.line
               << ",\"column\":" << location.column << '}';
    };
    const auto write_span = [&](const SourceSpan& span) {
        output << "{\"start\":";
        write_location(span.start);
        output << ",\"end\":";
        write_location(span.end);
        output << '}';
    };

    std::function<void(const HirExpr&)> write_expression;
    write_expression = [&](const HirExpr& expression) {
        output << "{\"kind\":" << quote(expression_kind_name(expression.kind))
               << ",\"type\":" << quote(expression.type)
               << ",\"span\":";
        write_span(expression.span);
        output << ",\"value\":" << quote(expression.value)
               << ",\"symbol\":" << quote(expression.symbol)
               << ",\"symbolId\":" << expression.symbol_id
               << ",\"typeSymbolId\":" << expression.type_symbol_id
               << ",\"left\":";
        if (expression.left) write_expression(*expression.left);
        else output << "null";
        output << ",\"right\":";
        if (expression.right) write_expression(*expression.right);
        else output << "null";
        output << ",\"arguments\":[";
        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
            if (index != 0) output << ',';
            write_expression(*expression.arguments[index]);
        }
        output << "],\"argumentNames\":[";
        for (std::size_t index = 0; index < expression.argument_names.size(); ++index) {
            if (index != 0) output << ',';
            output << quote(expression.argument_names[index]);
        }
        output << "]}";
    };

    std::function<void(const std::vector<std::unique_ptr<HirStmt>>&)> write_statements;
    write_statements = [&](const std::vector<std::unique_ptr<HirStmt>>& statements) {
        output << '[';
        for (std::size_t index = 0; index < statements.size(); ++index) {
            if (index != 0) output << ',';
            const auto& statement = *statements[index];
            output << "{\"kind\":" << quote(statement_kind_name(statement.kind))
                   << ",\"span\":";
            write_span(statement.span);
            output << ",\"name\":" << quote(statement.name)
                   << ",\"type\":" << quote(statement.type)
                   << ",\"symbolId\":" << statement.symbol_id
                   << ",\"typeSymbolId\":" << statement.type_symbol_id
                   << ",\"mutable\":" << (statement.mutable_binding ? "true" : "false")
                   << ",\"expression\":";
            if (statement.expression) write_expression(*statement.expression);
            else output << "null";
            output << ",\"body\":";
            write_statements(statement.body);
            output << ",\"elseBody\":";
            write_statements(statement.else_body);
            output << '}';
        }
        output << ']';
    };

    output << "{\"schemaVersion\":1,\"records\":[";
    for (std::size_t index = 0; index < program.records.size(); ++index) {
        if (index != 0) output << ',';
        const auto& record = program.records[index];
        output << "{\"name\":" << quote(record.name)
               << ",\"symbolId\":" << record.symbol_id << ",\"span\":";
        write_span(record.span);
        output << ",\"fields\":[";
        for (std::size_t field_index = 0; field_index < record.fields.size(); ++field_index) {
            if (field_index != 0) output << ',';
            const auto& field = record.fields[field_index];
            output << "{\"name\":" << quote(field.name)
                   << ",\"type\":" << quote(field.type)
                   << ",\"typeSymbolId\":" << field.type_symbol_id
                   << ",\"symbolId\":" << field.symbol_id << ",\"span\":";
            write_span(field.span);
            output << '}';
        }
        output << "]}";
    }
    output << "],\"functions\":[";
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        if (index != 0) output << ',';
        const auto& function = program.functions[index];
        output << "{\"name\":" << quote(function.name)
               << ",\"symbolId\":" << function.symbol_id
               << ",\"returnType\":" << quote(function.return_type)
               << ",\"returnTypeSymbolId\":" << function.return_type_symbol_id
               << ",\"span\":";
        write_span(function.span);
        output << ",\"parameters\":[";
        for (std::size_t parameter_index = 0;
             parameter_index < function.parameters.size(); ++parameter_index) {
            if (parameter_index != 0) output << ',';
            const auto& parameter = function.parameters[parameter_index];
            output << "{\"name\":" << quote(parameter.name)
                   << ",\"type\":" << quote(parameter.type)
                   << ",\"symbolId\":" << parameter.symbol_id
                   << ",\"typeSymbolId\":" << parameter.type_symbol_id
                   << ",\"span\":";
            write_span(parameter.span);
            output << '}';
        }
        output << "],\"body\":";
        write_statements(function.body);
        output << '}';
    }
    output << "],\"statements\":";
    write_statements(program.statements);
    output << '}';
    return output.str();
}

}