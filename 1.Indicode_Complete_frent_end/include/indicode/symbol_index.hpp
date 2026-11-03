#pragma once

#include "indicode/hir.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace indicode {

using SymbolScopeId = std::uint64_t;

enum class IndexedSymbolKind {
    Function,
    Record,
    Field,
    Parameter,
    Variable,
    LoopBinding,
    Type
};

struct IndexedSymbol {
    std::uint64_t id = 0;
    std::string name;
    IndexedSymbolKind kind = IndexedSymbolKind::Variable;
    SourceSpan definition_span;
    SymbolScopeId scope_id = 0;
    bool has_definition = false;
};

struct IndexedReference {
    std::uint64_t symbol_id = 0;
    SourceSpan span;
    SymbolScopeId scope_id = 0;
};

struct IndexedScope {
    SymbolScopeId id = 0;
    SymbolScopeId parent_id = 0;
    std::size_t depth = 0;
    SourceSpan span;
    std::vector<std::uint64_t> symbol_ids;
};

class SymbolIndex {
public:
    void rebuild(const HirProgram& program, std::uint64_t source_revision);

    std::uint64_t source_revision() const { return source_revision_; }
    bool is_current_for(std::uint64_t source_revision) const;
    const IndexedSymbol* find_symbol(std::uint64_t symbol_id) const;
    const std::vector<IndexedReference>& references_to(std::uint64_t symbol_id) const;
    const IndexedScope* scope_at(std::size_t byte_offset) const;
    const std::vector<IndexedScope>& scopes() const { return scopes_; }

private:
    std::uint64_t source_revision_ = 0;
    std::vector<IndexedScope> scopes_;
    std::unordered_map<std::uint64_t, IndexedSymbol> symbols_;
    std::unordered_map<std::uint64_t, std::vector<IndexedReference>> references_;
};

}