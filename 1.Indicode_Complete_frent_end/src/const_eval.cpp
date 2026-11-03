#include "indicode/const_eval.hpp"

#include <charconv>
#include <cstddef>
#include <limits>
#include <string>

namespace indicode {
namespace {

constexpr std::size_t evaluation_step_limit = 256;

struct EvaluationState {
    std::size_t steps_remaining;
};

ConstantEvaluationResult failure(ConstantEvaluationError error, SourceSpan span) {
    return {std::nullopt, error, span};
}

bool parse_integer(const std::string& spelling, std::uint64_t& magnitude) {
    std::string digits;
    digits.reserve(spelling.size());
    for (const char character : spelling) {
        if (character != '_') digits.push_back(character);
    }
    unsigned radix = 10;
    std::size_t prefix_length = 0;
    if (digits.size() >= 2 && digits[0] == '0') {
        if (digits[1] == 'b' || digits[1] == 'B') { radix = 2; prefix_length = 2; }
        else if (digits[1] == 'o' || digits[1] == 'O') { radix = 8; prefix_length = 2; }
        else if (digits[1] == 'x' || digits[1] == 'X') { radix = 16; prefix_length = 2; }
    }
    const auto* begin = digits.data() + prefix_length;
    const auto* end = digits.data() + digits.size();
    const auto result = std::from_chars(begin, end, magnitude, static_cast<int>(radix));
    return begin != end && result.ec == std::errc{} && result.ptr == end;
}

ConstantEvaluationResult evaluate(const Expr& expression, EvaluationState& state) {
    if (state.steps_remaining == 0) {
        return failure(ConstantEvaluationError::StepLimit, expression.span);
    }
    --state.steps_remaining;

    if (expression.kind == ExprKind::Literal) {
        if (expression.value == "true") return {ConstantValue{true}, ConstantEvaluationError::None, expression.span};
        if (expression.value == "false") return {ConstantValue{false}, ConstantEvaluationError::None, expression.span};
        std::uint64_t magnitude = 0;
        if (parse_integer(expression.value, magnitude) &&
            magnitude <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return {ConstantValue{static_cast<std::int64_t>(magnitude)},
                    ConstantEvaluationError::None, expression.span};
        }
        return failure(ConstantEvaluationError::NotConstant, expression.span);
    }

    if (expression.kind == ExprKind::Unary && expression.left) {
        if (expression.value == "-" && expression.left->kind == ExprKind::Literal) {
            std::uint64_t magnitude = 0;
            if (parse_integer(expression.left->value, magnitude) &&
                magnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1) {
                return {ConstantValue{std::numeric_limits<std::int64_t>::min()},
                        ConstantEvaluationError::None, expression.span};
            }
        }
        auto operand = evaluate(*expression.left, state);
        if (!operand.value) return operand;
        if (const auto* integer = std::get_if<std::int64_t>(&*operand.value)) {
            if (expression.value == "+") return {ConstantValue{*integer}, ConstantEvaluationError::None, expression.span};
            if (expression.value == "-") {
                if (*integer == std::numeric_limits<std::int64_t>::min()) {
                    return failure(ConstantEvaluationError::Overflow, expression.span);
                }
                return {ConstantValue{-*integer}, ConstantEvaluationError::None, expression.span};
            }
            if (expression.value == "~") return {ConstantValue{~*integer}, ConstantEvaluationError::None, expression.span};
        } else if (const auto* boolean = std::get_if<bool>(&*operand.value);
                   expression.value == "!" && boolean != nullptr) {
            return {ConstantValue{!*boolean}, ConstantEvaluationError::None, expression.span};
        }
        return failure(ConstantEvaluationError::NotConstant, expression.span);
    }

    if (expression.kind != ExprKind::Binary || !expression.left || !expression.right) {
        return failure(ConstantEvaluationError::NotConstant, expression.span);
    }
    auto left = evaluate(*expression.left, state);
    if (!left.value) return left;
    if (expression.value == "&&" || expression.value == "||") {
        const auto* left_boolean = std::get_if<bool>(&*left.value);
        if (left_boolean == nullptr) return failure(ConstantEvaluationError::NotConstant, expression.span);
        if ((expression.value == "&&" && !*left_boolean) ||
            (expression.value == "||" && *left_boolean)) {
            return {ConstantValue{*left_boolean}, ConstantEvaluationError::None, expression.span};
        }
    }
    auto right = evaluate(*expression.right, state);
    if (!right.value) return right;

    if (const auto* left_boolean = std::get_if<bool>(&*left.value)) {
        const auto* right_boolean = std::get_if<bool>(&*right.value);
        if (right_boolean == nullptr) return failure(ConstantEvaluationError::NotConstant, expression.span);
        if (expression.value == "&&") return {ConstantValue{*left_boolean && *right_boolean}, ConstantEvaluationError::None, expression.span};
        if (expression.value == "||") return {ConstantValue{*left_boolean || *right_boolean}, ConstantEvaluationError::None, expression.span};
        if (expression.value == "==") return {ConstantValue{*left_boolean == *right_boolean}, ConstantEvaluationError::None, expression.span};
        if (expression.value == "!=") return {ConstantValue{*left_boolean != *right_boolean}, ConstantEvaluationError::None, expression.span};
        return failure(ConstantEvaluationError::NotConstant, expression.span);
    }

    const auto* left_integer = std::get_if<std::int64_t>(&*left.value);
    const auto* right_integer = std::get_if<std::int64_t>(&*right.value);
    if (left_integer == nullptr || right_integer == nullptr) {
        return failure(ConstantEvaluationError::NotConstant, expression.span);
    }
    const auto lhs = *left_integer;
    const auto rhs = *right_integer;
    if (expression.value == "+") {
        if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
            (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
            return failure(ConstantEvaluationError::Overflow, expression.span);
        }
        return {ConstantValue{lhs + rhs}, ConstantEvaluationError::None, expression.span};
    }
    if (expression.value == "-") {
        if ((rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs) ||
            (rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs)) {
            return failure(ConstantEvaluationError::Overflow, expression.span);
        }
        return {ConstantValue{lhs - rhs}, ConstantEvaluationError::None, expression.span};
    }
    if (expression.value == "*") {
        if (lhs == 0 || rhs == 0) return {ConstantValue{std::int64_t{0}}, ConstantEvaluationError::None, expression.span};
        if ((lhs == -1 && rhs == std::numeric_limits<std::int64_t>::min()) ||
            (rhs == -1 && lhs == std::numeric_limits<std::int64_t>::min()) ||
            (lhs > 0
                 ? (rhs > 0 ? lhs > std::numeric_limits<std::int64_t>::max() / rhs
                            : rhs < std::numeric_limits<std::int64_t>::min() / lhs)
                 : (rhs > 0 ? lhs < std::numeric_limits<std::int64_t>::min() / rhs
                            : lhs < std::numeric_limits<std::int64_t>::max() / rhs))) {
            return failure(ConstantEvaluationError::Overflow, expression.span);
        }
        return {ConstantValue{lhs * rhs}, ConstantEvaluationError::None, expression.span};
    }
    if (expression.value == "/" || expression.value == "%") {
        if (rhs == 0) return failure(ConstantEvaluationError::DivisionByZero, expression.span);
        if (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1) {
            return expression.value == "%"
                ? ConstantEvaluationResult{ConstantValue{std::int64_t{0}}, ConstantEvaluationError::None, expression.span}
                : failure(ConstantEvaluationError::Overflow, expression.span);
        }
        return {ConstantValue{expression.value == "/" ? lhs / rhs : lhs % rhs},
                ConstantEvaluationError::None, expression.span};
    }
    if (expression.value == "==") return {ConstantValue{lhs == rhs}, ConstantEvaluationError::None, expression.span};
    if (expression.value == "!=") return {ConstantValue{lhs != rhs}, ConstantEvaluationError::None, expression.span};
    if (expression.value == "<") return {ConstantValue{lhs < rhs}, ConstantEvaluationError::None, expression.span};
    if (expression.value == "<=") return {ConstantValue{lhs <= rhs}, ConstantEvaluationError::None, expression.span};
    if (expression.value == ">") return {ConstantValue{lhs > rhs}, ConstantEvaluationError::None, expression.span};
    if (expression.value == ">=") return {ConstantValue{lhs >= rhs}, ConstantEvaluationError::None, expression.span};
    return failure(ConstantEvaluationError::NotConstant, expression.span);
}

}

ConstantEvaluationResult evaluate_constant_expression(const Expr& expression) {
    EvaluationState state{evaluation_step_limit};
    return evaluate(expression, state);
}

}
