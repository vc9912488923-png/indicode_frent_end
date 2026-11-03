#pragma once

#include "indicode/ast.hpp"

namespace indicode {

class AstVisitor {
public:
    virtual ~AstVisitor() = default;

    virtual void visit_program(Program& program) = 0;
    virtual void visit_record_decl(RecordDecl& record) = 0;
    virtual void visit_record_field(RecordField& field) = 0;
    virtual void visit_function_decl(FunctionDecl& function) = 0;
    virtual void visit_parameter(Parameter& parameter) = 0;

    virtual void visit_variable_stmt(Stmt& statement) = 0;
    virtual void visit_assignment_stmt(Stmt& statement) = 0;
    virtual void visit_return_stmt(Stmt& statement) = 0;
    virtual void visit_expression_stmt(Stmt& statement) = 0;
    virtual void visit_if_stmt(Stmt& statement) = 0;
    virtual void visit_while_stmt(Stmt& statement) = 0;
    virtual void visit_repeat_stmt(Stmt& statement) = 0;
    virtual void visit_for_each_stmt(Stmt& statement) = 0;

    virtual void visit_name_expr(Expr& expression) = 0;
    virtual void visit_literal_expr(Expr& expression) = 0;
    virtual void visit_unary_expr(Expr& expression) = 0;
    virtual void visit_binary_expr(Expr& expression) = 0;
    virtual void visit_range_expr(Expr& expression) = 0;
    virtual void visit_call_expr(Expr& expression) = 0;
    virtual void visit_list_literal_expr(Expr& expression) = 0;
    virtual void visit_index_expr(Expr& expression) = 0;
    virtual void visit_member_expr(Expr& expression) = 0;
};

void walk_ast(Program& program, AstVisitor& visitor);

}
