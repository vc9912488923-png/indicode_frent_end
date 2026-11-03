#include "indicode/sema.hpp"
#include "indicode/const_eval.hpp"
#include "indicode/effects.hpp"
#include "indicode/ownership.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace indicode {
namespace {

constexpr const char* error_type = "<error>";

std::uint64_t builtin_type_symbol_id(const std::string& type) {
    static const std::unordered_map<std::string, std::uint64_t> ids{
        {"Bool", 0x100001ULL}, {"Byte", 0x100002ULL},
        {"U8", 0x100003ULL}, {"Int2", 0x100004ULL},
        {"UInt2", 0x100005ULL}, {"Int", 0x100006ULL},
        {"UInt", 0x100007ULL}, {"Long", 0x100008ULL},
        {"ULong", 0x100009ULL}, {"Half", 0x10000aULL},
        {"Float", 0x10000bULL}, {"Double", 0x10000cULL},
        {"Char", 0x10000dULL}, {"Text", 0x10000eULL},
        {"Void", 0x10000fULL}
    };
    const auto found = ids.find(type);
    return found == ids.end() ? 0 : found->second;
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

bool is_integer_type(const std::string& type) {
    return type == "Byte" || type == "U8" || type == "Int2" || type == "UInt2" ||
           type == "Int" || type == "UInt" || type == "Long" || type == "ULong";
}

bool is_numeric_type(const std::string& type) {
    return is_integer_type(type) || type == "Half" || type == "Float" || type == "Double";
}

std::string get_list_element_type(const std::string& type) {
    constexpr std::string_view prefix = "List<";
    if (type.starts_with(prefix) && type.size() > prefix.size() && type.back() == '>') {
        return type.substr(prefix.size(), type.size() - prefix.size() - 1);
    }
    return {};
}

bool unify_type_pattern(const std::string& pattern, const std::string& actual,
                        const std::unordered_map<std::string, std::string>& generic_constraints,
                        std::unordered_map<std::string, std::string>& substitutions) {
    if (generic_constraints.contains(pattern)) {
        const auto [found, inserted] = substitutions.emplace(pattern, actual);
        return inserted || found->second == actual;
    }
    for (const auto& constructor : {std::string("List<"), std::string("Range<")}) {
        if (pattern.starts_with(constructor) && pattern.back() == '>' &&
            actual.starts_with(constructor) && actual.back() == '>') {
            return unify_type_pattern(
                pattern.substr(constructor.size(), pattern.size() - constructor.size() - 1),
                actual.substr(constructor.size(), actual.size() - constructor.size() - 1),
                generic_constraints, substitutions);
        }
    }
    return pattern == actual;
}

std::string substitute_generic_type(
    const std::string& type, const std::unordered_map<std::string, std::string>& substitutions) {
    if (const auto found = substitutions.find(type); found != substitutions.end()) {
        return found->second;
    }
    for (const auto& constructor : {std::string("List<"), std::string("Range<")}) {
        if (type.starts_with(constructor) && type.back() == '>') {
            return constructor + substitute_generic_type(
                                     type.substr(constructor.size(),
                                                 type.size() - constructor.size() - 1),
                                     substitutions) + ">";
        }
    }
    return type;
}

bool satisfies_generic_constraint(const std::string& constraint, const std::string& type) {
    if (constraint == "Numeric") return is_numeric_type(type);
    if (constraint == "Integer") return is_integer_type(type);
    return false;
}

std::string range_element_type(const std::string& type) {
    constexpr std::string_view prefix = "Range<";
    if (type.starts_with(prefix) && type.size() > prefix.size() && type.back() == '>') {
        return type.substr(prefix.size(), type.size() - prefix.size() - 1);
    }
    return {};
}

bool parse_integer_magnitude(const std::string& spelling, std::uint64_t& value) {
    std::string digits;
    digits.reserve(spelling.size());
    for (const char character : spelling) {
        if (character != '_') digits.push_back(character);
    }
    unsigned base = 10;
    std::size_t prefix_length = 0;
    if (digits.size() >= 2 && digits[0] == '0') {
        if (digits[1] == 'b' || digits[1] == 'B') { base = 2; prefix_length = 2; }
        else if (digits[1] == 'o' || digits[1] == 'O') { base = 8; prefix_length = 2; }
        else if (digits[1] == 'x' || digits[1] == 'X') { base = 16; prefix_length = 2; }
    }
    if (prefix_length == digits.size()) return false;
    const auto* begin = digits.data() + prefix_length;
    const auto* end = digits.data() + digits.size();
    const auto parsed = std::from_chars(begin, end, value, static_cast<int>(base));
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool is_integer_spelling(const std::string& spelling) {
    return spelling.size() < 2 || spelling[0] != '0' ||
           (spelling[1] != 'b' && spelling[1] != 'B' && spelling[1] != 'o' &&
            spelling[1] != 'O' && spelling[1] != 'x' && spelling[1] != 'X');
}

bool is_integer_literal_expression(const Expr& expression) {
    if (expression.kind == ExprKind::Literal) {
        return !expression.value.empty() && expression.value.front() >= '0' &&
               expression.value.front() <= '9';
    }
    const auto result = evaluate_constant_expression(expression);
    return result.value && std::holds_alternative<std::int64_t>(*result.value);
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

bool numeric_literal_value(const Expr& expression, long double& value) {
    if (expression.kind == ExprKind::Binary ||
        (expression.kind == ExprKind::Unary && expression.value != "-")) {
        const auto result = evaluate_constant_expression(expression);
        if (!result.value) return false;
        const auto* integer = std::get_if<std::int64_t>(&*result.value);
        if (integer == nullptr) return false;
        value = static_cast<long double>(*integer);
        return true;
    }
    const bool negative = expression.kind == ExprKind::Unary && expression.value == "-";
    const Expr& literal = negative ? *expression.left : expression;
    std::string spelling;
    spelling.reserve(literal.value.size());
    for (const char character : literal.value) {
        if (character != '_') spelling.push_back(character);
    }

    if (literal.inferred_type == "Float") {
        double parsed = 0;
        const auto result = std::from_chars(spelling.data(), spelling.data() + spelling.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != spelling.data() + spelling.size() ||
            !std::isfinite(parsed)) {
            return false;
        }
        value = parsed;
    } else {
        std::uint64_t magnitude = 0;
        if (!parse_integer_magnitude(spelling, magnitude)) {
            return false;
        }
        value = static_cast<long double>(magnitude);
    }
    if (negative) value = -value;
    return true;
}

bool numeric_literal_fits(const Expr& expression, const std::string& type) {
    long double value = 0;
    if (!numeric_literal_value(expression, value)) {
        return false;
    }

    if (type == "Half") {
        return std::fabs(value) <= 65504.0L;
    }
    if (type == "Float") {
        return std::fabs(value) <= static_cast<long double>(std::numeric_limits<float>::max());
    }
    if (type == "Double") {
        return std::fabs(value) <= static_cast<long double>(std::numeric_limits<double>::max());
    }
    if (std::trunc(value) != value) {
        return false;
    }
    long double minimum = 0;
    long double maximum = 0;
    if (type == "Byte" || type == "U8") maximum = 255.0L;
    else if (type == "Int2") { minimum = -32768.0L; maximum = 32767.0L; }
    else if (type == "UInt2") maximum = 65535.0L;
    else if (type == "Int") {
        minimum = static_cast<long double>(std::numeric_limits<std::int32_t>::min());
        maximum = static_cast<long double>(std::numeric_limits<std::int32_t>::max());
    } else if (type == "UInt") maximum = 4294967295.0L;
    else if (type == "Long") {
        minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
        maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    } else if (type == "ULong") {
        maximum = static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    } else {
        return false;
    }
    return value >= minimum && value <= maximum;
}

bool is_tensor_type(const std::string& type,
                    const std::function<bool(const std::string&)>& is_known_type) {
    constexpr std::string_view prefix = "Tensor<";
    if (!type.starts_with(prefix) || type.size() <= prefix.size() + 3 ||
        !type.ends_with("]>")) {
        return false;
    }
    const auto dimensions_start = type.find(",[", prefix.size());
    if (dimensions_start == std::string::npos) {
        return false;
    }
    const auto element_type = type.substr(prefix.size(), dimensions_start - prefix.size());
    if (!is_known_type(element_type) || !is_numeric_type(element_type)) {
        return false;
    }

    const auto dimensions_end = type.size() - 2;
    std::size_t start = dimensions_start + 2;
    while (start < dimensions_end) {
        const auto end = type.find(',', start);
        const auto stop = end == std::string::npos || end > dimensions_end ? dimensions_end : end;
        if (stop == start) return false;
        const auto dimension = type.substr(start, stop - start);
        const bool numeric = dimension.front() >= '0' && dimension.front() <= '9';
        if (numeric) {
            std::uint64_t value = 0;
            if (!parse_integer_magnitude(dimension, value)) return false;
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

bool statement_guarantees_return(const Stmt& statement);

bool statements_guarantee_return(const std::vector<std::unique_ptr<Stmt>>& statements) {
    for (const auto& statement : statements) {
        if (statement_guarantees_return(*statement)) return true;
    }
    return false;
}

bool statement_guarantees_return(const Stmt& statement) {
    return statement.kind == StmtKind::Return ||
           (statement.kind == StmtKind::If && !statement.else_body.empty() &&
            statements_guarantee_return(statement.body) &&
            statements_guarantee_return(statement.else_body));
}

bool integer_literal_fits(const Expr& expression, const std::string& type) {
    const bool negative = expression.kind == ExprKind::Unary;
    const Expr& literal = negative ? *expression.left : expression;
    std::uint64_t magnitude = 0;
    if (!parse_integer_magnitude(literal.value, magnitude)) {
        return false;
    }

    bool is_signed = false;
    std::uint64_t positive_limit = 0;
    std::uint64_t negative_limit = 0;
    if (type == "Byte" || type == "U8") positive_limit = 255;
    else if (type == "Int2") { is_signed = true; positive_limit = 32767; negative_limit = 32768; }
    else if (type == "UInt2") positive_limit = 65535;
    else if (type == "Int") { is_signed = true; positive_limit = 2147483647; negative_limit = 2147483648ULL; }
    else if (type == "UInt") positive_limit = 4294967295ULL;
    else if (type == "Long") {
        is_signed = true;
        positive_limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        negative_limit = positive_limit + 1;
    } else if (type == "ULong") {
        positive_limit = std::numeric_limits<std::uint64_t>::max();
    } else {
        return false;
    }

    return negative ? is_signed && magnitude <= negative_limit : magnitude <= positive_limit;
}

std::string literal_string_type(const std::string& spelling) {
    if (spelling.size() < 2) return "Text";
    std::size_t scalar_count = 0;
    for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
        const auto value = static_cast<unsigned char>(spelling[index]);
        if (value == '\\') {
            ++scalar_count;
            ++index;
            if (index + 1 < spelling.size() && spelling[index] == 'u' && spelling[index + 1] == '{') {
                index += 2;
                while (index + 1 < spelling.size() && spelling[index] != '}') ++index;
            }
        } else {
            ++scalar_count;
            if ((value & 0xe0) == 0xc0) index += 1;
            else if ((value & 0xf0) == 0xe0) index += 2;
            else if ((value & 0xf8) == 0xf0) index += 3;
        }
    }
    return scalar_count == 1 ? "Char" : "Text";
}

}

SemanticResult Analyzer::analyze(Program& program) {
    diagnostics_.clear();
    functions_.clear();
    records_.clear();
    generic_constraints_.clear();
    record_symbol_ids_.clear();
    record_field_symbol_ids_.clear();
    record_declaration_spans_.clear();
    record_field_declaration_spans_.clear();
    function_declaration_spans_.clear();
    scopes_.clear();
    next_symbol_id_ = 1;
    push_scope();

    for (auto& record : program.records) {
        record.symbol_id = 0;
        for (auto& field : record.fields) {
            field.type_symbol_id = 0;
            field.symbol_id = 0;
        }
        if (builtin_type_symbol_id(record.name) != 0 ||
            !records_.emplace(record.name, std::unordered_map<std::string, std::string>{}).second) {
            Diagnostic diagnostic{DiagnosticSeverity::Error, "E-SEMA-032",
                                  "record name is already declared", record.span};
            if (const auto previous = record_declaration_spans_.find(record.name);
                previous != record_declaration_spans_.end()) {
                diagnostic.notes.push_back({previous->second, "previous record declaration is here"});
            }
            diagnostics_.push_back(std::move(diagnostic));
        } else {
            record.symbol_id = next_symbol_id_++;
            record_symbol_ids_.emplace(record.name, record.symbol_id);
            record_declaration_spans_.emplace(record.name, record.span);
        }
    }
    for (auto& record : program.records) {
        if (record.symbol_id == 0) continue;
        auto found_record = records_.find(record.name);
        if (found_record == records_.end()) {
            continue;
        }
        for (auto& field : record.fields) {
            if (found_record->second.contains(field.name)) {
                Diagnostic diagnostic{DiagnosticSeverity::Error, "E-SEMA-033",
                                      "record field is already declared", field.span};
                const auto key = record.name + "." + field.name;
                if (const auto previous = record_field_declaration_spans_.find(key);
                    previous != record_field_declaration_spans_.end()) {
                    diagnostic.notes.push_back({previous->second, "previous field declaration is here"});
                }
                diagnostics_.push_back(std::move(diagnostic));
                continue;
            }
            if (!is_known_type(field.type) || field.type == "Void") {
                add_error("E-SEMA-034", "record field has an unknown or invalid type", field.span);
                continue;
            }
            field.type_symbol_id = type_symbol_id(field.type);
            field.symbol_id = next_symbol_id_++;
            const auto key = record.name + "." + field.name;
            record_field_symbol_ids_.emplace(key, field.symbol_id);
            record_field_declaration_spans_.emplace(key, field.span);
            found_record->second.emplace(field.name, field.type);
        }
    }

    for (auto& function : program.functions) {
        function.symbol_id = 0;
        function.return_type_symbol_id = 0;
        generic_constraints_.clear();
        for (const auto& parameter : function.generic_parameters) {
            if (generic_constraints_.contains(parameter.name)) {
                add_error("E-SEMA-053", "generic type parameter is already declared",
                          parameter.span);
                continue;
            }
            if (!parameter.constraint.empty() && parameter.constraint != "Numeric" &&
                parameter.constraint != "Integer") {
                add_error("E-SEMA-054", "unknown generic constraint", parameter.span);
            }
            generic_constraints_.emplace(parameter.name, parameter.constraint);
        }
        const auto previous_function = function_declaration_spans_.find(function.name);
        if (function.name == "say" || !numeric_conversion_target(function.name).empty() ||
            records_.contains(function.name) || functions_.contains(function.name)) {
            Diagnostic diagnostic{DiagnosticSeverity::Error, "E-SEMA-001",
                                  "function name is already declared", function.span};
            if (previous_function != function_declaration_spans_.end()) {
                diagnostic.notes.push_back({previous_function->second,
                                            "previous function declaration is here"});
            } else if (const auto previous_record = record_declaration_spans_.find(function.name);
                       previous_record != record_declaration_spans_.end()) {
                diagnostic.notes.push_back({previous_record->second,
                                            "conflicting record declaration is here"});
            }
            diagnostics_.push_back(std::move(diagnostic));
            continue;
        }
        function.symbol_id = next_symbol_id_++;
        FunctionSignature signature;
        signature.generic_parameters = function.generic_parameters;
        signature.return_type = function.return_type.empty() ? "Void" : function.return_type;
        signature.symbol_id = function.symbol_id;
        function.return_type_symbol_id = type_symbol_id(signature.return_type);
        for (auto& parameter : function.parameters) {
            parameter.resolved_type_symbol_id = 0;
            signature.parameter_names.push_back(parameter.name);
            signature.parameter_types.push_back(parameter.type);
            parameter.resolved_type_symbol_id = type_symbol_id(parameter.type);
            if (!is_known_type(parameter.type) || parameter.type == "Void") {
                add_error("E-SEMA-002", "unknown parameter type", parameter.span);
            }
        }
        if (!is_known_type(signature.return_type)) {
            add_error("E-SEMA-003", "unknown function return type", function.span);
        }
        functions_.emplace(function.name, std::move(signature));
        function_declaration_spans_.emplace(function.name, function.span);
    }
    generic_constraints_.clear();

    analyze_statements(program.statements);
    for (auto& function : program.functions) {
        push_scope();
        in_function_ = true;
        generic_constraints_.clear();
        for (const auto& parameter : function.generic_parameters) {
            generic_constraints_.emplace(parameter.name, parameter.constraint);
        }
        current_return_type_ = function.return_type.empty() ? "Void" : function.return_type;
        for (auto& parameter : function.parameters) {
            parameter.resolved_symbol_id =
                declare_binding(parameter.name,
                                {parameter.type,
                                 parameter.ownership_mode == OwnershipMode::BorrowMut,
                                 parameter.span}, parameter.span);
        }
        analyze_statements(function.body);
        if (current_return_type_ != "Void" && !always_returns(function.body)) {
            add_error("E-SEMA-011", "not all function paths return a value", function.span);
        }
        in_function_ = false;
        current_return_type_ = "Void";
        generic_constraints_.clear();
        pop_scope();
    }

    auto effect_result = EffectChecker{}.check(program);
    diagnostics_.insert(diagnostics_.end(),
                        std::make_move_iterator(effect_result.diagnostics.begin()),
                        std::make_move_iterator(effect_result.diagnostics.end()));
    auto ownership_result = OwnershipChecker{}.check(program);
    diagnostics_.insert(diagnostics_.end(),
                        std::make_move_iterator(ownership_result.diagnostics.begin()),
                        std::make_move_iterator(ownership_result.diagnostics.end()));

    pop_scope();
    return {std::move(diagnostics_)};
}

void Analyzer::add_error(const char* code, const char* message, SourceSpan span) {
    diagnostics_.push_back({DiagnosticSeverity::Error, code, message, span});
}

bool Analyzer::is_known_type(const std::string& type) const {
    if (generic_constraints_.contains(type)) return true;
    if (type.starts_with("Tensor<")) {
        return is_tensor_type(type, [this](const std::string& element) {
            return is_known_type(element);
        });
    }
    if (const auto element_type = list_element_type(type); !element_type.empty()) {
        return is_known_type(element_type) && element_type != "Void";
    }
    if (const auto element_type = range_element_type(type); !element_type.empty()) {
        return is_integer_type(element_type);
    }
    return records_.contains(type) || type == "Bool" || type == "Byte" || type == "U8" || type == "Int2" ||
           type == "UInt2" || type == "Int" || type == "UInt" || type == "Long" ||
           type == "ULong" || type == "Half" || type == "Float" || type == "Double" ||
           type == "Char" || type == "Text" || type == "Void";
}

std::uint64_t Analyzer::type_symbol_id(const std::string& type) const {
    if (const auto builtin_id = builtin_type_symbol_id(type); builtin_id != 0) {
        return builtin_id;
    }
    if (const auto record = record_symbol_ids_.find(type); record != record_symbol_ids_.end()) {
        return record->second;
    }
    for (const auto& prefix : {std::string("List<"), std::string("Range<")}) {
        if (type.starts_with(prefix) && type.size() > prefix.size() && type.back() == '>') {
            return type_symbol_id(type.substr(prefix.size(), type.size() - prefix.size() - 1));
        }
    }
    if (type.starts_with("Tensor<")) {
        const auto dimensions_start = type.find(",[", 7);
        if (dimensions_start != std::string::npos) {
            return type_symbol_id(type.substr(7, dimensions_start - 7));
        }
    }
    return 0;
}

std::string Analyzer::list_element_type(const std::string& type) const {
    return get_list_element_type(type);
}

void Analyzer::push_scope() {
    scopes_.emplace_back();
}

void Analyzer::pop_scope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

Analyzer::Binding* Analyzer::find_binding(const std::string& name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(name);
        if (found != scope->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

std::uint64_t Analyzer::declare_binding(const std::string& name, Binding binding, SourceSpan span) {
    if (scopes_.back().contains(name)) {
        Diagnostic diagnostic{DiagnosticSeverity::Error, "E-SEMA-004",
                              "name is already declared in this scope", span};
        diagnostic.notes.push_back({scopes_.back().at(name).declaration_span,
                                    "previous declaration is here"});
        diagnostics_.push_back(std::move(diagnostic));
        return 0;
    }
    binding.symbol_id = next_symbol_id_++;
    const auto symbol_id = binding.symbol_id;
    scopes_.back().emplace(name, std::move(binding));
    return symbol_id;
}

void Analyzer::analyze_statements(std::vector<std::unique_ptr<Stmt>>& statements) {
    bool unreachable_reported = false;
    bool terminates = false;
    for (auto& statement : statements) {
        if (terminates && !unreachable_reported) {
            add_error("E-SEMA-050", "unreachable statement", statement->span);
            unreachable_reported = true;
        }
        analyze_statement(*statement);
        terminates = terminates || statement_guarantees_return(*statement);
    }
}

void Analyzer::analyze_statement(Stmt& statement) {
    switch (statement.kind) {
    case StmtKind::Variable: {
            const bool has_explicit_type = !statement.declared_type.empty();
            const auto type = statement.declared_type;
        std::string value_type;
            if (has_explicit_type && !list_element_type(type).empty() &&
                statement.expression->kind == ExprKind::ListLiteral &&
                statement.expression->arguments.empty()) {
                statement.expression->inferred_type = type;
                statement.expression->resolved_type_symbol_id = type_symbol_id(type);
                value_type = type;
            } else if (has_explicit_type && is_integer_type(type) &&
            is_integer_literal_expression(*statement.expression)) {
            if (!type_integer_literal(*statement.expression, type)) {
                add_error("E-SEMA-022", "integer literal is outside the declared type range",
                          statement.expression->span);
                statement.expression->inferred_type = type;
            }
            value_type = type;
        } else {
            value_type = analyze_expression(*statement.expression);
        }
            if (has_explicit_type && (!is_known_type(type) || type == "Void")) {
            add_error("E-SEMA-002", "unknown variable type", statement.span);
            } else if (has_explicit_type && type != value_type && value_type != error_type) {
            add_error("E-SEMA-005", "initializer type does not match declared type", statement.span);
        }
            const auto resolved_type = has_explicit_type ? type : value_type;
            statement.declared_type = resolved_type;
            statement.resolved_symbol_id = declare_binding(
                statement.name, {resolved_type, statement.mutable_binding, statement.span}, statement.span);
            statement.resolved_type_symbol_id = type_symbol_id(resolved_type);
        break;
    }
    case StmtKind::Assignment: {
        const auto value_type = analyze_expression(*statement.expression);
        auto* binding = find_binding(statement.name);
        if (binding == nullptr) {
            statement.declared_type = value_type;
            statement.resolved_symbol_id =
                declare_binding(statement.name, {value_type, true, statement.span}, statement.span);
        } else {
            statement.resolved_symbol_id = binding->symbol_id;
            statement.declared_type = binding->type;
            if (!binding->mutable_binding) {
                add_error("E-SEMA-006", "cannot assign to an immutable binding", statement.span);
            } else if (binding->type != value_type && value_type != error_type) {
                add_error("E-SEMA-005", "assignment type does not match variable type", statement.span);
            }
        }
        statement.resolved_type_symbol_id = type_symbol_id(statement.declared_type);
        break;
    }
    case StmtKind::Return: {
        if (!in_function_) {
            add_error("E-SEMA-007", "return is only valid inside a function", statement.span);
            break;
        }
        const auto value_type = statement.expression ? analyze_expression(*statement.expression) : "Void";
        if (value_type != current_return_type_ && value_type != error_type) {
            add_error("E-SEMA-008", "return value does not match function return type", statement.span);
        }
        break;
    }
    case StmtKind::Expression:
        analyze_expression(*statement.expression);
        break;
    case StmtKind::If: {
        const auto condition_type = analyze_expression(*statement.expression);
        if (condition_type != "Bool" && condition_type != error_type) {
            add_error("E-SEMA-009", "if condition must have type Bool", statement.expression->span);
        }
        push_scope();
        analyze_statements(statement.body);
        pop_scope();
        if (!statement.else_body.empty()) {
            push_scope();
            analyze_statements(statement.else_body);
            pop_scope();
        }
        break;
    }
    case StmtKind::While: {
        const auto condition_type = analyze_expression(*statement.expression);
        if (condition_type != "Bool" && condition_type != error_type) {
            add_error("E-SEMA-009", "while condition must have type Bool", statement.expression->span);
        }
        push_scope();
        analyze_statements(statement.body);
        pop_scope();
        break;
    }
    case StmtKind::Repeat: {
        const auto count_type = analyze_expression(*statement.expression);
        if (!is_integer_type(count_type) && count_type != error_type) {
            add_error("E-SEMA-010", "repeat count must be an integer", statement.expression->span);
        }
        push_scope();
        analyze_statements(statement.body);
        pop_scope();
        break;
    }
    case StmtKind::ForEach: {
        const auto iterable_type = analyze_expression(*statement.expression);
        auto element_type = iterable_type == "Text" ? "Char" : list_element_type(iterable_type);
        if (element_type.empty()) {
            element_type = range_element_type(iterable_type);
        }
        if (element_type.empty()) {
            if (iterable_type != error_type) {
                add_error("E-SEMA-023", "for-each requires a List<T>, Range<T>, or Text value",
                          statement.expression->span);
            }
            break;
        }
        statement.declared_type = element_type;
        statement.resolved_type_symbol_id = type_symbol_id(element_type);
        push_scope();
        statement.resolved_symbol_id =
            declare_binding(statement.name, {element_type, false, statement.span}, statement.span);
        analyze_statements(statement.body);
        pop_scope();
        break;
    }
    }
}

std::string Analyzer::analyze_expression(Expr& expression) {
    std::string type = error_type;
    switch (expression.kind) {
    case ExprKind::Literal:
        if (expression.value == "true" || expression.value == "false") {
            type = "Bool";
        } else if (!expression.value.empty() && expression.value.front() == '"') {
            type = literal_string_type(expression.value);
        } else if (is_integer_spelling(expression.value) &&
                   expression.value.find_first_of(".eE") != std::string::npos) {
            type = "Float";
            expression.inferred_type = type;
            long double value = 0;
            if (!numeric_literal_value(expression, value)) {
                add_error("E-SEMA-012", "floating-point literal is outside the supported range",
                          expression.span);
                type = error_type;
            }
        } else {
            std::uint64_t magnitude = 0;
            if (!parse_integer_magnitude(expression.value, magnitude)) {
                add_error("E-SEMA-012", "integer literal is outside the supported range", expression.span);
            } else if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
                add_error("E-SEMA-012", "integer literal is outside the Int range", expression.span);
            } else {
                type = "Int";
            }
        }
        break;
    case ExprKind::Name: {
        auto* binding = find_binding(expression.value);
        if (binding == nullptr) {
            add_error("E-SEMA-013", "unknown variable name", expression.span);
        } else {
            type = binding->type;
            expression.resolved_symbol = expression.value;
            expression.resolved_symbol_id = binding->symbol_id;
        }
        break;
    }
    case ExprKind::Unary: {
        const auto operand_type = analyze_expression(*expression.left);
        if (expression.value == "move" || expression.value == "borrow" ||
            expression.value == "borrow_mut") {
            type = operand_type;
        } else if (expression.value == "!" && operand_type == "Bool") {
            type = "Bool";
        } else if ((expression.value == "+" || expression.value == "-") &&
                   (is_numeric_type(operand_type) ||
                    (generic_constraints_.contains(operand_type) &&
                     (generic_constraints_.at(operand_type) == "Numeric" ||
                      generic_constraints_.at(operand_type) == "Integer")))) {
            type = operand_type;
        } else if (expression.value == "~" &&
                   (is_integer_type(operand_type) ||
                    (generic_constraints_.contains(operand_type) &&
                     generic_constraints_.at(operand_type) == "Integer"))) {
            type = operand_type;
        } else if (operand_type != error_type) {
            add_error("E-SEMA-014", "invalid operand type for unary operator", expression.span);
        }
        break;
    }
    case ExprKind::Binary: {
        const auto left_type = analyze_expression(*expression.left);
        const auto right_type = analyze_expression(*expression.right);
        if (expression.value == "&&" || expression.value == "||") {
            if (left_type == "Bool" && right_type == "Bool") {
                type = "Bool";
            } else if (left_type != error_type && right_type != error_type) {
                add_error("E-SEMA-015", "logical operators require Bool operands", expression.span);
            }
        } else if (expression.value == "==" || expression.value == "!=" ||
                   expression.value == "<" || expression.value == "<=" ||
                   expression.value == ">" || expression.value == ">=") {
            if (left_type == right_type && left_type != error_type &&
                                (is_numeric_type(left_type) ||
                                 (generic_constraints_.contains(left_type) &&
                                    (generic_constraints_.at(left_type) == "Numeric" ||
                                     generic_constraints_.at(left_type) == "Integer")) ||
                                 left_type == "Char" || left_type == "Text" ||
                 ((expression.value == "==" || expression.value == "!=") &&
                  left_type == "Bool"))) {
                type = "Bool";
            } else if (left_type != error_type && right_type != error_type) {
                add_error("E-SEMA-016", "comparison operands have incompatible types", expression.span);
            }
        } else {
            type = numeric_binary_type(expression, left_type, right_type);
        }
        break;
    }
    case ExprKind::Range: {
        const auto start_type = analyze_expression(*expression.left);
        const auto end_type = analyze_expression(*expression.right);
          if ((is_integer_type(start_type) ||
             (generic_constraints_.contains(start_type) &&
              generic_constraints_.at(start_type) == "Integer")) && start_type == end_type) {
            type = "Range<" + start_type + ">";
        } else if (start_type != error_type && end_type != error_type) {
            add_error("E-SEMA-045", "range bounds must have the same integer type", expression.span);
        }
        break;
    }
    case ExprKind::Call: {
        std::vector<std::string> argument_types;
        for (auto& argument : expression.arguments) {
            argument_types.push_back(analyze_expression(*argument));
        }
        if (expression.value == "say") {
            if (argument_types.size() != 1 || argument_types[0] == "Void" ||
                (!expression.argument_names.empty() && !expression.argument_names[0].empty())) {
                add_error("E-SEMA-017", "say expects exactly one non-Void argument", expression.span);
            } else {
                type = "Void";
                expression.resolved_symbol = expression.value;
                expression.resolved_symbol_id = builtin_function_symbol_id(expression.value);
            }
        } else {
            const auto function = functions_.find(expression.value);
            if (function == functions_.end()) {
                const auto conversion_type = numeric_conversion_target(expression.value);
                if (!conversion_type.empty()) {
                    const bool valid_shape = argument_types.size() == 2 &&
                        expression.argument_names.size() == 2 &&
                        expression.argument_names[0].empty() &&
                        expression.argument_names[1] == "checked" &&
                        expression.arguments[1]->kind == ExprKind::Literal &&
                        expression.arguments[1]->value == "true";
                    const bool valid_source = !argument_types.empty() &&
                                              is_numeric_type(argument_types[0]);
                    if (!valid_shape || !valid_source) {
                        add_error("E-SEMA-044",
                                  "integer conversion expects a numeric value and checked: true",
                                  expression.span);
                    } else if (is_integer_literal_expression(*expression.arguments[0]) &&
                               !numeric_literal_fits(*expression.arguments[0], conversion_type)) {
                        add_error("E-SEMA-022",
                                  "integer literal is outside the conversion target range",
                                  expression.arguments[0]->span);
                    } else {
                        type = conversion_type;
                        expression.resolved_symbol = expression.value;
                        expression.resolved_symbol_id = builtin_function_symbol_id(expression.value);
                    }
                } else if (const auto record = records_.find(expression.value);
                           record == records_.end()) {
                    add_error("E-SEMA-018", "unknown function", expression.span);
                } else {
                    std::vector<bool> assigned(record->second.size(), false);
                    bool fields_match = argument_types.size() == record->second.size();
                    for (std::size_t index = 0; index < argument_types.size(); ++index) {
                        const std::string field_name = index < expression.argument_names.size()
                            ? expression.argument_names[index] : "";
                        if (field_name.empty()) {
                            add_error("E-SEMA-037", "record construction requires named fields",
                                      expression.arguments[index]->span);
                            fields_match = false;
                            continue;
                        }
                        std::size_t field_index = 0;
                        auto field = record->second.begin();
                        while (field != record->second.end() && field->first != field_name) {
                            ++field;
                            ++field_index;
                        }
                        if (field == record->second.end()) {
                            add_error("E-SEMA-038", "unknown record field", expression.arguments[index]->span);
                            fields_match = false;
                            continue;
                        }
                        if (assigned[field_index]) {
                            add_error("E-SEMA-039", "record field was supplied more than once",
                                      expression.arguments[index]->span);
                            fields_match = false;
                            continue;
                        }
                        assigned[field_index] = true;
                        if (argument_types[index] != error_type && argument_types[index] != field->second) {
                            fields_match = false;
                        }
                    }
                    for (const bool field_assigned : assigned) {
                        if (!field_assigned) {
                            add_error("E-SEMA-040", "missing required record field", expression.span);
                            fields_match = false;
                        }
                    }
                    if (fields_match) {
                        type = expression.value;
                        expression.resolved_symbol = expression.value;
                        expression.resolved_symbol_id = record_symbol_ids_.at(expression.value);
                    } else {
                        add_error("E-SEMA-041", "record field value does not match its declared type",
                                  expression.span);
                    }
                }
            } else if (function->second.parameter_types.size() != argument_types.size()) {
                add_error("E-SEMA-019", "function argument count does not match signature", expression.span);
            } else {
                std::vector<bool> assigned(argument_types.size(), false);
                bool arguments_match = true;
                std::size_t next_positional = 0;
                std::unordered_map<std::string, std::string> substitutions;
                std::unordered_map<std::string, std::string> generic_constraints;
                for (const auto& parameter : function->second.generic_parameters) {
                    generic_constraints.emplace(parameter.name, parameter.constraint);
                }
                for (std::size_t index = 0; index < argument_types.size(); ++index) {
                    std::size_t parameter_index = argument_types.size();
                    const std::string argument_name = index < expression.argument_names.size()
                        ? expression.argument_names[index]
                        : "";
                    if (argument_name.empty()) {
                        while (next_positional < assigned.size() && assigned[next_positional]) ++next_positional;
                        if (next_positional < assigned.size()) parameter_index = next_positional++;
                    } else {
                        for (std::size_t candidate = 0; candidate < function->second.parameter_names.size(); ++candidate) {
                            if (function->second.parameter_names[candidate] == argument_name) {
                                parameter_index = candidate;
                                break;
                            }
                        }
                        if (parameter_index == argument_types.size()) {
                            add_error("E-SEMA-028", "unknown named argument", expression.arguments[index]->span);
                            arguments_match = false;
                            continue;
                        }
                    }
                    if (parameter_index >= assigned.size()) {
                        add_error("E-SEMA-029", "too many positional arguments", expression.arguments[index]->span);
                        arguments_match = false;
                        continue;
                    }
                    if (assigned[parameter_index]) {
                        add_error("E-SEMA-030", "function parameter was supplied more than once",
                                  expression.arguments[index]->span);
                        arguments_match = false;
                        continue;
                    }
                    assigned[parameter_index] = true;
                    if (argument_types[index] != error_type) {
                        if (!unify_type_pattern(
                                function->second.parameter_types[parameter_index],
                                argument_types[index], generic_constraints, substitutions)) {
                            arguments_match = false;
                        }
                    }
                }
                for (std::size_t index = 0; index < assigned.size(); ++index) {
                    if (!assigned[index]) {
                        add_error("E-SEMA-031", "missing required function argument", expression.span);
                        arguments_match = false;
                    }
                }
                for (const auto& parameter : function->second.generic_parameters) {
                    const auto inferred = substitutions.find(parameter.name);
                    if (inferred == substitutions.end()) {
                        if (arguments_match) {
                            add_error("E-SEMA-055", "cannot infer generic type parameter",
                                      expression.span);
                        }
                        arguments_match = false;
                    } else if (!parameter.constraint.empty() &&
                               !satisfies_generic_constraint(parameter.constraint,
                                                             inferred->second)) {
                        add_error("E-SEMA-056", "generic argument does not satisfy its constraint",
                                  expression.span);
                        arguments_match = false;
                    }
                }
                if (!arguments_match) {
                    add_error("E-SEMA-020", "function argument type does not match signature", expression.span);
                } else {
                    type = substitute_generic_type(function->second.return_type, substitutions);
                    expression.resolved_symbol = expression.value;
                    expression.resolved_symbol_id = function->second.symbol_id;
                }
            }
        }
        break;
    }
    case ExprKind::ListLiteral: {
        if (expression.arguments.empty()) {
            add_error("E-SEMA-024", "an empty list needs an explicit element type", expression.span);
            break;
        }
        const auto element_type = analyze_expression(*expression.arguments.front());
        bool homogeneous = element_type != error_type && element_type != "Void";
        for (std::size_t index = 1; index < expression.arguments.size(); ++index) {
            const auto current_type = analyze_expression(*expression.arguments[index]);
            homogeneous = homogeneous && current_type == element_type;
        }
        if (!homogeneous) {
            add_error("E-SEMA-025", "all list elements must have the same non-Void type", expression.span);
        } else {
            type = "List<" + element_type + ">";
        }
        break;
    }
    case ExprKind::Index: {
        const auto collection_type = analyze_expression(*expression.left);
        const auto index_type = analyze_expression(*expression.right);
        if (index_type != "Int" && index_type != error_type) {
            add_error("E-SEMA-026", "list index must have type Int", expression.right->span);
        }
        const auto element_type = list_element_type(collection_type);
        if (!element_type.empty()) {
            type = element_type;
        } else if (collection_type == "Text") {
            type = "Char";
        } else if (collection_type != error_type) {
            add_error("E-SEMA-027", "indexing requires a List<T> or Text value", expression.left->span);
        }
        break;
    }
    case ExprKind::Member: {
        const auto base_type = analyze_expression(*expression.left);
        const auto record = records_.find(base_type);
        if (record == records_.end()) {
            if (base_type != error_type) {
                add_error("E-SEMA-042", "member access requires a record value", expression.left->span);
            }
        } else {
            const auto field = record->second.find(expression.value);
            if (field == record->second.end()) {
                add_error("E-SEMA-043", "record has no field with this name", expression.span);
            } else {
                type = field->second;
                expression.resolved_symbol = base_type + "." + expression.value;
                expression.resolved_symbol_id =
                    record_field_symbol_ids_.at(expression.resolved_symbol);
            }
        }
        break;
    }
    }
    if (is_integer_type(type) &&
        (expression.kind == ExprKind::Binary || expression.kind == ExprKind::Unary)) {
        const auto result = evaluate_constant_expression(expression);
        if (result.error == ConstantEvaluationError::Overflow ||
            result.error == ConstantEvaluationError::DivisionByZero) {
            add_error("E-SEMA-051",
                      result.error == ConstantEvaluationError::DivisionByZero
                          ? "constant expression divides by zero"
                          : "constant integer expression overflows",
                      result.span);
            type = error_type;
        } else if (result.error == ConstantEvaluationError::StepLimit) {
            add_error("E-SEMA-052", "constant expression exceeds the evaluation limit", result.span);
            type = error_type;
        } else if (result.value &&
                   std::holds_alternative<std::int64_t>(*result.value) &&
                   !numeric_literal_fits(expression, type)) {
            add_error("E-SEMA-022", "integer expression is outside its type range", expression.span);
            type = error_type;
        }
    }
    expression.inferred_type = type;
    expression.resolved_type_symbol_id = type_symbol_id(type);
    return type;
}

bool Analyzer::type_integer_literal(Expr& expression, const std::string& type) {
    const bool negative = expression.kind == ExprKind::Unary;
    Expr& literal = negative ? *expression.left : expression;
    if (!integer_literal_fits(expression, type)) {
        return false;
    }
    literal.inferred_type = type;
    literal.resolved_type_symbol_id = type_symbol_id(type);
    expression.inferred_type = type;
    expression.resolved_type_symbol_id = type_symbol_id(type);
    return true;
}

std::string Analyzer::numeric_binary_type(Expr& expression, const std::string& left,
                                          const std::string& right) {
    const auto generic_numeric = [&](const std::string& type) {
        const auto constraint = generic_constraints_.find(type);
        return constraint != generic_constraints_.end() &&
               (constraint->second == "Numeric" || constraint->second == "Integer");
    };
    if (left == right && (is_numeric_type(left) || generic_numeric(left))) {
        return left;
    }
    if (left != error_type && right != error_type) {
        add_error("E-SEMA-021", "arithmetic operands must have the same numeric type", expression.span);
    }
    return error_type;
}

bool Analyzer::always_returns(const std::vector<std::unique_ptr<Stmt>>& statements) const {
    return statements_guarantee_return(statements);
}

}