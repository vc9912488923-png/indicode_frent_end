# Strict Community Rules

Updated: 2026-11-03
Front-end package version: 0.9.8 (pre-release; not a complete compiler)

These rules protect the project's scope, build size, maintainability, and community trust. They apply to every contribution, including changes proposed by maintainers and automated coding tools.

## 1. Front End Only

- This phase builds the Indicode language front end: source handling, lexer, parser, AST, name resolution, type checking, diagnostics, project-owned typed HIR, and verification.
- Do not add machine-code generation, runtime execution, GPU/device backends, autodiff runtime, training systems, or unrelated product features to this front-end project.
- Tensor types and statically knowable tensor shape checks belong to the planned front end; tensor execution and training do not.
- Update the roadmap and [front-end status report](./FRONT_END_STATUS_REPORT.md) before expanding the agreed scope. A catalogued feature is not automatically approved for the current milestone.

## 2. LLVM Must Not Be Bundled

- Never copy LLVM source, headers, libraries, binaries, or generated LLVM files into this repository.
- Do not add LLVM downloads, network fetches, Git submodules, `FetchContent`, or required LLVM package discovery to the default configure/build.
- The ordinary front-end build and tests must work without LLVM installed and without network access.
- Keep Indicode's typed HIR and verifier owned by this project. They are the stable boundary for a later backend; they are not LLVM IR.
- LLVM connection is a separate future adapter. It may use an LLVM installation supplied by the developer, must be opt-in and off by default, and must not make LLVM a prerequisite for front-end development. Do not hand-implement LLVM's IR or ABI as a substitute for its framework.
- Any proposal to change these rules requires a documented community decision and a progress-document update before implementation.

## 3. Progress Percentages Need Evidence

- Never raise the overall percentage to satisfy a target or to make the project look more complete.
- Use the weighted acceptance table in `PROGRESS.md`. Keep the weights fixed unless a documented scope decision changes them.
- A sub-area earns credit only for behavior that is implemented, covered by passing tests, and documented. A placeholder, interface declaration, or design note alone is not implementation.
- Record what changed, what remains, the build/test command and result, and whether compiled outputs were cleaned.
- Distinguish front-end progress from LLVM adapter, complete compiler, AI runtime, and GPU progress. Do not claim those are complete based on front-end work.

## 4. Every Code Change Must Be Reviewable

Before a language feature is coded, document:

1. Its grammar and examples.
2. Its exact type and runtime-facing semantics.
3. At least one valid example and one invalid example.
4. Expected diagnostic code/message and source location for invalid cases.
5. Acceptance tests and its effect on the progress estimate.

For every code change:

- Add or update focused tests in the same contribution.
- Update beginner docs and `PROGRESS.md` when behavior or milestone status changes.
- Keep public headers and APIs small; explain why a new abstraction is needed.
- Do not silently change existing language meaning. Propose behavior changes and migration notes first.
- Do not combine unrelated formatting, refactoring, or generated-file churn with a feature.

## 5. Build and Dependency Hygiene

- C++20 and the standard library are the default dependency baseline. Every additional required dependency needs a written reason, supported-version policy, license/security review, and a dependency-free path where practical.
- Configure builds outside the source tree when possible, for example `cmake -S . -B /tmp/indicode-build`.
- Never commit `build/`, `CMakeFiles/`, CMake caches, object files, executables, static/shared libraries, temporary logs, downloaded archives, or generated IR dumps unless a specific source-controlled fixture is reviewed and approved.
- Keep ignore rules narrow and explicit. Do not remove source files, tests, the current front-end completion plan, or user files during cleanup.
- The default build must be reproducible and must not download tools or dependencies.

## 6. Diagnostics, Safety, and Input Handling

- Compiler code must report malformed input as diagnostics; it must not crash or terminate the process for an ordinary source error.
- Preserve source spans through lexing, parsing, semantic analysis, and HIR.
- Give diagnostic codes stable meaning; do not reuse a code for a different error.
- Validate file sizes, source encodings, integer ranges, and recursion/resource limits as the relevant stages are implemented.
- Never commit secrets, credentials, private datasets, or machine-specific absolute paths.

## 7. Community and Beginner Accessibility

- Explain compiler terms the first time they appear in beginner-facing documents. Keep a plain-language path alongside technical detail.
- Reviews should be respectful, specific, and about the change rather than the contributor.
- Ask for evidence: tests, examples, and measured behavior. Do not accept performance or completion claims without reproducible measurements.
- Preserve contributors' unrelated work. Do not reset, overwrite, or delete files outside the agreed change.

## Required Contribution Checklist

A contribution is ready for review only when all applicable items are true:

- [ ] Scope is front end only and does not bundle or fetch LLVM.
- [ ] Syntax/semantics and valid/invalid examples are documented.
- [ ] Focused tests pass, including negative cases where relevant.
- [ ] Full current CTest suite passes.
- [ ] README/roadmap/progress are updated when needed.
- [ ] No generated build output or unrelated file changes are included.
- [ ] Progress percentage is evidence-based and its calculation is consistent.
