# LLVM IR Generator v0

The front end and textual IR generator are separate milestones. The generator
consumes verified project-owned HIR and writes LLVM IR text; it does not use or
download LLVM libraries, invoke LLVM tools, produce machine code, link, or run
programs. Use `indicode ir file.indi` to write the IR to standard output.

## Initial supported slice

- Non-generic functions with scalar `Bool`, `Byte`, `U8`, `Int2`, `UInt2`,
  `Int`, `UInt`, `Long`, and `ULong` values, plus `Void` function returns.
- Parameters, local scalar variables, assignment, arithmetic, comparisons,
  unary `+`, `-`, `~`, `!`, ownership markers as value-preserving wrappers,
    and returns. Local variables in nested `if`/`while` bodies are supported.
- Signedness-sensitive integer division, remainder, and ordered comparisons.
- `if`/`else` and `while` control flow, plus short-circuit `&&` and `||`.
- Direct calls to non-generic user functions, including named arguments. Call
    operands are emitted in parameter order while expressions are evaluated in
    source order.

Integer mappings follow the front-end's existing range rules: `Byte`/`U8` map
to `i8`, `Int2`/`UInt2` to `i16`, `Int`/`UInt` to `i32`, and `Long`/`ULong`
to `i64`; `Bool` maps to `i1`. LLVM arithmetic without overflow flags follows
LLVM's integer behavior. This is an IR mapping, not a complete Indicode runtime
semantics or ABI guarantee.

Built-in calls (`say` and conversions), generic functions, `repeat`/`for`
loops, globals, records, lists, ranges, text, floating-point values, and
tensors are not implemented in v0. Encountering unsupported constructs returns
an error diagnostic and no partial IR. The output has no target triple or data
layout.

This is the 50% implementation milestone of the separately tracked IR
generator, not a complete backend. LLVM validation is possible only when a
compatible LLVM tool is already installed locally.