# Indicode Front-End Progress

**Package version:** 0.9.8 (pre-release).
**Updated:** 2026-11-04

## Scope and status

This C++20 package implements the documented Indicode front end and textual
LLVM IR generator milestone. It reads supported source, builds an AST, checks
names/types/semantics, independently verifies typed HIR, and lowers supported
single-file v1 constructs to textual LLVM IR. It does not generate machine
code, execute programs, or provide tensor/device backends. LLVM is not
downloaded, vendored, or built here.

The code audit found implementations and focused tests for all **40/40
catalogue items (IDs 445-484)** at their stated v1 or public-API acceptance
level. The textual LLVM IR generator lowers **100% of documented single-file
v1 constructs**. Together, these complete the requested milestone within its
declared scope. This does not claim that every API is integrated into source
syntax or that the full language/backend is complete. See the [combined
status report](./COMPILER_FRONT_END_LLVM_IR_STATUS.md), the [front-end status
page](./FRONT_END_STATUS_REPORT.md), and the [40-item code audit](./FRONT_END_REMAINING_WORK.md).

The source grammar contract is **1.0.0**; it is separate from the package
version **0.9.8**. Diagnostic JSON uses schema version 1. AST JSON and typed-HIR
JSON use schema version 2.

## Implemented code areas

- **Source and lexer:** owned source buffers, spans, indexed CR/LF/CRLF line
  tables, byte offsets, UTF-8 scalar columns, four-column tab stops, UTF-8
  validation, typed literal/keyword payloads, trivia, nested comments, and
  recoverable lexical diagnostics. Identifiers are ASCII; numeric suffixes,
  raw strings, and multiline strings are not supported.
- **Grammar and AST:** recursive-descent declarations/statements and Pratt
  expressions; records, lists, calls/named arguments, indexing/member access,
  loops/ranges, nested `else if`, generic function declarations, ownership
  parameter modes, and effect annotations. Recovery, deterministic printing,
  exhaustive visitors, AST JSON v2, and the `unique_ptr` ownership contract
  are tested. Grammar version 1.0.0 excludes module/import syntax, overload
  declarations, and generic record declarations.
- **Names and semantics:** lexical scopes, stable symbol IDs, name/type
  checking, generic `Numeric`/`Integer` constraint inference and substitution,
  explicit checked numeric conversions, records, collections, exact static
  tensor-shape checks, bounded integer/Boolean constant evaluation, and
  conservative control-flow/return checks.
- **Effects and ownership:** effects `io`, `allocation`, and `mutation` are
  inferred, propagated through calls, and checked against annotations when
  present. The limited ownership checker validates parameter/call modes,
  moves, use-after-move, and call-scoped borrow conflicts. It is not a general
  lifetime, escape, or unsafe-code checker.
- **HIR and tools:** checked AST lowers to project-owned typed HIR; an
  independent verifier checks types, symbols, scopes, calls, control flow,
  ownership metadata, and effect summaries. The CLI provides `check`, `tokens`,
  `ast`, `ast-json`, `typed`, `typed-json`, and `--json-diagnostics`.
- **Library APIs:** module path/import/overload resolution is implemented and
  tested, but not connected to the single-file parser, semantic analyzer, or
  CLI. The HIR symbol/reference index is a tested library API, not an LSP.
  Incremental parsing reuses unchanged top-level fragments across source
  edits; tests compare results with clean parses.
- **Packaging:** CMake installs/exports the library and CLI. A separate CMake
  consumer test verifies installed-package use.
- **LLVM IR generator:** 100% of documented single-file front-end v1
  constructs lower to textual IR, including generic function
  monomorphization, globals, records/collections, and control flow. This is not
  runtime or ABI completeness: helper implementations, machine-code
  generation, linking, and execution are outside scope. Tensor operations,
  modules, and generic records are not in front-end v1. LLVM is optional and
  never downloaded; see [LLVM_IR_GENERATOR_V0.md](./LLVM_IR_GENERATOR_V0.md).

## Defined-scope completion

| Milestone measure | Completion |
|---|---:|
| Front-end v1/API catalogue | **40/40 (100%)** |
| Textual LLVM IR lowering of documented single-file v1 constructs | **100%** |
| Combined requested front-end + IR milestone | **100% within the stated scope** |
| Current test suite in this environment | **28/28 passed** |

This feature-completion measure does not include future language extensions,
LSP integration, release engineering, runtime implementations, target ABI,
machine code, linking, or execution. The broad language/backend catalogue is
not the denominator for this front-end + textual-IR milestone.

## Verification

The current CMake/CTest suite contains **26 base tests**, plus two optional
LLVM assembly/verifier tests when `llvm-as` and `opt` are already installed.
The current environment has both tools, so a fresh configure, build, and
`ctest --output-on-failure` run passed **28/28**. Coverage includes
source mapping, lexer payloads/trivia, grammar/operators, parser recovery,
AST printing/visiting/scalability, semantics, HIR verification, resolution
APIs, symbol indexing, incremental parsing, CLI JSON/diagnostics, and an
installed-package consumer. Passing tests prove their tested cases, not
exhaustive conformance.

Reproduce the build from this project directory:

```sh
cmake -S . -B /tmp/indicode-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/indicode-build
ctest --test-dir /tmp/indicode-build --output-on-failure
```

## Remaining work

1. If desired, consider integrating module/import/overload APIs into a future
   source grammar version.
2. Expand conformance coverage, fuzzing, sanitizers, CI/release workflows,
   package/JSON compatibility policies, and compiler/platform checks.
3. Any work beyond textual LLVM IR, such as runtime helpers or machine code,
   requires a separate backend/runtime decision and is not part of this
   milestone.

Tensor execution, machine-code generation, runtime behavior, autodiff,
training, and device backends are outside package 0.9.8.
