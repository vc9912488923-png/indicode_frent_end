#include "indicode/ast.hpp"

#include <sstream>
#include <string>

namespace indicode {
namespace {

void indent(std::ostringstream& output, std::size_t depth) {
    for (std::size_t index = 0; index < depth; ++index) {
        output << "  ";
    }
}

void print_expression(std::ostringstream& output, const Expr& expression, bool include_types) {
    switch (expression.kind) {
    case ExprKind::Name:
        output << "Name(" << expression.value << ')';
        break;
    case ExprKind::Literal:
        output << "Literal(" << expression.value << ')';
        break;
    case ExprKind::Unary:
        output << "Unary(" << expression.value << ' ';
        print_expression(output, *expression.left, include_types);
        output << ')';
        break;
    case ExprKind::Binary:
        output << "Binary(" << expression.value << ' ';
        print_expression(output, *expression.left, include_types);
        output << ' ';
        print_expression(output, *expression.right, include_types);
        output << ')';
        break;
    case ExprKind::Range:
        output << "Range(";
        print_expression(output, *expression.left, include_types);
        output << " .. ";
        print_expression(output, *expression.right, include_types);
        output << ')';
        break;
    case ExprKind::Call:
        output << "Call(" << expression.value;
        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
            output << ' ';
            if (index < expression.argument_names.size() && !expression.argument_names[index].empty()) {
                output << expression.argument_names[index] << ':';
            }
            print_expression(output, *expression.arguments[index], include_types);
        }
        output << ')';
        break;
    case ExprKind::ListLiteral:
        output << "List[";
        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
            if (index != 0) output << ", ";
            print_expression(output, *expression.arguments[index], include_types);
        }
        output << ']';
        break;
    case ExprKind::Index:
        output << "Index(";
        print_expression(output, *expression.left, include_types);
        output << ", ";
        print_expression(output, *expression.right, include_types);
        output << ')';
        break;
    case ExprKind::Member:
        print_expression(output, *expression.left, include_types);
        output << '.' << expression.value;
        break;
    }
    if (include_types && !expression.inferred_type.empty()) {
        output << ':' << expression.inferred_type;
    }
}

void print_statements(std::ostringstream& output,
                      const std::vector<std::unique_ptr<Stmt>>& statements,
                      std::size_t depth, bool include_types) {
    for (const auto& statement : statements) {
        indent(output, depth);
        switch (statement->kind) {
        case StmtKind::Variable:
            output << (statement->mutable_binding ? "var " : "let ");
            if (!statement->declared_type.empty()) {
                output << statement->declared_type << ' ';
            }
            output << statement->name << " = ";
            print_expression(output, *statement->expression, include_types);
            output << '\n';
            break;
        case StmtKind::Assignment:
            output << statement->name;
            if (include_types && !statement->declared_type.empty()) {
                output << ':' << statement->declared_type;
            }
            output << " = ";
            print_expression(output, *statement->expression, include_types);
            output << '\n';
            break;
        case StmtKind::Return:
            output << "return";
            if (statement->expression) {
                output << ' ';
                print_expression(output, *statement->expression, include_types);
            }
            output << '\n';
            break;
        case StmtKind::Expression:
            print_expression(output, *statement->expression, include_types);
            output << '\n';
            break;
        case StmtKind::If:
            output << "if ";
            print_expression(output, *statement->expression, include_types);
            output << "\n";
            print_statements(output, statement->body, depth + 1, include_types);
            if (!statement->else_body.empty()) {
                indent(output, depth);
                output << "else\n";
                print_statements(output, statement->else_body, depth + 1, include_types);
            }
            break;
        case StmtKind::While:
            output << "while ";
            print_expression(output, *statement->expression, include_types);
            output << "\n";
            print_statements(output, statement->body, depth + 1, include_types);
            break;
        case StmtKind::Repeat:
            output << "repeat ";
            print_expression(output, *statement->expression, include_types);
            output << " times\n";
            print_statements(output, statement->body, depth + 1, include_types);
            break;
        case StmtKind::ForEach:
            output << "for " << statement->name << " in ";
            print_expression(output, *statement->expression, include_types);
            output << "\n";
            print_statements(output, statement->body, depth + 1, include_types);
            break;
        }
    }
}

}

std::string print_program(const Program& program, bool include_types) {
    std::ostringstream output;
    for (const auto& record : program.records) {
        output << "record " << record.name << " {\n";
        for (const auto& field : record.fields) {
            output << "  " << field.name << ": " << field.type << '\n';
        }
        output << "}\n";
    }
    for (const auto& function : program.functions) {
        output << "function " << function.name << '(';
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0) {
                output << ", ";
            }
            output << function.parameters[index].name << ": " << function.parameters[index].type;
        }
        output << ')';
        if (!function.return_type.empty()) {
            output << " -> " << function.return_type;
        }
        output << "\n";
        print_statements(output, function.body, 1, include_types);
    }
    print_statements(output, program.statements, 0, include_types);
    return output.str();
}

}