# LLVM IR Generator v0

This generator is part of the completed front-end + textual-IR milestone.
See the [consolidated status report](./COMPILER_FRONT_END_LLVM_IR_STATUS.md)
and its [single PDF](../Compiler_Front_End_and_LLVM_IR_Status.pdf) for the
combined completion and verification summary.

The generator consumes independently verified, project-owned HIR and emits
textual LLVM IR. It does not download, build, or link LLVM libraries, invoke
LLVM during normal compilation, produce machine code, provide a runtime, link
programs, or execute them.

**Completion: 100% of the documented single-file front-end v1 constructs have
a textual IR lowering.** This is feature-scope completion, not a claim of
runtime/ABI completeness, test coverage, or production readiness. It excludes
syntax and behavior outside the front-end v1 contract.

## Lowered constructs

- Non-generic functions and on-demand monomorphizations of generic functions,
  including nested and recursive generic calls.
- Parameters, local and top-level bindings, assignment, returns, direct calls,
  and named arguments. Call arguments are evaluated in source order and emitted
  in parameter order.
- `Bool`, all v1 integer and floating-point types, `Char`, `Text`, records,
  `List<T>`, `Range<T>`, and opaque-pointer tensor values.
- Arithmetic, comparisons, unary operators, checked-conversion helper calls,
  ownership markers as value-preserving wrappers, and short-circuit Boolean
  logic.
- `if`/`else`/`else if`, `while`, `repeat`, and `for` over integer ranges,
  lists, or text.
- Record construction and member reads; list construction and indexing; text
  literals, indexing, and comparisons; range construction and iteration.
- A generated `indicode_entry` initializer when top-level statements exist.

Integer mappings follow the front end's range rules: `Byte`/`U8` map to `i8`,
`Int2`/`UInt2` to `i16`, `Int`/`UInt` to `i32`, and `Long`/`ULong` to `i64`.
`Bool` and `Char` map to `i1` and `i32`; floating-point types map to their
corresponding LLVM types. Tensors are opaque `ptr` values: the v1 language has
no tensor operations to lower. Arithmetic is emitted without overflow flags.
This mapping does not promise a platform ABI or define runtime behavior.

## Explicitly outside this milestone

- Module/import compilation, overload syntax, generic record declarations,
  and tensor operations are not part of front-end grammar/semantics v1.
- Runtime implementations for `say`, checked conversions, text, and list
  helpers. The IR declares helper signatures; consumers must provide compatible
  implementations to link or run a program.
- Target triple, data layout, target-specific ABI, machine-code generation,
  linking, and execution.

The generator reports diagnostics instead of returning partial IR for invalid
or unsupported HIR. It uses opaque pointers. LLVM tools are optional and are
never downloaded or required. If compatible tools already exist, CMake
registers additional IR assembly/verifier tests. Manual validation is:

```sh
/tmp/indicode-build/indicode ir examples/ir_generics.indi > /tmp/indicode.ll
llvm-as /tmp/indicode.ll -o /tmp/indicode.bc
opt -passes=verify -disable-output /tmp/indicode.bc
```
