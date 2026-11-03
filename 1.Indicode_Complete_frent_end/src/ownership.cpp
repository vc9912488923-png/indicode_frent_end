#include "indicode/ownership.hpp"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace indicode {
namespace {

using MovedSymbols = std::unordered_set<std::uint64_t>;
using MutableSymbols = std::unordered_set<std::uint64_t>;

OwnershipMode expression_mode(const Expr& expression) {
    if (expression.kind != ExprKind::Unary) return OwnershipMode::Value;
    if (expression.value == "move") return OwnershipMode::Move;
    if (expression.value == "borrow") return OwnershipMode::Borrow;
    if (expression.value == "borrow_mut") return OwnershipMode::BorrowMut;
    return OwnershipMode::Value;
}

std::size_t parameter_for_argument(const FunctionDecl& function, const Expr& call,
                                   std::size_t argument_index) {
    const auto& argument_name = call.argument_names[argument_index];
    if (argument_name.empty()) return argument_index;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (function.parameters[index].name == argument_name) return index;
    }
    return function.parameters.size();
}

}

OwnershipCheckResult OwnershipChecker::check(const Program& program) const {
    OwnershipCheckResult result;
    std::unordered_map<std::string, const FunctionDecl*> functions;
    for (const auto& function : program.functions) functions.emplace(function.name, &function);

    const auto add_error = [&](const char* code, const char* message, SourceSpan span) {
        result.diagnostics.push_back({DiagnosticSeverity::Error, code, message, span});
    };

    std::function<void(const Expr&, MovedSymbols&, const MutableSymbols&, bool)> check_expression;
    check_expression = [&](const Expr& expression, MovedSymbols& moved,
                           const MutableSymbols& mutable_symbols, bool borrow_allowed) {
        if (expression.kind == ExprKind::Unary &&
            (expression.value == "move" || expression.value == "borrow" ||
             expression.value == "borrow_mut")) {
            if (!expression.left || expression.left->kind != ExprKind::Name ||
                expression.left->resolved_symbol_id == 0) {
                add_error("E-OWN-001", "ownership operation requires a resolved variable", expression.span);
                if (expression.left) {
                    check_expression(*expression.left, moved, mutable_symbols, false);
                }
                return;
            }
            const auto symbol_id = expression.left->resolved_symbol_id;
            if (moved.contains(symbol_id)) {
                add_error("E-OWN-003", "value is used after it was moved", expression.left->span);
                return;
            }
            if (expression.value == "move") {
                moved.insert(symbol_id);
            } else if (!borrow_allowed) {
                add_error("E-OWN-006", "borrow expressions are only valid as call arguments",
                          expression.span);
            } else if (expression.value == "borrow_mut" &&
                       !mutable_symbols.contains(symbol_id)) {
                add_error("E-OWN-005", "mutable borrow requires a mutable binding",
                          expression.left->span);
            }
            return;
        }

        if (expression.kind == ExprKind::Name &&
            expression.resolved_symbol_id != 0 && moved.contains(expression.resolved_symbol_id)) {
            add_error("E-OWN-003", "value is used after it was moved", expression.span);
            return;
        }

        if (expression.kind == ExprKind::Call) {
            const auto target = functions.find(expression.value);
            std::vector<std::pair<std::uint64_t, OwnershipMode>> borrowed_in_call;
            for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                const auto& argument = *expression.arguments[index];
                OwnershipMode expected = OwnershipMode::Value;
                if (target != functions.end()) {
                    const auto parameter_index = parameter_for_argument(
                        *target->second, expression, index);
                    if (parameter_index < target->second->parameters.size()) {
                        expected = target->second->parameters[parameter_index].ownership_mode;
                    }
                }
                const auto actual = expression_mode(argument);
                if (actual != expected) {
                    add_error("E-OWN-002", "call argument ownership mode does not match the parameter",
                              argument.span);
                }
                if ((actual == OwnershipMode::Borrow || actual == OwnershipMode::BorrowMut ||
                     actual == OwnershipMode::Move) && argument.left &&
                    argument.left->kind == ExprKind::Name) {
                    const auto symbol_id = argument.left->resolved_symbol_id;
                    for (const auto& [borrowed_id, borrowed_mode] : borrowed_in_call) {
                        if (borrowed_id == symbol_id &&
                            (actual != OwnershipMode::Borrow ||
                             borrowed_mode != OwnershipMode::Borrow)) {
                            add_error("E-OWN-004", "conflicting borrows or move in one call",
                                      argument.span);
                        }
                    }
                    borrowed_in_call.emplace_back(symbol_id, actual);
                }
                check_expression(argument, moved, mutable_symbols, true);
            }
            if (expression.left) check_expression(*expression.left, moved, mutable_symbols, false);
            if (expression.right) check_expression(*expression.right, moved, mutable_symbols, false);
            return;
        }

        if (expression.left) check_expression(*expression.left, moved, mutable_symbols, false);
        if (expression.right) check_expression(*expression.right, moved, mutable_symbols, false);
        for (const auto& argument : expression.arguments) {
            check_expression(*argument, moved, mutable_symbols, false);
        }
    };

    std::function<void(const std::vector<std::unique_ptr<Stmt>>&, MovedSymbols&,
                       MutableSymbols&)> check_statements;
    check_statements = [&](const auto& statements, MovedSymbols& moved,
                           MutableSymbols& mutable_symbols) {
        for (const auto& statement : statements) {
            if (statement->expression) {
                check_expression(*statement->expression, moved, mutable_symbols, false);
            }
            if (statement->kind == StmtKind::Variable && statement->resolved_symbol_id != 0) {
                if (statement->mutable_binding) mutable_symbols.insert(statement->resolved_symbol_id);
                else mutable_symbols.erase(statement->resolved_symbol_id);
            }
            if (statement->kind == StmtKind::Assignment && statement->resolved_symbol_id != 0) {
                moved.erase(statement->resolved_symbol_id);
            }
            if (statement->kind == StmtKind::If) {
                auto then_moved = moved;
                auto else_moved = moved;
                auto then_mutable = mutable_symbols;
                auto else_mutable = mutable_symbols;
                check_statements(statement->body, then_moved, then_mutable);
                check_statements(statement->else_body, else_moved, else_mutable);
                moved.insert(then_moved.begin(), then_moved.end());
                moved.insert(else_moved.begin(), else_moved.end());
            } else if (statement->kind == StmtKind::While ||
                       statement->kind == StmtKind::Repeat ||
                       statement->kind == StmtKind::ForEach) {
                auto body_moved = moved;
                auto body_mutable = mutable_symbols;
                check_statements(statement->body, body_moved, body_mutable);
                moved.insert(body_moved.begin(), body_moved.end());
            } else {
                check_statements(statement->body, moved, mutable_symbols);
                check_statements(statement->else_body, moved, mutable_symbols);
            }
        }
    };

    MovedSymbols top_level_moved;
    MutableSymbols top_level_mutable;
    check_statements(program.statements, top_level_moved, top_level_mutable);
    for (const auto& function : program.functions) {
        MovedSymbols moved;
        MutableSymbols mutable_symbols;
        for (const auto& parameter : function.parameters) {
            if (parameter.ownership_mode == OwnershipMode::BorrowMut &&
                parameter.resolved_symbol_id != 0) {
                mutable_symbols.insert(parameter.resolved_symbol_id);
            }
        }
        check_statements(function.body, moved, mutable_symbols);
    }
    return result;
}

}