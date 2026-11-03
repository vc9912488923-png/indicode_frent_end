# Indicode Front-End Roadmap for Beginners

Updated: 2026-11-03

## What Are We Building?

We are building the part of a compiler that reads Indicode source code and checks what it means. It should eventually turn a correct program into a typed, verified Indicode representation that a separately installed LLVM backend can consume.

LLVM is a large, separate compiler framework. This project does **not** download, copy, or build LLVM. Our front end and its tests must keep working without LLVM. Connecting an optional backend is a later project stage, after the front end has a stable typed representation.

The [front-end status report](./FRONT_END_STATUS_REPORT.md) and its [summary PDF](../Compiler_Front_End_Status_Report.pdf) summarize completed and remaining work; the [detailed 40-item audit PDF](../Compiler_Front_End_Remaining_Work.pdf) lists each catalogue item. The previous all-area blueprint's 777 concepts were not 777 front-end requirements; the front-end section contains 40 catalogue entries. The editable audit now has 35 implemented to the audited acceptance level, 0 partial, and 5 not started. Three resolver items are implemented as public APIs, but are not yet integrated with v1 source syntax, semantic analysis, or the CLI. See the [versioned grammar specification](LANGUAGE_SPEC_V1.md), [semantic rules](SEMANTICS_V1.md), and [AST ownership contract](AST_CONTEXT_V1.md).

## Current Overall Progress: 97%

This percentage means estimated completion of the **front-end work needed before a future LLVM adapter**. It does not mean 97% of LLVM, machine-code generation, a GPU compiler, or the AI runtime is complete.

The estimate uses the weighted acceptance areas in `PROGRESS.md`:

- Records, named construction, member checking, checked numeric conversions, static/symbolic tensor shapes, improved declaration recovery, and parser robustness coverage have been added.
- Integer ranges and typed `for` bindings are supported, along with nested `else if` return-path analysis and HIR verification.
- Text diagnostic carets align with expanded tabs and UTF-8 scalar columns; full terminal display-width handling remains future work.
- Lexer columns also count UTF-8 scalars rather than encoded bytes and expand tabs to four-column stops; offsets remain byte-based.
- JSON diagnostics can include source edits (fix-its); missing function-parameter colons receive an insertion suggestion.
- Out-of-range decimal floating-point literals are rejected during semantic analysis.
- Variables, parameters, loop bindings, functions, record types/fields, and references receive stable IDs checked through HIR lowering and independent HIR verification.
- The library can be installed and used by a separate CMake consumer project.
- The source manager owns source buffers and maps byte offsets through indexed line tables, including CR/LF/CRLF, UTF-8 scalar columns, and tabs.
- Lexer tokens retain typed keyword/literal payloads, and comments and whitespace are preserved as source-spanned trivia; the recursive-descent and Pratt grammar is specified and versioned as v1.0.0.
- `indicode typed-json` emits verified HIR as JSON with a schema version and stable symbol IDs; deserialization and broader compatibility guarantees remain future work.
- `indicode ast-json` serializes parsed declarations and syntax nodes with source spans; deterministic v1 golden output is tested.
- Parser synchronization preserves valid siblings across expression errors and later statements after malformed declarations.
- Bounded constant evaluation, conservative control-flow/return checks, and exact static tensor-shape rules are specified and tested.
- Diagnostic schema v1 covers locations, notes, fixes, escaping, and deterministic full-output golden behavior.
- The AST now has an exhaustive kind-specific visitor and a documented owner/lifetime contract (see `AST_CONTEXT_V1.md`).
- The HIR now has a symbol/reference index API with lexical scopes and revision-based rebuild invalidation; incremental parsing is still separate future work.
- Public module/import resolver APIs and exact-type overload candidate selection are tested; the v1 parser and semantic analyzer do not yet use them.
- Updated weighted estimate: **97.25%, rounded to 97%** (see `PROGRESS.md` and the [front-end status report](./FRONT_END_STATUS_REPORT.md)).

This is an estimate, not a marketing score. It may go down if a review finds missing acceptance behavior. Passing tests prove only the tested cases, not completeness of the language.

## What Already Works?

- C++20/CMake builds the front-end without LLVM.
- The lexer splits a small subset of Indicode text into tokens.
- The parser builds an AST (Abstract Syntax Tree: a tree-shaped record of the code's structure).
- Semantic checks catch selected name, type, mutability, function-call, return, and integer-range errors.
- The compiler lowers checked AST into project-owned typed HIR (High-level Intermediate Representation: a simpler, explicitly typed form of the program).
- The HIR verifier checks required type annotations, expression/operator types, function calls, and return types.
- Lists can be created, indexed, and iterated with `for`; text can also be iterated/indexed as `Char` values.
- UTF-8 is validated throughout the source, including strings and comments; supported string escapes include Unicode scalar escapes such as `\u{e9}`. A single Unicode scalar infers `Char`, more infer `Text`.
- `indicode check`, `tokens`, `ast`, `ast-json`, `typed`, `typed-json`, and opt-in `--json-diagnostics` work for the current subset.
- Integer literals support `0b` binary, `0o` octal, `0x` hexadecimal, and `_` digit separators; malformed forms are rejected.
- Function calls accept named arguments such as `add(b: 22, a: 20)` and reject unknown, duplicate, missing, or out-of-order arguments.
- Nested block comments use `/* ... */`; newlines inside them still separate statements. An unclosed comment is a compile-time error.
- Records support typed fields, construction with named fields, and read-only member access with type checking.
- Numeric values can be explicitly converted with functions such as `to_Int2(value, checked: true)` and `to_Float(value, checked: true)`. Literal range/finite checks run before lowering.
- Tensor function signatures support numeric element types and static or symbolic dimensions, such as `Tensor<Float, [Batch, 784]>`; incompatible shapes are rejected by exact type checking.
- Integer ranges such as `0..10` are parsed and type-checked; `for item in 0..10` binds `item` to the bound integer type.
- `else if` chains are parsed into nested conditionals, and each branch participates in non-`Void` return-path checks.
- Nineteen CTest cases include source-manager line/column checks, lexer payload/trivia checks, grammar version and precedence checks, AST-printer golden output, exhaustive AST visitor/lifetime checks, AST and typed-HIR JSON validation, parser recovery/fix-its, HIR symbol/scope checks, an installed-package visitor consumer, a deterministic malformed-input smoke test, tensor shapes, and record/conversion samples.

## Newly Supported Syntax

Records declare fields with explicit types. Construction uses named arguments and must provide every field exactly once.

```indi
record Point {
    x: Int,
    y: Int
}

origin = Point(x: 20, y: 22)
say(origin.x)
```

Unknown/missing fields, duplicate field declarations, and mismatched field types are errors. Record methods, field assignment, and visibility rules are not implemented.

Integer conversion is explicit and checked:

```indi
small = to_Int2(30000, checked: true)
```

The current conversion subset supports checked conversions to integer and floating-point types. It does not execute conversions at runtime.

Tensor declarations support static and symbolic dimensions in types and signatures:

```indi
function preserve_batch(batch: Tensor<Float, [Batch, 784]>) -> Tensor<Float, [Batch, 784]> {
    return batch
}
```

Incompatible declared shapes are rejected. Tensor construction, operators, broadcasting, device placement, and runtime shape guards are not implemented.

Integer ranges use two endpoints:

```indi
for step in 0..10 {
    say(step)
}
```

Both endpoints must have the same integer type. The parser and type checker do not define runtime iteration or whether the upper endpoint is included; programs are not executed by this front end.

Lists are written with square brackets. A non-empty list must contain values of one type. Its type is inferred from those values.

```indi
items = [20, 22]
answer = items[0]

for item in items {
	say(item)
}
```

An index must be an `Int`, and the collection type must support indexing. This front end checks types only; it does not execute programs or perform runtime bounds checks. Text indexing and iteration produce one `Char` Unicode scalar at a time.

```indi
accent = "\u{e9}"  # one Unicode scalar, so this is Char
message = "hello"   # multiple scalars, so this is Text
for letter in message {
	say(letter)
}

answer = add(b: items[1], a: items[0])
mask = 0b1111
population = 1_000
```

Invalid cases are rejected with stable diagnostics:

```indi
mixed = [1, "two"]       # E-SEMA-025: list elements need one type
wrong_index = items[true] # E-SEMA-026: index must be Int
for value in 5 { say(value) } # E-SEMA-023: Int is not iterable
bad_text = "\q"           # E-LEX-007: unsupported string escape
bad_binary = 0b102         # E-LEX-009: invalid binary digit
bad_names = add(c: 1, a: 2) # E-SEMA-028: unknown named argument
```

An unclosed block comment is also an error:

```indi
value = 1 /* comment never ends
```

The current grammar does not yet provide a type annotation syntax for an empty list, so `[]` is rejected until that syntax is designed.

## Roadmap

| Stage | Status | In plain language | Completion evidence |
| --- | --- | --- | --- |
| 1. Project foundation | Mostly done | Build the project without large external frameworks. | C++20/CMake build and CTest work without LLVM or network downloads. |
| 2. Source and lexer | Started | Own source buffers, map locations, and recognize names, numbers, strings, punctuation, and errors. | Unicode identifiers, numeric suffixes, raw strings, trivia preservation, and full terminal display-width mapping. |
| 3. Parser and AST | Started | Recognize the language's grammar and record its structure. | Ranges, complete newline rules, AST golden tests, full recovery. |
| 4. Names and types | Started | Decide what names refer to and whether operations use compatible types. | Modules, imports, conversions, overflow, ownership/effects, tensor shapes, and negative diagnostics. |
| 5. Typed HIR | **Core subset implemented** | Convert checked source into our own typed representation. | More language coverage, stable function/type IDs, deserialization/compatibility policy, and broader verifier invariants. |
| 6. Diagnostics and tools | Started | Make errors useful to people and editors. | JSON schema stability, fix-its, editor interfaces, and complete recovery diagnostics. |
| 7. Front-end acceptance | Not complete | Prove the documented front-end subset works reliably. | Conformance suite, fuzz/property tests, sanitizers, CI, docs, and repeatable clean builds. |
| 8. Optional LLVM adapter | Not started | Connect verified Indicode HIR to an external LLVM installation. | Separate opt-in adapter, no vendored LLVM, backend integration tests, documented external setup. |

## What Must Happen Before LLVM Is Connected?

1. Finish and version the front-end grammar and type/conversion rules.
2. Expand lexer/parser/resolver/type-checker tests to cover valid and invalid examples from the agreed v1 front-end contract.
3. Make the typed HIR stable, independently verifiable, and documented.
4. Keep source spans and diagnostics usable through lowering.
5. Pass clean offline builds, the complete test suite, fuzz/sanitizer checks, and beginner examples.
6. Agree on a separate LLVM adapter design. Only that optional adapter may locate an externally installed LLVM; ordinary front-end builds must remain independent.

## How to Build and Try It

From this project directory, an out-of-tree build keeps generated files away from source:

```sh
cmake -S . -B /tmp/indicode-build
cmake --build /tmp/indicode-build
ctest --test-dir /tmp/indicode-build --output-on-failure
/tmp/indicode-build/indicode check examples/hello.indi
/tmp/indicode-build/indicode typed examples/hello.indi
/tmp/indicode-build/indicode check examples/records.indi
/tmp/indicode-build/indicode check examples/tensor_shapes.indi
```

A successful check means the source passed the current subset's front-end checks. It does not mean the program was executed or compiled to machine code.

## Glossary

- **Lexer:** turns characters into tokens such as names, numbers, and symbols.
- **Parser:** checks token order against the language grammar.
- **AST:** a tree describing the source code's structure.
- **Name resolution:** connects each use of a name to its declaration.
- **Type checking:** checks that values and operations are compatible.
- **HIR:** this project's typed, higher-level representation between source code and a future backend.
- **Verifier:** checks that an HIR program obeys the representation's rules before another compiler stage uses it.
- **LLVM adapter:** a future, separate component that translates verified Indicode representation for an externally provided LLVM framework.
