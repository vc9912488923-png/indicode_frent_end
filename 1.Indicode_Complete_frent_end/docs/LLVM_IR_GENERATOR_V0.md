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
  and returns.
- Signedness-sensitive integer division, remainder, and ordered comparisons.

Integer mappings follow the front-end's existing range rules: `Byte`/`U8` map
to `i8`, `Int2`/`UInt2` to `i16`, `Int`/`UInt` to `i32`, and `Long`/`ULong`
to `i64`; `Bool` maps to `i1`. LLVM arithmetic without overflow flags follows
LLVM's integer behavior. This is an IR mapping, not a complete Indicode runtime
semantics or ABI guarantee.

Calls, short-circuit Boolean operators, branches, loops, globals, records,
lists, ranges, text, floating-point values, tensors, and generic functions are
not implemented in v0. Encountering unsupported constructs returns an error
diagnostic and no partial IR. The output has no target triple or data layout.

This is the initial 10% implementation slice of the separately tracked IR
generator milestone, not a complete backend. LLVM validation is possible only
when a compatible LLVM tool is already installed locally.