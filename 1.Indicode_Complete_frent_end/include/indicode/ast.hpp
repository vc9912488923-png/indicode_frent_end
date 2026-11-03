#pragma once

#include "indicode/source.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace indicode {

enum class ExprKind {
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

struct Expr {
    ExprKind kind;
    SourceSpan span;
    std::string value;
    std::string inferred_type;
    std::string resolved_symbol;
    std::uint64_t resolved_symbol_id = 0;
    std::uint64_t resolved_type_symbol_id = 0;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::vector<std::unique_ptr<Expr>> arguments;
    std::vector<std::string> argument_names;
};

enum class StmtKind {
    Variable,
    Assignment,
    Return,
    Expression,
    If,
    While,
    Repeat,
    ForEach
};

struct Stmt {
    StmtKind kind;
    SourceSpan span;
    std::string name;
    std::string declared_type;
    std::uint64_t resolved_symbol_id = 0;
    std::uint64_t resolved_type_symbol_id = 0;
    bool mutable_binding = true;
    std::unique_ptr<Expr> expression;
    std::vector<std::unique_ptr<Stmt>> body;
    std::vector<std::unique_ptr<Stmt>> else_body;
};

enum class OwnershipMode {
    Value,
    Move,
    Borrow,
    BorrowMut
};

inline const char* ownership_mode_name(OwnershipMode mode) {
    switch (mode) {
    case OwnershipMode::Value: return "value";
    case OwnershipMode::Move: return "move";
    case OwnershipMode::Borrow: return "borrow";
    case OwnershipMode::BorrowMut: return "borrow_mut";
    }
    return "value";
}

struct Parameter {
    std::string name;
    std::string type;
    SourceSpan span;
    std::uint64_t resolved_symbol_id = 0;
    std::uint64_t resolved_type_symbol_id = 0;
    OwnershipMode ownership_mode = OwnershipMode::Value;
};

struct GenericParameter {
    std::string name;
    std::string constraint;
    SourceSpan span;
};

struct FunctionDecl {
    std::string name;
    std::uint64_t symbol_id = 0;
    std::vector<GenericParameter> generic_parameters;
    bool has_effect_annotation = false;
    std::vector<std::string> declared_effects;
    std::vector<std::string> inferred_effects;
    std::vector<Parameter> parameters;
    std::string return_type;
    std::uint64_t return_type_symbol_id = 0;
    SourceSpan span;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct RecordField {
    std::string name;
    std::string type;
    SourceSpan span;
    std::uint64_t type_symbol_id = 0;
    std::uint64_t symbol_id = 0;
};

struct RecordDecl {
    std::string name;
    std::uint64_t symbol_id = 0;
    std::vector<RecordField> fields;
    SourceSpan span;
};

struct Program {
    std::vector<RecordDecl> records;
    std::vector<FunctionDecl> functions;
    std::vector<std::unique_ptr<Stmt>> statements;
};

std::string print_program(const Program& program, bool include_types = false);
std::string serialize_ast_json(const Program& program);

}