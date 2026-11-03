#pragma once

#include "indicode/ast.hpp"
#include "indicode/diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace indicode {

enum class HirExprKind {
    Name,
    Literal,
    Unary,
    Binary,
    Range,
    Call,
    ListLiteral,
    Index,
    Member
};

struct HirExpr {
    HirExprKind kind;
    std::string type;
    SourceSpan span;
    std::string value;
    std::string symbol;
    std::uint64_t symbol_id = 0;
    std::uint64_t type_symbol_id = 0;
    std::unique_ptr<HirExpr> left;
    std::unique_ptr<HirExpr> right;
    std::vector<std::unique_ptr<HirExpr>> arguments;
    std::vector<std::string> argument_names;
};

enum class HirStmtKind {
    Variable,
    Assignment,
    Return,
    Expression,
    If,
    While,
    Repeat,
    ForEach
};

struct HirStmt {
    HirStmtKind kind;
    SourceSpan span;
    std::string name;
    std::string type;
    std::uint64_t symbol_id = 0;
    std::uint64_t type_symbol_id = 0;
    bool mutable_binding = true;
    std::unique_ptr<HirExpr> expression;
    std::vector<std::unique_ptr<HirStmt>> body;
    std::vector<std::unique_ptr<HirStmt>> else_body;
};

struct HirParameter {
    std::string name;
    std::string type;
    SourceSpan span;
    std::uint64_t symbol_id = 0;
    std::uint64_t type_symbol_id = 0;
};

struct HirFunction {
    std::string name;
    std::uint64_t symbol_id = 0;
    std::vector<HirParameter> parameters;
    std::string return_type;
    std::uint64_t return_type_symbol_id = 0;
    SourceSpan span;
    std::vector<std::unique_ptr<HirStmt>> body;
};

struct HirRecord {
    std::string name;
    std::uint64_t symbol_id = 0;
    std::vector<RecordField> fields;
    SourceSpan span;
};

struct HirProgram {
    std::vector<HirRecord> records;
    std::vector<HirFunction> functions;
    std::vector<std::unique_ptr<HirStmt>> statements;
};

HirProgram lower_to_hir(const Program& program);
std::vector<Diagnostic> verify_hir(const HirProgram& program);
std::string print_hir(const HirProgram& program);
std::string serialize_hir_json(const HirProgram& program);

}