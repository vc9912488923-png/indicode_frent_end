# Indicode semantic rules v1

**Front-end package version:** 0.9.8 (pre-release; not a complete compiler).

These are the static rules implemented by the front end for syntax in
[LANGUAGE_SPEC_V1.md](./LANGUAGE_SPEC_V1.md). They describe typing and binding,
not runtime or code-generation behavior.

## Names and scopes

Values, functions, and types are resolved by distinct lookup rules. Top-level
values are visible in function bodies. Function and record declarations are
visible throughout the file, including before their source declaration. A
nested block can shadow an outer value; duplicate values in the same scope are
errors. Function and record declarations share a call-name uniqueness rule,
since both use identifier-call syntax. Modules, imports, overload declarations,
and declaration visibility modifiers do not exist in v1.
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

Generic functions infer type parameters from their arguments. Repeated uses of
one type parameter must infer the same type. `Numeric` accepts the supported
integer and floating-point types; `Integer` accepts the supported integer
types. Generic parameters are substituted into the return type and supported
`List<T>`/`Range<T>` type patterns. Generic record declarations and explicit
type arguments at call sites are not supported.

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
diagnostic outcomes. Floating-point constant folding is not part of v1.
Non-constant expressions are left to ordinary semantic analysis.

A `return` terminates its statement list. An `if` terminates only if it has an
`else` and both branches terminate. Loops are conservatively not assumed to
execute, so a return inside a loop alone does not establish a definite return.
Statements after a guaranteed return are unreachable and diagnosed.

## Effects

The checker infers `io` for `say`, `allocation` for list literals and record
construction, and `mutation` for assignments. Effects propagate through direct
and transitive function calls. If a function has an `effects(...)` annotation,
the annotation must use only these effect names, contain no duplicate names,
and include every inferred effect. Functions without annotations still have
their inferred effect set recorded; this front end does not define runtime
effect behavior.

## Ownership modes

Parameters use `value` (the default), `move`, `borrow`, or `borrow_mut` modes;
call arguments must use the matching mode. `move value` marks a resolved
binding as moved, and later use is rejected until assignment restores it.
`borrow value` and `borrow_mut value` are only accepted as call arguments.
Mutable borrowing requires a mutable binding. In one call, multiple shared
borrows are accepted, while a mutable borrow or move conflicts with another
borrow/move of the same binding.

This is the ownership checker implemented by this front end, not a complete
borrow checker: it does not define reference types, lifetimes, escape analysis,
concurrency guarantees, or unsafe operations. The checker uses conservative
branch/loop tracking; focused tests define the current behavior.
