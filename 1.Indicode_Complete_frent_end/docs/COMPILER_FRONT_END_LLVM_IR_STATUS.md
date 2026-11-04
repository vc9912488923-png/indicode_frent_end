# Indicode Compiler Front-End and LLVM IR Status

**Status date:** 2026-11-04  
**Package version:** 1.0.0
**Grammar version:** 1.0.0  
**Milestone:** Compiler front end plus textual LLVM IR generation; no machine-code backend.

## Executive summary

The requested milestone is complete **within the documented single-file
front-end v1 and public-API scope**:

| Area | Completion | Evidence |
|---|---:|---|
| Front-end v1/API feature catalogue | **100% (40/40 items)** | Implementation and focused tests at each item's documented acceptance level; IDs 445–484 |
| Textual LLVM IR lowering | **100% of documented single-file v1 constructs** | IR generator tests and optional LLVM assembly/verifier tests |
| Combined front-end + textual IR milestone | **100% of the defined scope** | The supported source subset lowers through verified HIR to textual LLVM IR |
| Current test run | **28/28 passed** | 26 base CTest cases and two optional tests using already-installed `llvm-as` and `opt` |

This is a scoped feature-completion statement, **not** a claim that every
conceivable Indicode feature or a production-ready compiler is complete. The
front end accepts the documented single-file v1 grammar. A few separately
implemented resolver/index library APIs are not connected to that grammar or
an LSP. The exact accepted syntax is in
[LANGUAGE_SPEC_V1.md](./LANGUAGE_SPEC_V1.md); static rules are in
[SEMANTICS_V1.md](./SEMANTICS_V1.md).

## What “100%” covers

The front end reads source, tokenizes and parses supported constructs, checks
names/types/semantics, produces typed HIR, and independently verifies that
HIR. The IR generator then lowers all documented single-file v1 constructs
from verified HIR to textual LLVM IR.

| Front-end area | Delivered v1/API scope |
|---|---|
| Source and diagnostics | Source buffers/spans, line and column mapping, UTF-8 validation, source-aware diagnostics, fix-its, and diagnostic JSON |
| Lexer and parser | Documented token/literal forms, recursive-descent declarations/statements, Pratt expressions, recovery, grammar version 1.0.0 |
| AST | Owned AST, deterministic printer, visitor, JSON schema v2, documented ownership/lifetime |
| Names and semantics | Lexical scopes, stable symbols, name/type checks, generic `Numeric`/`Integer` inference, effects, limited ownership checks, constant and control-flow checks |
| Typed HIR | AST lowering, schema-versioned output, independent validation of supported types, scopes, calls, effects, ownership metadata, and returns |
| Supporting APIs | Module/import/overload resolution, HIR symbol/reference index, and incremental parsing implemented and tested at their stated library-API scope |
| CLI and packaging | `check`, `tokens`, `ast`, `ast-json`, `typed`, `typed-json`, `ir`, and CMake install/export package |

The complete 40-item audit, including evidence and per-item boundaries, is
maintained in [FRONT_END_REMAINING_WORK.md](./FRONT_END_REMAINING_WORK.md).
“Remaining work” in that audit means optional future integration or broader
acceptance beyond this milestone, not an unfinished item in the 40-item
catalogue at its stated v1/API scope.

## LLVM IR lowering coverage

| Construct family | Lowering status |
|---|---|
| Functions and calls | Non-generic functions; on-demand generic specialization, including nested and recursive generic calls |
| Values and data | Parameters, locals, globals, assignment, records, lists, ranges, text, and opaque-pointer tensor values |
| Expressions | Integer/floating arithmetic, comparisons, unary operators, checked-conversion helper calls, ownership wrappers, short-circuit Boolean logic |
| Control flow | `if`/`else if`/`else`, `while`, `repeat`, and `for` over integer ranges, lists, or text |
| Program initialization | Generated `indicode_entry` initializer when top-level statements exist |
| Output | Valid textual LLVM IR for supported constructs; failures produce diagnostics rather than partial IR |

The detailed lowering contract and type mappings are in
[LLVM_IR_GENERATOR_V0.md](./LLVM_IR_GENERATOR_V0.md).

## Explicitly outside this milestone

These are intentional boundaries, not claims of incomplete work on the
requested front-end + textual-IR milestone:

- LLVM framework libraries, downloading/building LLVM, machine-code
  generation, target ABI/data layout, linking, and program execution.
- Runtime implementations for helper declarations such as `say`, checked
  conversions, text, and list helpers.
- Tensor operations/device execution; v1 tensor values are opaque pointers.
- Module/import compilation, overload syntax, generic record declarations,
  explicit generic call-site arguments, and syntax not accepted by grammar v1.
- Full lifetime/escape analysis, LSP/editor protocol, sanitizer/fuzz/release
  gates, and production compatibility guarantees.

LLVM itself is not a build dependency. If compatible `llvm-as` and `opt`
executables are already installed, CMake adds optional IR validation tests; it
does not download them.

## Verification

The recorded validation used:

```sh
cmake -S . -B /tmp/indicode-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/indicode-build
ctest --test-dir /tmp/indicode-build --output-on-failure
```

Result: **28/28 tests passed** in the environment with `llvm-as` and `opt`
available. The suite covers front-end components, HIR verification, CLI output,
generic/scalar IR generation, LLVM assembly and verification, and installed
package consumption. Tests provide evidence for the covered cases; they are
not a proof of support for language features outside the documented scope.

## Related documentation

- [Front-end status report](./FRONT_END_STATUS_REPORT.md) — progress details.
- [Front-end code audit](./FRONT_END_REMAINING_WORK.md) — all 40 catalogue items.
- [LLVM IR generator scope](./LLVM_IR_GENERATOR_V0.md) — lowering details and limits.
- [Beginner guide and roadmap](./ROADMAP_BEGINNER.md).
- [Progress and verification details](./PROGRESS.md).
