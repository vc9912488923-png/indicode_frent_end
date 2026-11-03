# Indicode Front End

This project is building the C++20 front end for the proposed Indicode language, targeting a validated typed representation that can later be connected to an LLVM backend. **LLVM is not downloaded, vendored, or built here.** LLVM IR generation, runtime execution, and AI/device backends are not implemented in this front-end phase.

Current estimated completion: **97%** (weighted estimate 97.25%). The project has documented v1 grammar and semantics, typed token payloads/trivia, parser/AST recovery, deterministic AST output and an exhaustive visitor, documented `unique_ptr` AST ownership with a 10,000-statement scalability test, stable identities and lexical scopes, bounded constant evaluation, control-flow/return checks, static tensor-shape checks, typed HIR verification, source-aware diagnostics, a HIR-backed symbol/reference index API, public module/import resolver APIs, exact-type overload candidate selection, effect and ownership checking, and incremental parse support with revision-based invalidation and clean reparsing. All 23 CTest cases pass. The 40-item front-end inventory records 38 done, 0 partial, and 2 not started. Resolver APIs are not wired into the v1 parser, semantic analyzer, or CLI; generic constraints/substitution and production readiness remain unfinished. Read the [detailed status report](docs/FRONT_END_STATUS_REPORT.md) or its [summary PDF](Compiler_Front_End_Status_Report.pdf), and the [40-item catalogue audit](docs/FRONT_END_REMAINING_WORK.md) or its [detailed PDF](Compiler_Front_End_Remaining_Work.pdf). LLVM is strictly kept outside the project build. Also see [docs/LANGUAGE_SPEC_V1.md](docs/LANGUAGE_SPEC_V1.md) for syntax, [docs/SEMANTICS_V1.md](docs/SEMANTICS_V1.md) for typing/binding rules, [docs/AST_CONTEXT_V1.md](docs/AST_CONTEXT_V1.md) for AST ownership, [docs/DIAGNOSTICS_V1.md](docs/DIAGNOSTICS_V1.md) for diagnostic JSON, [docs/PROGRESS.md](docs/PROGRESS.md) for evidence and the percentage formula, [docs/ROADMAP_BEGINNER.md](docs/ROADMAP_BEGINNER.md) for the beginner guide, and [docs/COMMUNITY_RULES.md](docs/COMMUNITY_RULES.md) for contribution rules.

## Build and test

```sh
cmake -S . -B /tmp/indicode-build
cmake --build /tmp/indicode-build
ctest --test-dir /tmp/indicode-build --output-on-failure
```

## Commands

```sh
/tmp/indicode-build/indicode check examples/hello.indi
/tmp/indicode-build/indicode tokens examples/hello.indi
/tmp/indicode-build/indicode ast examples/hello.indi
/tmp/indicode-build/indicode ast-json examples/records.indi
/tmp/indicode-build/indicode typed examples/hello.indi
/tmp/indicode-build/indicode typed-json examples/records.indi
/tmp/indicode-build/indicode check examples/records.indi
/tmp/indicode-build/indicode check examples/tensor_shapes.indi
/tmp/indicode-build/indicode --json-diagnostics check tests/fixtures/type_error.indi
```

`--json-diagnostics` prints machine-readable errors for editor/CI tools. It is an option before the command and source path.

`typed-json` prints verified project-owned HIR in a JSON envelope with `schemaVersion: 1`, including source spans and symbol IDs for a future backend adapter.

`ast-json` serializes the parsed AST before semantic analysis. Its version-1 JSON includes declarations, node kinds, source spans, expression metadata, and nested statements.

See [docs/PROGRESS.md](docs/PROGRESS.md) for measured progress, [docs/ROADMAP_BEGINNER.md](docs/ROADMAP_BEGINNER.md) for the plain-language roadmap and current syntax examples, and [docs/COMMUNITY_RULES.md](docs/COMMUNITY_RULES.md) for strict contribution rules.

## Production readiness

This is a buildable, tested front-end prototype for the documented language subset, not a production-ready general-purpose compiler. It does not generate LLVM IR or executables and still needs a complete language specification, conformance testing, fuzzing/sanitizer coverage, CI/release practices, and a stable compatibility policy. Do not rely on it for production workloads.