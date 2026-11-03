#include "indicode/ast_visitor.hpp"

namespace indicode {
namespace {

void walk_expression(Expr& expression, AstVisitor& visitor) {
    switch (expression.kind) {
    case ExprKind::Name: visitor.visit_name_expr(expression); break;
    case ExprKind::Literal: visitor.visit_literal_expr(expression); break;
    case ExprKind::Unary: visitor.visit_unary_expr(expression); break;
    case ExprKind::Binary: visitor.visit_binary_expr(expression); break;
    case ExprKind::Range: visitor.visit_range_expr(expression); break;
    case ExprKind::Call: visitor.visit_call_expr(expression); break;
    case ExprKind::ListLiteral: visitor.visit_list_literal_expr(expression); break;
    case ExprKind::Index: visitor.visit_index_expr(expression); break;
    case ExprKind::Member: visitor.visit_member_expr(expression); break;
    }

    if (expression.left) walk_expression(*expression.left, visitor);
    if (expression.right) walk_expression(*expression.right, visitor);
    for (auto& argument : expression.arguments) {
        walk_expression(*argument, visitor);
    }
}

void walk_statements(std::vector<std::unique_ptr<Stmt>>& statements,
                     AstVisitor& visitor) {
    for (auto& statement : statements) {
        switch (statement->kind) {
        case StmtKind::Variable: visitor.visit_variable_stmt(*statement); break;
        case StmtKind::Assignment: visitor.visit_assignment_stmt(*statement); break;
        case StmtKind::Return: visitor.visit_return_stmt(*statement); break;
        case StmtKind::Expression: visitor.visit_expression_stmt(*statement); break;
        case StmtKind::If: visitor.visit_if_stmt(*statement); break;
        case StmtKind::While: visitor.visit_while_stmt(*statement); break;
        case StmtKind::Repeat: visitor.visit_repeat_stmt(*statement); break;
        case StmtKind::ForEach: visitor.visit_for_each_stmt(*statement); break;
        }

        if (statement->expression) walk_expression(*statement->expression, visitor);
        walk_statements(statement->body, visitor);
        walk_statements(statement->else_body, visitor);
    }
}

}

void walk_ast(Program& program, AstVisitor& visitor) {
    visitor.visit_program(program);
    for (auto& record : program.records) {
        visitor.visit_record_decl(record);
        for (auto& field : record.fields) {
            visitor.visit_record_field(field);
        }
    }
    for (auto& function : program.functions) {
        visitor.visit_function_decl(function);
        for (auto& parameter : function.parameters) {
            visitor.visit_parameter(parameter);
        }
        walk_statements(function.body, visitor);
    }
    walk_statements(program.statements, visitor);
}

}
