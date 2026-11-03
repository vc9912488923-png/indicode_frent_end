#pragma once

#include "indicode/ast.hpp"
#include "indicode/diagnostic.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace indicode {

struct SemanticResult {
    std::vector<Diagnostic> diagnostics;
};

class Analyzer {
public:
    SemanticResult analyze(Program& program);

private:
    struct Binding {
        std::string type;
        bool mutable_binding;
        SourceSpan declaration_span;
        std::uint64_t symbol_id = 0;
    };

    struct FunctionSignature {
        std::vector<std::string> parameter_names;
        std::vector<std::string> parameter_types;
        std::string return_type;
        std::uint64_t symbol_id = 0;
    };

    std::vector<std::unordered_map<std::string, Binding>> scopes_;
    std::unordered_map<std::string, FunctionSignature> functions_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> records_;
    std::unordered_map<std::string, std::uint64_t> record_symbol_ids_;
    std::unordered_map<std::string, std::uint64_t> record_field_symbol_ids_;
    std::unordered_map<std::string, SourceSpan> record_declaration_spans_;
    std::unordered_map<std::string, SourceSpan> record_field_declaration_spans_;
    std::unordered_map<std::string, SourceSpan> function_declaration_spans_;
    std::vector<Diagnostic> diagnostics_;
    std::string current_return_type_ = "Void";
    bool in_function_ = false;
    std::uint64_t next_symbol_id_ = 1;

    void add_error(const char* code, const char* message, SourceSpan span);
    bool is_known_type(const std::string& type) const;
    std::uint64_t type_symbol_id(const std::string& type) const;
    std::string list_element_type(const std::string& type) const;
    void push_scope();
    void pop_scope();
    Binding* find_binding(const std::string& name);
    std::uint64_t declare_binding(const std::string& name, Binding binding, SourceSpan span);
    void analyze_statements(std::vector<std::unique_ptr<Stmt>>& statements);
    void analyze_statement(Stmt& statement);
    std::string analyze_expression(Expr& expression);
    bool type_integer_literal(Expr& expression, const std::string& type);
    std::string numeric_binary_type(Expr& expression, const std::string& left,
                                    const std::string& right);
    bool always_returns(const std::vector<std::unique_ptr<Stmt>>& statements) const;
};

}