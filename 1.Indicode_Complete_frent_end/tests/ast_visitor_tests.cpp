#include "indicode/ast_visitor.hpp"
#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

indicode::Program parse_owned_program() {
    const indicode::SourceFile source{
        "visitor.indi",
        "record Point { x: Int }\n"
        "function inspect(point: Point, items: List<Int>) -> Int {\n"
        "  let local: Int = 0\n"
        "  var temp = +local\n"
        "  temp = temp + ~local\n"
        "  if true { return point.x } else { return items[0] }\n"
        "  while false { temp = temp - 1 }\n"
        "  repeat 1 times { call(temp) }\n"
        "  for value in 0..2 { call(value) }\n"
        "}\n"
        "items = [1, 2]\n"
        "indexed = items[0]\n"
        "answer = inspect(point: Point(x: 1), items: items)\n"
    };
    auto lexed = indicode::Lexer(source).lex();
    require(lexed.diagnostics.empty(), "visitor fixture should lex cleanly");
    auto parsed = indicode::Parser(lexed.tokens).parse();
    require(parsed.diagnostics.empty(), "visitor fixture should parse cleanly");
    return std::move(parsed.program);
}

class CountingVisitor final : public indicode::AstVisitor {
public:
    std::size_t program_count = 0;
    std::size_t record_count = 0;
    std::size_t field_count = 0;
    std::size_t function_count = 0;
    std::size_t parameter_count = 0;
    std::array<std::size_t, 8> statement_counts{};
    std::array<std::size_t, 9> expression_counts{};

    void visit_program(indicode::Program&) override { ++program_count; }
    void visit_record_decl(indicode::RecordDecl&) override { ++record_count; }
    void visit_record_field(indicode::RecordField&) override { ++field_count; }
    void visit_function_decl(indicode::FunctionDecl&) override { ++function_count; }
    void visit_parameter(indicode::Parameter&) override { ++parameter_count; }

    void visit_variable_stmt(indicode::Stmt&) override { count(indicode::StmtKind::Variable); }
    void visit_assignment_stmt(indicode::Stmt&) override { count(indicode::StmtKind::Assignment); }
    void visit_return_stmt(indicode::Stmt&) override { count(indicode::StmtKind::Return); }
    void visit_expression_stmt(indicode::Stmt&) override { count(indicode::StmtKind::Expression); }
    void visit_if_stmt(indicode::Stmt&) override { count(indicode::StmtKind::If); }
    void visit_while_stmt(indicode::Stmt&) override { count(indicode::StmtKind::While); }
    void visit_repeat_stmt(indicode::Stmt&) override { count(indicode::StmtKind::Repeat); }
    void visit_for_each_stmt(indicode::Stmt&) override { count(indicode::StmtKind::ForEach); }

    void visit_name_expr(indicode::Expr&) override { count(indicode::ExprKind::Name); }
    void visit_literal_expr(indicode::Expr&) override { count(indicode::ExprKind::Literal); }
    void visit_unary_expr(indicode::Expr&) override { count(indicode::ExprKind::Unary); }
    void visit_binary_expr(indicode::Expr&) override { count(indicode::ExprKind::Binary); }
    void visit_range_expr(indicode::Expr&) override { count(indicode::ExprKind::Range); }
    void visit_call_expr(indicode::Expr&) override { count(indicode::ExprKind::Call); }
    void visit_list_literal_expr(indicode::Expr&) override { count(indicode::ExprKind::ListLiteral); }
    void visit_index_expr(indicode::Expr&) override { count(indicode::ExprKind::Index); }
    void visit_member_expr(indicode::Expr&) override { count(indicode::ExprKind::Member); }

private:
    void count(indicode::StmtKind kind) {
        ++statement_counts[static_cast<std::size_t>(kind)];
    }

    void count(indicode::ExprKind kind) {
        ++expression_counts[static_cast<std::size_t>(kind)];
    }
};

}

int main() {
    auto program = parse_owned_program();
    CountingVisitor visitor;
    indicode::walk_ast(program, visitor);

    require(visitor.program_count == 1 && visitor.record_count == 1 &&
                visitor.field_count == 1 && visitor.function_count == 1 &&
                visitor.parameter_count == 2,
            "walker should visit every declaration and parameter");
    for (const auto count : visitor.statement_counts) {
        require(count > 0, "walker should visit every statement kind");
    }
    for (const auto count : visitor.expression_counts) {
        require(count > 0, "walker should visit every expression kind");
    }
    require(program.records[0].name == "Point" &&
                program.functions[0].body.size() == 7 &&
                program.statements.size() == 3,
            "AST should remain usable after source, lexer, and parser temporaries are destroyed");

    std::cout << "AST visitor and ownership tests passed\n";
}
