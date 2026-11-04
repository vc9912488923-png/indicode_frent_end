# Indicode Front-End Guide and Roadmap

**Front-end package version:** 1.0.0.
**Updated:** 2026-11-04

## What is this project?

This project reads Indicode source and checks the supported language subset.
It can show tokens, an abstract syntax tree (AST), diagnostics, or a verified
typed representation called HIR. It does **not** turn source into machine code
or run programs, so it is a compiler front end, not a complete compiler.

The project emits textual LLVM IR for all documented single-file v1 constructs
from verified HIR. It does not
download, copy, or build LLVM; compatible installed LLVM tools can optionally
verify that text. It does not produce machine code, link, or run programs.

## Current status

The code audit found implementations and focused tests for all **40/40
front-end catalogue items (IDs 445-484)** at their listed v1 or library-API
scope. The textual LLVM IR generator lowers **100% of documented single-file
v1 constructs**. Together, they complete the requested milestone within its
defined scope; this does not mean every possible Indicode program is supported.

Some pieces are library APIs rather than language syntax:

- Module/import resolution and overload candidate selection are implemented
  and tested, but are not connected to the single-file parser, semantic
  analyzer, or CLI.
- The symbol/reference index operates on typed HIR snapshots; it is not a
  language server (LSP).
- Incremental parsing reuses unchanged top-level fragments after source edits.

See the [combined front-end + LLVM IR status](./COMPILER_FRONT_END_LLVM_IR_STATUS.md), the [status page](./FRONT_END_STATUS_REPORT.md), the [40-item code audit](./FRONT_END_REMAINING_WORK.md), and the [progress details](./PROGRESS.md).
The source grammar and package are both version 1.0.0; they version different
contracts.
AST/HIR JSON use schema version 2; diagnostic JSON uses schema version 1.

## What works in the supported language

- Functions, records, typed fields, values, assignments, conditionals,
  `while`/`repeat`/`for` loops, integer ranges, lists, and named arguments.
- Name and type checks, stable symbol identities, checked numeric conversions,
  record construction/member reads, and static/symbolic tensor-shape checks.
- Generic functions with inferred type parameters and `Numeric`/`Integer`
  constraints.
- Effect annotations for `io`, `allocation`, and `mutation`.
- Limited ownership modes (`move`, `borrow`, `borrow_mut`) with call-mode,
  use-after-move, and borrow-conflict checks. This is not a full lifetime or
  escape checker.
- Source-aware text/JSON diagnostics, parser recovery, typed HIR lowering and
  independent HIR verification.

For example, the supported front end can check:

```indi
function add<T: Numeric>(left: T, right: T) -> T {
    return left + right
}

function announce(value: borrow List<Int>) effects(io) {
    say(value[0])
}

answer = add(20, 22)
```

The front end checks this source; it does not execute it. The language
specification and semantic rules document exact syntax and limits.

## Known boundaries

- The parser handles one source file. Module/import syntax and overload
  declarations are not part of grammar v1 even though resolver APIs exist.
- Generic functions are supported; generic record declarations and explicit
  type arguments at call sites are not.
- Identifiers are ASCII. Numeric suffixes, raw strings, and multiline strings
  are not supported. Terminal-specific wide/combining-character display-width
  alignment is not implemented.
- Tensor support checks types/shapes only; it does not construct or execute
  tensors, broadcast shapes, or select devices.
- AST and HIR JSON are serialization-only; deserialization and long-term
  compatibility guarantees are not implemented.
- No machine code, executable runtime, GPU/device backend, autodiff, or training
  system is included. The current IR subset and its limits are in
  [LLVM_IR_GENERATOR_V0.md](./LLVM_IR_GENERATOR_V0.md).

## Roadmap

| Area | Current status | Next work |
|---|---|---|
| Source, lexer, parser, AST | Implemented and focused-tested for the documented subset | Broader language conformance, Unicode identifiers, additional literal forms |
| Name/type checking and HIR | Implemented and independently verified for supported syntax | Expand conformance and schema compatibility guarantees |
| Module/import/overload resolution | Public APIs implemented and tested | Decide whether and how to integrate into a future grammar/CLI |
| Symbol/reference index | HIR snapshot library API implemented and tested | Editor/LSP integration if required |
| Incremental parsing | Top-level fragment reuse implemented and tested | Expand editor-oriented workflows and benchmarks |
| Release readiness | Not complete | CI, supported compiler/platform matrix, fuzzing, sanitizers, compatibility policy |
| LLVM IR text output | 100% of documented single-file front-end v1 constructs, including generic specialization | Runtime helpers, target ABI, machine code, linking, and execution are separate/out of scope |
| Machine code/runtime | Out of scope | Separate future decision |

The roadmap items above are broader future integrations and release goals;
they are not required for completion of the documented single-file v1
front-end + textual LLVM IR milestone.

## Build and test

Run these commands from `1.Indicode_Complete_frent_end`:

```sh
cmake -S . -B /tmp/indicode-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/indicode-build
ctest --test-dir /tmp/indicode-build --output-on-failure
```

The latest recorded run passed all **28 CTest cases**. When `llvm-as` and
`opt` are already installed, CMake registers two optional LLVM IR verifier
tests. For example:

```sh
/tmp/indicode-build/indicode check examples/hello.indi
/tmp/indicode-build/indicode ast-json examples/records.indi
/tmp/indicode-build/indicode typed-json examples/records.indi
/tmp/indicode-build/indicode --json-diagnostics check tests/fixtures/type_error.indi
```

A successful `check` means the input passed the implemented front-end
validation. It does not mean the program was compiled to machine code or run.

## Glossary

- **Lexer:** turns source characters into tokens such as names, numbers, and
  punctuation.
- **Parser:** checks token order against the grammar and builds an AST.
- **AST:** a tree describing the structure of source code.
- **Name resolution:** connects a name use to the declaration it refers to.
- **Type checking:** checks that values and operations use compatible types.
- **HIR:** this project's typed intermediate representation for a future
  backend; it is not LLVM IR.
- **Verifier:** checks that an HIR value obeys the front end's representation
  rules.
- **LSP:** Language Server Protocol, a standard interface for editor features.
  The current symbol index is not an LSP implementation.
