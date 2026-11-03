#include "indicode/symbol_index.hpp"

#include <algorithm>
#include <functional>

namespace indicode {
namespace {

bool contains_offset(const SourceSpan& span, std::size_t offset) {
    return span.start.offset <= offset && offset <= span.end.offset;
}

SourceSpan statements_span(const std::vector<std::unique_ptr<HirStmt>>& statements,
                           const SourceSpan& fallback) {
    if (statements.empty()) return fallback;
    return {statements.front()->span.start, statements.back()->span.end};
}

}

void SymbolIndex::rebuild(const HirProgram& program, std::uint64_t source_revision) {
    source_revision_ = source_revision;
    scopes_.clear();
    symbols_.clear();
    references_.clear();

    SourceSpan program_span{};
    bool has_program_span = false;
    const auto include_span = [&](const SourceSpan& span) {
        if (!has_program_span) {
            program_span = span;
            has_program_span = true;
            return;
        }
        if (span.start.offset < program_span.start.offset) program_span.start = span.start;
        if (span.end.offset > program_span.end.offset) program_span.end = span.end;
    };
    for (const auto& record : program.records) include_span(record.span);
    for (const auto& function : program.functions) include_span(function.span);
    for (const auto& statement : program.statements) include_span(statement->span);

    scopes_.push_back({1, 0, 0, program_span, {}});
    SymbolScopeId next_scope_id = 2;
    const auto add_scope = [&](SymbolScopeId parent_id, const SourceSpan& span) {
        const auto parent_index = static_cast<std::size_t>(parent_id - 1);
        const auto depth = parent_id != 0 && parent_index < scopes_.size()
                               ? scopes_[parent_index].depth + 1
                               : 0;
        const auto id = next_scope_id++;
        scopes_.push_back({id, parent_id, depth, span, {}});
        return id;
    };
    const auto add_definition = [&](std::uint64_t id, const std::string& name,
                                   IndexedSymbolKind kind, const SourceSpan& span,
                                   SymbolScopeId scope_id) {
        if (id == 0) return;
        auto [found, inserted] = symbols_.try_emplace(id);
        auto& symbol = found->second;
        if (inserted || !symbol.has_definition) {
            symbol.id = id;
            symbol.name = name;
            symbol.kind = kind;
            symbol.definition_span = span;
            symbol.scope_id = scope_id;
            symbol.has_definition = true;
            const auto scope_index = static_cast<std::size_t>(scope_id - 1);
            if (scope_id != 0 && scope_index < scopes_.size()) {
                scopes_[scope_index].symbol_ids.push_back(id);
            }
        }
    };
    const auto add_reference = [&](std::uint64_t id, const std::string& name,
                                   IndexedSymbolKind kind, const SourceSpan& span,
                                   SymbolScopeId scope_id) {
        if (id == 0) return;
        auto [found, inserted] = symbols_.try_emplace(id);
        if (inserted) {
            found->second.id = id;
            found->second.name = name;
            found->second.kind = kind;
        }
        references_[id].push_back({id, span, scope_id});
    };
    const auto add_type_reference = [&](std::uint64_t id, const std::string& name,
                                        const SourceSpan& span, SymbolScopeId scope_id) {
        add_reference(id, name, IndexedSymbolKind::Type, span, scope_id);
    };

    for (const auto& record : program.records) {
        add_definition(record.symbol_id, record.name, IndexedSymbolKind::Record,
                       record.span, 1);
        const auto record_scope = add_scope(1, record.span);
        for (const auto& field : record.fields) {
            add_definition(field.symbol_id, field.name, IndexedSymbolKind::Field,
                           field.span, record_scope);
            add_type_reference(field.type_symbol_id, field.type, field.span, record_scope);
        }
    }

    std::vector<SymbolScopeId> function_scopes;
    function_scopes.reserve(program.functions.size());
    for (const auto& function : program.functions) {
        add_definition(function.symbol_id, function.name, IndexedSymbolKind::Function,
                       function.span, 1);
        const auto function_scope = add_scope(1, function.span);
        function_scopes.push_back(function_scope);
        add_type_reference(function.return_type_symbol_id, function.return_type,
                           function.span, function_scope);
        for (const auto& parameter : function.parameters) {
            add_definition(parameter.symbol_id, parameter.name, IndexedSymbolKind::Parameter,
                           parameter.span, function_scope);
            add_type_reference(parameter.type_symbol_id, parameter.type,
                               parameter.span, function_scope);
        }
    }

    std::function<void(const HirExpr&, SymbolScopeId)> index_expression;
    index_expression = [&](const HirExpr& expression, SymbolScopeId scope_id) {
        switch (expression.kind) {
        case HirExprKind::Name:
            add_reference(expression.symbol_id, expression.symbol,
                          IndexedSymbolKind::Variable, expression.span, scope_id);
            break;
        case HirExprKind::Call:
            add_reference(expression.symbol_id, expression.symbol,
                          IndexedSymbolKind::Function, expression.span, scope_id);
            break;
        case HirExprKind::Member:
            add_reference(expression.symbol_id, expression.value,
                          IndexedSymbolKind::Field, expression.span, scope_id);
            break;
        default:
            break;
        }
        add_type_reference(expression.type_symbol_id, expression.type,
                           expression.span, scope_id);
        if (expression.left) index_expression(*expression.left, scope_id);
        if (expression.right) index_expression(*expression.right, scope_id);
        for (const auto& argument : expression.arguments) {
            index_expression(*argument, scope_id);
        }
    };

    std::function<void(const std::vector<std::unique_ptr<HirStmt>>&, SymbolScopeId)> index_statements;
    std::function<void(const HirStmt&, SymbolScopeId)> index_statement;
    index_statements = [&](const auto& statements, SymbolScopeId scope_id) {
        for (const auto& statement : statements) index_statement(*statement, scope_id);
    };
    index_statement = [&](const HirStmt& statement, SymbolScopeId scope_id) {
        if (statement.kind == HirStmtKind::Variable) {
            add_definition(statement.symbol_id, statement.name, IndexedSymbolKind::Variable,
                           statement.span, scope_id);
            add_type_reference(statement.type_symbol_id, statement.type,
                               statement.span, scope_id);
        } else if (statement.kind == HirStmtKind::Assignment) {
            add_reference(statement.symbol_id, statement.name, IndexedSymbolKind::Variable,
                          statement.span, scope_id);
        }
        if (statement.expression) index_expression(*statement.expression, scope_id);

        if (statement.kind == HirStmtKind::ForEach) {
            const auto loop_scope = add_scope(scope_id, statements_span(statement.body, statement.span));
            add_definition(statement.symbol_id, statement.name, IndexedSymbolKind::LoopBinding,
                           statement.span, loop_scope);
            add_type_reference(statement.type_symbol_id, statement.type,
                               statement.span, loop_scope);
            index_statements(statement.body, loop_scope);
            return;
        }
        if (statement.kind == HirStmtKind::If) {
            const auto then_scope = add_scope(scope_id, statements_span(statement.body, statement.span));
            index_statements(statement.body, then_scope);
            if (!statement.else_body.empty()) {
                const auto else_scope = add_scope(
                    scope_id, statements_span(statement.else_body, statement.span));
                index_statements(statement.else_body, else_scope);
            }
            return;
        }
        if (statement.kind == HirStmtKind::While || statement.kind == HirStmtKind::Repeat) {
            const auto body_scope = add_scope(scope_id, statements_span(statement.body, statement.span));
            index_statements(statement.body, body_scope);
        }
    };

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        index_statements(program.functions[index].body, function_scopes[index]);
    }
    index_statements(program.statements, 1);
}

bool SymbolIndex::is_current_for(std::uint64_t source_revision) const {
    return source_revision_ == source_revision;
}

const IndexedSymbol* SymbolIndex::find_symbol(std::uint64_t symbol_id) const {
    const auto found = symbols_.find(symbol_id);
    return found == symbols_.end() ? nullptr : &found->second;
}

const std::vector<IndexedReference>& SymbolIndex::references_to(std::uint64_t symbol_id) const {
    static const std::vector<IndexedReference> empty;
    const auto found = references_.find(symbol_id);
    return found == references_.end() ? empty : found->second;
}

const IndexedScope* SymbolIndex::scope_at(std::size_t byte_offset) const {
    const IndexedScope* best = nullptr;
    for (const auto& scope : scopes_) {
        if (contains_offset(scope.span, byte_offset) &&
            (best == nullptr || scope.depth > best->depth)) {
            best = &scope;
        }
    }
    return best;
}

}