#include "indicode/effects.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace indicode {
namespace {

struct FunctionEffects {
    std::set<std::string> direct;
    std::set<std::string> calls;
    std::set<std::string> inferred;
};

void collect_expression_effects(const Expr& expression,
                                const std::unordered_set<std::string>& record_names,
                                FunctionEffects& effects) {
    if (expression.kind == ExprKind::ListLiteral) effects.direct.insert("allocation");
    if (expression.kind == ExprKind::Call) {
        if (expression.value == "say") effects.direct.insert("io");
        else if (record_names.contains(expression.value)) effects.direct.insert("allocation");
        else effects.calls.insert(expression.value);
    }
    if (expression.left) collect_expression_effects(*expression.left, record_names, effects);
    if (expression.right) collect_expression_effects(*expression.right, record_names, effects);
    for (const auto& argument : expression.arguments) {
        collect_expression_effects(*argument, record_names, effects);
    }
}

void collect_statement_effects(const std::vector<std::unique_ptr<Stmt>>& statements,
                               const std::unordered_set<std::string>& record_names,
                               FunctionEffects& effects) {
    for (const auto& statement : statements) {
        if (statement->kind == StmtKind::Assignment) effects.direct.insert("mutation");
        if (statement->expression) {
            collect_expression_effects(*statement->expression, record_names, effects);
        }
        collect_statement_effects(statement->body, record_names, effects);
        collect_statement_effects(statement->else_body, record_names, effects);
    }
}

}

EffectCheckResult EffectChecker::check(Program& program) const {
    EffectCheckResult result;
    static const std::unordered_set<std::string> supported_effects{
        "allocation", "io", "mutation"
    };
    std::unordered_set<std::string> record_names;
    for (const auto& record : program.records) record_names.insert(record.name);

    std::vector<FunctionEffects> summaries(program.functions.size());
    std::unordered_map<std::string, std::size_t> function_indices;
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        function_indices.emplace(program.functions[index].name, index);
        collect_statement_effects(program.functions[index].body, record_names, summaries[index]);
        summaries[index].inferred = summaries[index].direct;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& summary : summaries) {
            for (const auto& callee : summary.calls) {
                const auto found = function_indices.find(callee);
                if (found != function_indices.end()) {
                    const auto previous_size = summary.inferred.size();
                    summary.inferred.insert(summaries[found->second].inferred.begin(),
                                            summaries[found->second].inferred.end());
                    changed = changed || summary.inferred.size() != previous_size;
                }
            }
        }
    }

    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        auto& function = program.functions[index];
        function.inferred_effects.assign(summaries[index].inferred.begin(),
                                         summaries[index].inferred.end());
        if (!function.has_effect_annotation) continue;

        std::unordered_set<std::string> declared;
        for (const auto& effect : function.declared_effects) {
            if (!supported_effects.contains(effect)) {
                result.diagnostics.push_back({DiagnosticSeverity::Error, "E-EFFECT-001",
                                             "unknown function effect", function.span});
            }
            if (!declared.insert(effect).second) {
                result.diagnostics.push_back({DiagnosticSeverity::Error, "E-EFFECT-002",
                                             "function effect is declared more than once", function.span});
            }
        }
        for (const auto& effect : function.inferred_effects) {
            if (!declared.contains(effect)) {
                result.diagnostics.push_back({DiagnosticSeverity::Error, "E-EFFECT-003",
                                             "function body uses an undeclared effect", function.span});
            }
        }
    }
    return result;
}

}