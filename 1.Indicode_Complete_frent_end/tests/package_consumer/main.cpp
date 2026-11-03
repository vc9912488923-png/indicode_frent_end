#include "indicode/ast_visitor.hpp"
#include "indicode/lexer.hpp"

namespace {

class ConsumerVisitor final : public indicode::AstVisitor {
public:
    void visit_program(indicode::Program&) override {}
    void visit_record_decl(indicode::RecordDecl&) override {}
    void visit_record_field(indicode::RecordField&) override {}
    void visit_function_decl(indicode::FunctionDecl&) override {}
    void visit_parameter(indicode::Parameter&) override {}
    void visit_variable_stmt(indicode::Stmt&) override {}
    void visit_assignment_stmt(indicode::Stmt&) override {}
    void visit_return_stmt(indicode::Stmt&) override {}
    void visit_expression_stmt(indicode::Stmt&) override {}
    void visit_if_stmt(indicode::Stmt&) override {}
    void visit_while_stmt(indicode::Stmt&) override {}
    void visit_repeat_stmt(indicode::Stmt&) override {}
    void visit_for_each_stmt(indicode::Stmt&) override {}
    void visit_name_expr(indicode::Expr&) override {}
    void visit_literal_expr(indicode::Expr&) override {}
    void visit_unary_expr(indicode::Expr&) override {}
    void visit_binary_expr(indicode::Expr&) override {}
    void visit_range_expr(indicode::Expr&) override {}
    void visit_call_expr(indicode::Expr&) override {}
    void visit_list_literal_expr(indicode::Expr&) override {}
    void visit_index_expr(indicode::Expr&) override {}
    void visit_member_expr(indicode::Expr&) override {}
};

}

int main() {
    const indicode::SourceFile source{"consumer.indi", "answer = 42\n"};
    const auto result = indicode::Lexer(source).lex();
    indicode::Program program;
    ConsumerVisitor visitor;
    indicode::walk_ast(program, visitor);
    return result.diagnostics.empty() &&
                   !result.tokens.empty() &&
                   result.tokens.back().kind == indicode::TokenKind::EndOfFile
        ? 0
        : 1;
}
