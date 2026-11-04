# Indicode Front End

**Package version: 1.0.0.** This project builds the C++20 front end for the proposed Indicode language and a textual LLVM IR generator that lowers 100% of the documented single-file v1 constructs. It does not provide runtime helpers, generate machine code, link, or execute programs; those belong to a separate backend/runtime scope. LLVM is not downloaded, vendored, or built here.

The documented front-end v1/API catalogue is **100% implemented and focused-tested (40/40 items, IDs 445–484)**, and the textual LLVM IR generator lowers **100% of documented single-file v1 constructs**. Together, they complete the requested front-end + IR milestone within its stated scope. This is not a claim that every possible language feature or production/release criterion is complete. Module/import/overload resolver APIs are not connected to the single-file v1 parser, semantic analyzer or CLI, and the HIR symbol index is not an LSP server. AST and typed-HIR JSON use schema version 2; diagnostic JSON uses schema version 1. The source grammar contract is version 1.0.0, separate from the package version.

Read the [combined front-end + LLVM IR status](docs/COMPILER_FRONT_END_LLVM_IR_STATUS.md) and its [single consolidated PDF](Compiler_Front_End_and_LLVM_IR_Status.pdf), the [front-end status page](docs/FRONT_END_STATUS_REPORT.md), the [40-item code audit](docs/FRONT_END_REMAINING_WORK.md), [docs/LANGUAGE_SPEC_V1.md](docs/LANGUAGE_SPEC_V1.md) for accepted syntax, [docs/SEMANTICS_V1.md](docs/SEMANTICS_V1.md) for static rules, [docs/AST_CONTEXT_V1.md](docs/AST_CONTEXT_V1.md) for AST ownership, [docs/DIAGNOSTICS_V1.md](docs/DIAGNOSTICS_V1.md) for diagnostic JSON, [docs/PROGRESS.md](docs/PROGRESS.md) for evidence, [docs/ROADMAP_BEGINNER.md](docs/ROADMAP_BEGINNER.md) for a plain-language guide, and [docs/COMMUNITY_RULES.md](docs/COMMUNITY_RULES.md) for contribution rules.

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
/tmp/indicode-build/indicode ir examples/ir_scalar.indi
/tmp/indicode-build/indicode ir examples/ir_generics.indi
/tmp/indicode-build/indicode check examples/records.indi
/tmp/indicode-build/indicode check examples/tensor_shapes.indi
/tmp/indicode-build/indicode --json-diagnostics check tests/fixtures/type_error.indi
```

`--json-diagnostics` prints machine-readable errors for editor/CI tools. It is an option before the command and source path.

`typed-json` prints verified project-owned HIR in a JSON envelope with `schemaVersion: 2`, including source spans and symbol IDs for a future backend adapter.

`ir` emits textual LLVM IR for all documented single-file v1 constructs, including generic specialization, as documented in [LLVM_IR_GENERATOR_V0.md](docs/LLVM_IR_GENERATOR_V0.md). It requires no LLVM installation. Existing compatible LLVM tools can optionally validate the output; runtime helpers, machine-code generation, and linking are not provided.

`ast-json` serializes the parsed AST before semantic analysis. Its version-2 JSON includes declarations, node kinds, generic/effect/ownership metadata, source spans, expression metadata, and nested statements. Diagnostic JSON is independently versioned at schema version 1.

See [docs/PROGRESS.md](docs/PROGRESS.md) for measured progress, [docs/ROADMAP_BEGINNER.md](docs/ROADMAP_BEGINNER.md) for the plain-language roadmap and current syntax examples, and [docs/COMMUNITY_RULES.md](docs/COMMUNITY_RULES.md) for strict contribution rules.

## Production readiness

This is a buildable, tested front-end prototype. Front-end feature-catalogue completion and IR lowering coverage are scoped to their documented v1 contracts; broader conformance, fuzzing/sanitizer coverage, CI/release practices, and a stable compatibility policy remain. Do not rely on it for production workloads.