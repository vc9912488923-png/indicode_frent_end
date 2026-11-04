# LLVM IR Generator v0

The front end and textual IR generator are separate milestones. The generator
consumes verified project-owned HIR and writes LLVM IR text. It does not link
LLVM libraries, download or build LLVM, invoke LLVM as part of normal builds,
produce machine code, link programs, or execute them.

The current implementation is estimated at **about 70% of the planned
single-file v1-to-LLVM-IR lowering scope**. This is a scope estimate, not a
test-coverage percentage or a compiler-wide readiness score. The front-end
readiness estimate is tracked separately in [PROGRESS.md](./PROGRESS.md).

## Implemented lowering

- Non-generic functions, parameters, locals, top-level globals and an
  `indicode_entry` initializer function.
- `Bool`, integer types (`Byte`, `U8`, `Int2`, `UInt2`, `Int`, `UInt`, `Long`,
  `ULong`), floating-point types (`Half`, `Float`, `Double`), `Char`, `Text`,
  records, lists, and ranges.
- Assignments, returns, arithmetic, comparisons, unary operators, checked
  numeric conversions, ownership markers as value-preserving wrappers, and
  direct calls to declared non-generic functions.
- `if`/`else`, `while`, `repeat`, `for` over ranges/lists/text, and short-circuit
  `&&`/`||`.
- Record construction/member reads; list construction/indexing; text
  literals/indexing/comparison; and range construction/iteration.
- Named call arguments are evaluated in source order and passed in parameter
  order.

Integer mappings follow the front end's range rules: `Byte`/`U8` map to `i8`,
`Int2`/`UInt2` to `i16`, `Int`/`UInt` to `i32`, and `Long`/`ULong` to `i64`.
`Bool` maps to `i1`, `Char` to `i32`, and floating-point types to their
corresponding LLVM types. Arithmetic is emitted without overflow flags. This
is an IR mapping, not a complete Indicode runtime semantics or ABI guarantee.

## Not implemented or incomplete

- Generic function lowering/specialization and multi-file module compilation.
- Tensor construction and operations. Tensor values can only pass through as
  opaque pointers where the HIR already provides a value.
- Runtime implementations for `say`, checked conversions, text, and list
  helpers. The IR declares these helpers, but does not define or link them.
- Target triple, data layout, target-specific ABI, machine-code generation,
  linking, and execution.

Unsupported constructs produce diagnostics instead of partial IR. The output
uses opaque pointers and should be validated with a compatible LLVM tool if one
is already installed. LLVM tools are optional and are not downloaded or
required by the build. For example:

```sh
/tmp/indicode-build/indicode ir examples/ir_scalar.indi > /tmp/indicode.ll
llvm-as /tmp/indicode.ll -o /tmp/indicode.bc
opt -passes=verify -disable-output /tmp/indicode.bc
```
