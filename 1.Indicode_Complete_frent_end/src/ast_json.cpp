#include "indicode/ast.hpp"

#include <functional>
#include <sstream>

namespace indicode {
namespace {

const char* expression_kind_name(ExprKind kind) {
    switch (kind) {
    case ExprKind::Name: return "name";
    case ExprKind::Literal: return "literal";
    case ExprKind::Unary: return "unary";
    case ExprKind::Binary: return "binary";
    case ExprKind::Range: return "range";
    case ExprKind::Call: return "call";
    case ExprKind::ListLiteral: return "list";
    case ExprKind::Index: return "index";
    case ExprKind::Member: return "member";
    }
    return "invalid";
}

const char* statement_kind_name(StmtKind kind) {
    switch (kind) {
    case StmtKind::Variable: return "variable";
    case StmtKind::Assignment: return "assignment";
    case StmtKind::Return: return "return";
    case StmtKind::Expression: return "expression";
    case StmtKind::If: return "if";
    case StmtKind::While: return "while";
    case StmtKind::Repeat: return "repeat";
    case StmtKind::ForEach: return "forEach";
    }
    return "invalid";
}

std::string quote_json(const std::string& value) {
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
}

void write_location(std::ostringstream& output, const SourceLocation& location) {
    output << "{\"offset\":" << location.offset << ",\"line\":" << location.line
           << ",\"column\":" << location.column << '}';
}

void write_span(std::ostringstream& output, const SourceSpan& span) {
    output << "{\"start\":";
    write_location(output, span.start);
    output << ",\"end\":";
    write_location(output, span.end);
    output << '}';
}

}

std::string serialize_ast_json(const Program& program) {
    std::ostringstream output;
    std::function<void(const Expr&)> write_expression;
    write_expression = [&](const Expr& expression) {
        output << "{\"kind\":" << quote_json(expression_kind_name(expression.kind))
               << ",\"span\":";
        write_span(output, expression.span);
        output << ",\"value\":" << quote_json(expression.value)
               << ",\"inferredType\":" << quote_json(expression.inferred_type)
               << ",\"resolvedSymbol\":" << quote_json(expression.resolved_symbol)
               << ",\"resolvedSymbolId\":" << expression.resolved_symbol_id
               << ",\"resolvedTypeSymbolId\":" << expression.resolved_type_symbol_id
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
            output << quote_json(expression.argument_names[index]);
        }
        output << "]}";
    };

    std::function<void(const std::vector<std::unique_ptr<Stmt>>&)> write_statements;
    write_statements = [&](const std::vector<std::unique_ptr<Stmt>>& statements) {
        output << '[';
        for (std::size_t index = 0; index < statements.size(); ++index) {
            if (index != 0) output << ',';
            const auto& statement = *statements[index];
            output << "{\"kind\":" << quote_json(statement_kind_name(statement.kind))
                   << ",\"span\":";
            write_span(output, statement.span);
            output << ",\"name\":" << quote_json(statement.name)
                   << ",\"declaredType\":" << quote_json(statement.declared_type)
                   << ",\"resolvedSymbolId\":" << statement.resolved_symbol_id
                   << ",\"resolvedTypeSymbolId\":" << statement.resolved_type_symbol_id
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

    output << "{\"schemaVersion\":2,\"records\":[";
    for (std::size_t index = 0; index < program.records.size(); ++index) {
        if (index != 0) output << ',';
        const auto& record = program.records[index];
        output << "{\"name\":" << quote_json(record.name)
               << ",\"symbolId\":" << record.symbol_id << ",\"span\":";
        write_span(output, record.span);
        output << ",\"fields\":[";
        for (std::size_t field_index = 0; field_index < record.fields.size(); ++field_index) {
            if (field_index != 0) output << ',';
            const auto& field = record.fields[field_index];
            output << "{\"name\":" << quote_json(field.name)
                   << ",\"type\":" << quote_json(field.type)
                   << ",\"typeSymbolId\":" << field.type_symbol_id
                   << ",\"symbolId\":" << field.symbol_id << ",\"span\":";
            write_span(output, field.span);
            output << '}';
        }
        output << "]}";
    }

    output << "],\"functions\":[";
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        if (index != 0) output << ',';
        const auto& function = program.functions[index];
         output << "{\"name\":" << quote_json(function.name)
             << ",\"symbolId\":" << function.symbol_id
             << ",\"genericParameters\":[";
         for (std::size_t generic_index = 0;
              generic_index < function.generic_parameters.size(); ++generic_index) {
             if (generic_index != 0) output << ',';
             const auto& generic_parameter = function.generic_parameters[generic_index];
             output << "{\"name\":" << quote_json(generic_parameter.name)
                 << ",\"constraint\":" << quote_json(generic_parameter.constraint)
                 << ",\"span\":";
             write_span(output, generic_parameter.span);
             output << '}';
         }
         output << "],\"returnType\":" << quote_json(function.return_type)
               << ",\"returnTypeSymbolId\":" << function.return_type_symbol_id
               << ",\"span\":";
        write_span(output, function.span);
        output << ",\"hasEffectAnnotation\":"
               << (function.has_effect_annotation ? "true" : "false")
               << ",\"declaredEffects\":[";
        for (std::size_t effect_index = 0;
             effect_index < function.declared_effects.size(); ++effect_index) {
            if (effect_index != 0) output << ',';
            output << quote_json(function.declared_effects[effect_index]);
        }
        output << "],\"inferredEffects\":[";
        for (std::size_t effect_index = 0;
             effect_index < function.inferred_effects.size(); ++effect_index) {
            if (effect_index != 0) output << ',';
            output << quote_json(function.inferred_effects[effect_index]);
        }
        output << "],\"parameters\":[";
        for (std::size_t parameter_index = 0;
             parameter_index < function.parameters.size(); ++parameter_index) {
            if (parameter_index != 0) output << ',';
            const auto& parameter = function.parameters[parameter_index];
            output << "{\"name\":" << quote_json(parameter.name)
                   << ",\"type\":" << quote_json(parameter.type)
                     << ",\"ownershipMode\":" << quote_json(ownership_mode_name(parameter.ownership_mode))
                   << ",\"typeSymbolId\":" << parameter.resolved_type_symbol_id << ",\"span\":";
            write_span(output, parameter.span);
            output << ",\"resolvedSymbolId\":" << parameter.resolved_symbol_id << '}';
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
