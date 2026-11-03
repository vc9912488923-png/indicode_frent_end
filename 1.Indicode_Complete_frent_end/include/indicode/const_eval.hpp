#pragma once

#include "indicode/ast.hpp"

#include <cstdint>
#include <optional>
#include <variant>

namespace indicode {

enum class ConstantEvaluationError {
    None,
    NotConstant,
    Overflow,
    DivisionByZero,
    StepLimit
};

using ConstantValue = std::variant<std::int64_t, bool>;

struct ConstantEvaluationResult {
    std::optional<ConstantValue> value;
    ConstantEvaluationError error = ConstantEvaluationError::None;
    SourceSpan span{};
};

ConstantEvaluationResult evaluate_constant_expression(const Expr& expression);

}
