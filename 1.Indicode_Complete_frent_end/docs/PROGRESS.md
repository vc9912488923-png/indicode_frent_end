# Indicode Front-End Progress

Updated: 2026-11-03

## Scope

This phase builds only the Indicode front end, targeting a validated typed representation that a future LLVM backend can consume. It now includes an initial tensor type and static/symbolic shape-compatibility subset; tensor operations, broadcasting, dynamic shape guards, and device checks are not implemented. LLVM is not downloaded, vendored, or built in this repository. LLVM IR generation, machine-code generation, tensor execution, autodiff runtime/training, and device backends are outside the current phase.

## Production Readiness

This repository is a real, buildable, tested compiler-front-end prototype, but it is **not production-ready** and should not be represented as a deployable general-purpose compiler. The supported grammar and semantics are a small documented subset. There is no LLVM adapter or executable generation, language conformance suite, production CI/release process, fuzzing campaign, sanitizer gate, or compatibility/ABI policy. Use it for development and evaluation of the implemented subset, not as a production compiler.

## Completion

**Overall front-end completion: 97%**

The percentage measures progress toward the agreed front-end-only boundary in the [detailed status report](./FRONT_END_STATUS_REPORT.md) and [PDF](../Compiler_Front_End_Status_Report.pdf), not progress toward LLVM itself or the full compiler. It is a weighted estimate: each area's weight is fixed, and completion credit requires implemented, tested, documented acceptance behavior. The catalogue audit now records **35/40 done, 0 partial, and 5 not started**. AST ownership measurement (ID 460), the HIR symbol/reference index API (ID 483), and module/import/overload resolution APIs (IDs 468–470) are accepted. The weighted estimate is **97.25%, rounded to 97%**; grammar/analyzer integration for multi-file modules and overload calls, generics, incremental parsing, production readiness, and full conformance remain unfinished.

| Acceptance area | Weight | Area completion | Evidence / remaining work |
| --- | --- | --- |
| Project/build foundation | 5% | 95% | C++20, CMake, CTest, install/export package config, public headers and a separately configured/compiled package consumer; CI/release acceptance remains. |
| Source manager and diagnostics | 10% | 97% | Source manager owns immutable-by-interface buffers, stable file IDs, indexed CR/LF/CRLF line tables, byte-to-line/scalar-column mapping, source spans, tab-aware carets, secondary notes, JSON escaping, and validated fix-it edits; terminal display-width handling remains. |
| Lexer | 15% | 92% | ASCII identifiers, typed keyword and literal payloads, exact-source trivia, UTF-8 source validation, strings/escapes, binary/octal/hex integers, digit separators, nested block comments with newline preservation, and scalar/tab-aware source columns; Unicode identifiers, suffix policy, and complete numeric grammar remain. |
| Parser and AST | 15% | 99% | Grammar v1.0.0 specifies the accepted syntax; deterministic AST golden output, an exhaustive typed visitor, explicit ownership/lifetime rules, and declaration/statement/expression recovery are tested; broader grammar and conformance testing remain. |
| Name resolution | 10% | 96% | Separate v1 namespaces, nested scopes/shadowing, stable identities, and tested module/import and exact-signature overload resolution APIs; grammar/analyzer integration remains. |
| Type checking and semantic validation | 15% | 99% | Documented v1 rules cover declarations, expressions/operators/calls/conversions, records, collections, tensor shapes, constant evaluation, and conservative control-flow/return validation; ownership/effects and tensor operations remain outside v1. |
| Typed HIR and verification | 15% | 99% | Project-owned typed HIR carries stable local/function/type/field identities and schema-versioned JSON; verifier rechecks lexical visibility, identity uniqueness/references, immutability, return paths, and expression/call/loop contracts. Deserialization and compatibility policy remain. |
| CLI and diagnostics tooling | 5% | 99% | `check`, `tokens`, `ast`, `ast-json`, `typed`, `typed-json`, source excerpts/carets aligned across tabs and UTF-8 scalar columns, schema-v1 JSON diagnostics, and HIR symbol/reference index; terminal-specific display widths and editor-protocol integration remain. |
| Tests, docs, and integration | 10% | 99% | Twenty-two CTest cases cover source mapping, lexer/trivia, grammar/operators, AST golden output and exhaustive visitor coverage, AST lifetime/scalability, symbol index, module/import/overload resolution, semantic checks, HIR identities, diagnostics, CLI, malformed-input smoke testing, and installed-package consumption; full conformance, fuzzing, sanitizers, and CI remain. |

Updated weighted estimate: 4.75 + 9.7 + 13.8 + 14.85 + 9.6 + 14.85 + 14.85 + 4.95 + 9.9 = **97.25%, rounded to 97%**. This is a judgment-based estimate for the implemented front-end boundary, including the resolver library APIs; it is not a claim that 97% of a complete language specification or all possible compiler work is done.

## Work completed

- Added a C++20 CMake library and a CTest-based lexer test executable.
- Added source locations/spans, source-file storage, diagnostic records, and token definitions.
- Added an owning source manager with source IDs, indexed line lookup, CR/LF/CRLF handling, UTF-8 scalar/tab-aware byte-offset mapping, and CLI/diagnostic integration.
- Implemented initial lexing for identifiers, selected keywords, numeric and string literals, punctuation, operators, `#` line comments, and newline separators.
- Added stable lexer error codes for unsupported characters, malformed logical operators, malformed exponents, and unterminated strings.
- Added tests covering function syntax, CRLF line endings, comments, strings, EOF, and recoverable malformed literals.
- Configured and built with GCC 13.3 and CMake; the lexer CTest target passes (1/1).
- Added an AST model and parser for function signatures, variable declarations, assignment, return, if/else, while, repeat, calls, and precedence-based expressions.
- Added parser tests for function/body structure, expression precedence, recoverable syntax errors, and empty input; lexer and parser CTest targets pass (2/2).
- Added scope-based name lookup, function signature checks, inferred types on expressions, basic mutability/type checks, and return-path validation for simple branches.
- Added the `indicode` CLI with `check`, `tokens`, `ast`, and `typed` commands, plus valid/invalid CLI integration fixtures.
- Added a source-oriented AST/typed-tree printer and documented local build and command usage.
- Added schema-versioned JSON serialization for the parsed AST, retaining declarations, expression/statement kinds, source spans, named argument labels, and nested statements; exposed it as `indicode ast-json` and added structure/span/escaping tests.
- Added context-typed integer literals with range checks for Byte/U8, Int2/UInt2, Int/UInt, and Long/ULong; signed minimum values are checked without overflow.
- Added `examples/hello.indi` as the first runnable end-to-end front-end sample.
- Added project-owned typed HIR types and AST-to-HIR lowering without LLVM headers, libraries, downloads, or build integration.
- Added an HIR verifier for type annotations, expression structure, operator consistency, call signatures, statement types, and return types; `indicode typed` now prints verified HIR.
- Added HIR tests for valid lowering/verification, typed output, and rejection of an invalid type annotation.
- Added source-line/caret text diagnostics and opt-in `--json-diagnostics` with stable codes, escaped messages, file paths, and structured source spans.
- Added homogeneous list literals, list/text indexing, and `for item in items` for-each loops through AST, semantic checking, HIR, verifier, printer, and CLI.
- Added UTF-8 validation for string contents, common string escape checks, `\u{...}` Unicode scalar validation, and Char/Text inference by scalar count.
- Added lexer, semantic, HIR, diagnostic, and JSON CLI regression tests for the new behavior.
- Documented supported list/iteration/index and Unicode-string rules, including positive/negative examples and current limits.
- Added strict community contribution rules and a beginner-readable roadmap explaining the LLVM boundary and percentage calculation.
- Added binary/octal/hexadecimal integer literals and underscore separators with malformed-digit/separator diagnostics and destination range checks.
- Added named function arguments end-to-end, including parameter-name resolution, duplicate/unknown/missing argument errors, and HIR preservation.
- Added nested block comments, preserved their newline separators, and diagnosed unterminated comments.
- Added record declarations, named record construction, and typed member access through parsing, semantic analysis, HIR lowering/verification, printers, and CLI coverage.
- Added checked integer conversion calls to all supported integer destination types; conversion requires a numeric input and `checked: true`, and integer constants are range-checked before lowering.
- Added checked numeric conversion calls to `Half`, `Float`, and `Double`, with compile-time finite/range checks for numeric literals.
- Added `Tensor<Element, [Dimension, ...]>` types with numeric element validation and static/symbolic dimension compatibility through signatures, semantic checking, HIR, and CLI example coverage.
- Improved declaration-level parser recovery so malformed function/record declarations do not consume later valid declarations; added a deterministic 1,000-input lexer/parser robustness smoke test.
- Added `start..end` integer range expressions with same-integer-type bounds, `Range<T>` HIR verification, and integer range iteration binding for `for` loops.
- Added nested `else if` parsing as nested conditional AST/HIR nodes, with return-path analysis across each branch.
- Added independent HIR verification that non-`Void` functions return on every supported conditional path.
- Strengthened independent HIR checks to reject duplicate parameter names, argument-label metadata that is not aligned with call operands, and returns outside functions.
- Aligned diagnostic carets with expanded tabs and UTF-8 scalar columns; terminal-specific wide/combining-character display widths remain out of scope.
- Added optional diagnostic fix-its with replacement spans, messages, text hints, and JSON output; the parser suggests inserting `: ` after a function parameter name when its colon is missing.
- Added semantic errors for decimal floating-point literals that overflow or underflow the supported host conversion range.
- Lexer source columns now count valid UTF-8 continuation bytes as part of one scalar and expand tabs to four-column stops; byte offsets remain unchanged for slicing and edits.
- The lexer rejects malformed UTF-8 in line and block comments as well as strings and other source tokens.
- Added stable IDs for local variables, parameters, and loop bindings through semantic analysis and AST-to-HIR lowering; HIR printing exposes those IDs for backend inspection.
- HIR verification now independently checks lexical name visibility, matching symbol identity and type, immutable assignments, duplicate declarations, loop scope boundaries, and global bindings visible from functions.
- Added CMake install/export rules, a versioned package config, and a CTest consumer that installs the library, configures a separate CMake project, links `indicode::frontend`, and runs it.
- Added canonical `typed-json` CLI output with schema version 1, typed nodes, source spans, argument labels, and stable local symbol IDs; a CMake JSON check validates the schema and a parameter reference.
- Completed the v1 AST-printer golden snapshot for every declaration, statement, expression kind, and grammar operator.
- Added expression-delimiter recovery for call arguments/list elements, retained valid sibling expressions, documented synchronization sets, and tested applied parser fix-its.
- Added stable per-analysis IDs for functions, record types/fields, builtins, type references, and calls; HIR verification rejects duplicate or mismatched identities.
- Specified v1 namespaces and semantic typing rules, including conservative branch/loop return analysis, exact tensor shape compatibility, and which module/import/overload features are excluded.
- Added a bounded integer/Boolean constant evaluator with short-circuiting, signed overflow/division-by-zero diagnostics, a 256-node budget, conversion-range integration, and boundary tests.
- Published diagnostic text/JSON policies with secondary notes and schema v1; full-output golden tests cover spans, fix-its, notes, escaping, and multiple diagnostics.
- Added semantic errors for unreachable statements, invalid `Void` values, duplicate declarations with previous-location notes, and invalid `say(Void)` calls; HIR independently rejects the latter.
- Added a 10,000-statement AST scalability test and measured parse/destruction behavior; documented retaining `unique_ptr` ownership rather than adding an arena.
- Added a typed-HIR symbol index for definitions, name/call/member references, lexical scope lookup, and full-snapshot invalidation keyed by source revision.
- Added public resolver APIs for dotted module paths, manifest imports with aliases/public exports/cycle detection, and exact-type overload candidate selection; parser/analyzer integration remains outside grammar v1.

## Current limitations

- Grammar v1.0.0 is specified in `LANGUAGE_SPEC_V1.md`; modules/imports, generic function/record declarations, overloads, and declaration visibility are explicitly outside this syntax version.
- Identifiers are ASCII-only. UTF-8 source sequences are validated, but Unicode identifier rules and full Unicode display-column mapping are not implemented.
- Numeric suffixes, raw strings, and the complete string escape repertoire remain incomplete.
- Semantic checking follows the documented single-file v1 rules; conversions require explicit checked mode and tensor compatibility is exact shape/type equality. Ownership/effects, tensor operations/broadcasting/device checks, and numeric widening are outside v1.
- Module/import and overload resolver APIs exist, but their syntax and semantic integration are not in grammar v1. Generic declarations are also excluded. Range runtime behavior is undefined because this front end does not execute programs.
- Typed HIR/verifier cover the supported v1 subset and verify stable identities; the symbol index is a single-file HIR snapshot API, not an LSP integration or incremental index. Deserialization, broader compatibility guarantees, incremental parsing, and full language conformance remain before an LLVM adapter is ready.

## Milestones

1. **Source model and lexer:** complete UTF-8/source management, token coverage, literal validation, and lexer tests.
2. **Parser and AST:** documented v1 grammar, precedence, source spans, deterministic printer output, and declaration/statement/expression recovery are implemented and tested.
3. **Names and types:** v1 scopes, identities, name resolution, typing, conversion/range checks, and diagnostics are implemented and tested.
4. **Core semantic validation:** v1 function/control-flow checks, mutability, constant evaluation, and negative tests are implemented.
5. **Typed HIR:** lowering, printer, verifier, schema-versioned JSON, symbol identity checks, and focused tests cover the supported v1 subset; deserialization and broader compatibility guarantees remain.
6. **Front-end CLI and release checks:** `check`, `tokens`, `ast`, `ast-json`, `typed`, `typed-json`, and schema-v1 JSON diagnostics exist; full conformance, fuzzing, sanitizers, and release/CI checks remain.

LLVM/backend work begins only after these front-end milestones are complete and accepted. The front end is being designed to connect to LLVM later without requiring the LLVM framework to be downloaded into this repository now.

## Latest verification

- The out-of-tree build in `/tmp/indicode-review-build` succeeds with GCC 13.3.
- All 22 CTest cases pass, including lexer payload/trivia contracts, versioned grammar and operator coverage, AST visitor/lifetime/scalability checks, symbol index definitions/references/scopes/invalidation, module/import/overload resolution, parser recovery/fix-its, semantic checks, HIR identity verification, diagnostic JSON golden output, CLI checks, and the installed-package consumer.
- The suite also verifies nested `else if` return coverage, malformed HIR return-path rejection, invalid HIR parameter/call/return/symbol-scope invariants, UTF-8/tab diagnostic caret alignment, and float literal overflow/underflow rejection.
- Lexer regression tests confirm that UTF-8 byte offsets and human-facing source columns remain distinct and consistent after tabs and non-ASCII string contents.
- The verification build was kept outside the project; no generated build files were added to the repository.