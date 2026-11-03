# Indicode Front-End Progress

**Front-end package version:** 0.9.8 (pre-release; not a complete compiler).
**Updated:** 2026-11-03

## Scope and status

This C++20 package implements an Indicode compiler front end that reads source,
builds an AST, performs supported name/type/semantic checks, and lowers valid
input to project-owned typed HIR that is independently verified. It does not
generate LLVM IR or machine code, execute programs, or provide tensor/device
backends. LLVM is not downloaded, vendored, or built here.

The code audit found implementations and focused tests for all **40/40
catalogue items (IDs 445-484)** at their stated v1 or public-API acceptance
level. This is an item count, not a claim that every API is integrated into
source syntax or that the full language/compiler is complete. The separate
weighted front-end readiness estimate is **97.25% (rounded to 97%)** because
conformance, editor integration, release/CI, fuzzing, sanitizer, and
compatibility work remains. See the [status report](./FRONT_END_STATUS_REPORT.md)
and [code-evidence audit](./FRONT_END_REMAINING_WORK.md).

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

## Weighted readiness estimate

The estimate is a judgment-based measure of acceptance maturity; it is not
derived from the count of catalogue entries and does not measure the complete
compiler.

| Acceptance area | Weight | Completion | Weighted points |
|---|---:|---:|---:|
| Project/build foundation | 5% | 95% | 4.75 |
| Source manager and diagnostics | 10% | 97% | 9.70 |
| Lexer | 15% | 92% | 13.80 |
| Parser and AST | 15% | 99% | 14.85 |
| Name resolution | 10% | 96% | 9.60 |
| Type checking and semantic validation | 15% | 99% | 14.85 |
| Typed HIR and verification | 15% | 99% | 14.85 |
| CLI and diagnostics tooling | 5% | 99% | 4.95 |
| Tests, docs, and integration | 10% | 99% | 9.90 |
| **Total** | **100%** |  | **97.25%** |

The remaining estimate reflects broader acceptance gaps: language conformance,
resolver/editor integration, terminal display-width handling, sanitizer and
fuzz gates, CI/release practices, and package/JSON compatibility policy. These
gaps do not mean a catalogue row lacks code; they do mean the overall front-end
should not be called fully production-ready.

## Verification

The current CMake/CTest suite contains **23 tests**. A fresh Release configure,
build, and `ctest --output-on-failure` run passed **23/23**. Coverage includes
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

1. Decide whether multi-file module/import and overload support should become
   source-language features, then integrate the existing APIs if approved.
2. Expand requirement-linked conformance coverage; add coverage-guided fuzzing
   and sanitizer gates.
3. Add reproducible CI/release workflows and compatibility policies for the
   package and JSON schemas; test more compilers/platforms.
4. Build a separate, optional LLVM adapter only if/when backend generation is
   in scope. Keep LLVM out of the default front-end build.

Tensor execution, machine-code generation, runtime behavior, autodiff,
training, and device backends are outside package 0.9.8.
