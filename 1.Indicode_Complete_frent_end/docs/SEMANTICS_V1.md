# Indicode semantic rules v1

These are the static rules implemented by the front end for syntax in
[LANGUAGE_SPEC_V1.md](./LANGUAGE_SPEC_V1.md). They describe typing and binding,
not runtime or code-generation behavior.

## Names and scopes

Values, functions, and types are resolved by distinct lookup rules. Top-level
values are visible in function bodies. Function and record declarations are
visible throughout the file, including before their source declaration. A
nested block can shadow an outer value; duplicate values in the same scope are
errors. Function and record declarations share a call-name uniqueness rule,
since both use identifier-call syntax. Modules,
imports, overloads, and declaration visibility modifiers do not exist in v1.
Each value, function, record type, record field, and builtin type/function
reference has a stable identity within a semantic-analysis run.

## Types and expressions

There are no implicit numeric promotions or implicit conversions. Arithmetic
binary operators require operands of the same numeric type and produce that
type. Unary `+` and `-` accept numeric operands, `~` accepts integer operands,
and `!`, `&&`, and `||` require `Bool`. Comparisons require equal operand types:
numeric values, `Char`, and `Text` support ordering; `Bool` supports only
equality and inequality. Records, lists, ranges, and tensors have no equality
or ordering operation in v1.

`if` and `while` conditions must be `Bool`; `repeat` counts must be integers.
`return` must match the declared function return type, and only `Void`
functions may omit a returned value. `Void` is not a value type and cannot be
used for variables, parameters, list elements, or record fields.

List literals must be homogeneous. An empty list needs an explicit `List<T>`
type. A list index must be `Int` and produces `T`; a text index must be `Int`
and produces `Char`. `for` accepts `List<T>`, `Range<T>`, or `Text`, binding
each element with type `T` (or `Char` for text).

Record construction uses named fields, each exactly once, with values matching
the declared field types. Member access must name a declared field. Function
calls must match parameter count, names, and types; positional arguments
cannot follow named arguments.

Numeric conversions are explicit calls to `to_Byte`, `to_U8`, `to_Int2`,
`to_UInt2`, `to_Int`, `to_UInt`, `to_Long`, `to_ULong`, `to_Half`, `to_Float`,
or `to_Double`. They require a numeric input and the literal `checked: true`;
compile-time integer constants and numeric literal operands are checked
against the destination range. Other expressions remain checked conversions
whose runtime behavior is outside this front end. The `say` builtin accepts
exactly one non-`Void` value and returns `Void`.

Tensor declarations permit numeric element types and non-empty dimensions.
Assignments, argument passing, and returns require exact element type and
shape equality; symbolic dimensions are equal only when their names match.
Tensor operations and broadcasting are not part of v1.

## Constant expressions and control flow

The constant evaluator handles integer and Boolean literals, supported unary
operators, integer arithmetic/comparisons, and short-circuit Boolean
operators. Integer evaluation uses signed 64-bit bounds and a 256-node step
limit; overflow, division by zero, and reaching the limit have distinct
diagnostic outcomes. Floating-point constant folding, ownership, and effect
checking are not part of v1. Non-constant expressions are left to ordinary
semantic analysis.

A `return` terminates its statement list. An `if` terminates only if it has an
`else` and both branches terminate. Loops are conservatively not assumed to
execute, so a return inside a loop alone does not establish a definite return.
Statements after a guaranteed return are unreachable and diagnosed.
