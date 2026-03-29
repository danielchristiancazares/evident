# Compiler Finish Plan

This document defines what "finished" should mean for the current Evident compiler and lays out the recommended order of work.

## Current State

As of 2026-03-29, the checked-out tree is a working subset compiler rather than a broken prototype.

- `ctest --test-dir build-x64 --output-on-failure` passed `68/68` tests.
- The implemented pipeline is `source -> lexer -> parser -> semantic analysis -> HIR -> MIR -> LLVM IR -> native artifact emission`.
- The current suite covers positive compile tests, MIR and LLVM golden outputs, native object/executable smoke tests, backend entrypoint rejection tests, and newer permit/proof/affine checks.

The main remaining work is scope completion and productization.

## Finish Targets

There are two legitimate definitions of "finished":

### 1. Polished Subset Compiler

Ship the currently implemented language subset with strong diagnostics, reliable native emission on Windows x64, accurate docs, and a stable test/CI story.

### 2. Larger Language Compiler

Expand the compiler until the language surface matches the broader design intent, including generics, trait implementations and dispatch, package-scale compilation, and a more complete proof/permit/typestate model.

## Recommendation

Finish the compiler in two stages:

1. Reach "polished subset compiler" first.
2. Use that stable baseline to drive the larger language roadmap.

This avoids polishing backend details that may need redesign once generics, trait dispatch, and package compilation settle the HIR/MIR shape.

## Major Gaps

### Language Scope

- Generic function calls are still rejected.
- Trait declarations exist, but trait solving and method dispatch are not implemented.
- There is no `impl` surface or trait implementation model yet.
- The expression language is still intentionally small and only covers the currently tested subset.
- Package or import-based multi-file compilation does not exist yet; the CLI still compiles one input file at a time.

### Proof / Permit / Typestate Semantics

- The tree already contains meaningful `prove`, `with`, permit, and affine-move enforcement.
- The broader typestate transition story is not yet closed out as a complete language feature set.
- Permit/proof minting rules should be documented and tested as a stable semantic contract before the language is called complete.

### Native Backend

- Native emission is currently limited to `x86_64-pc-windows-msvc`.
- The backend still uses a straightforward alloca-heavy LLVM IR lowering rather than a cleaner SSA-friendly strategy.
- Type/layout coverage is still first-pass quality and rejects several categories that a production compiler would need to support.
- Toolchain discovery and failure reporting are usable but still deserve hardening.

### Testing and Productization

- Many negative tests only assert that compilation fails, not that the exact diagnostic text/span is correct.
- Assembly/object tests are mostly existence checks rather than semantic validation.
- Executable tests mostly validate exit codes rather than richer observable behavior.
- CI, release packaging, and reproducible toolchain expectations are not yet documented as a complete release workflow.

## Recommended Roadmap

### Milestone 0: Truth Sync

Goal: make the repo self-describing again.

- Keep `README.md`, `AGENTS.md`, and planning docs aligned with the actual CLI and pipeline.
- Document the currently supported target, toolchain assumptions, and tested command lines.
- Keep this plan updated as milestones land.

Exit criteria:

- A new session can understand the implemented subset, backend constraints, and remaining roadmap from the docs alone.

### Milestone 1: Polished Subset Release

Goal: finish the current subset as a reliable compiler product.

- Add diagnostic golden tests for representative parse, semantic, and backend failures.
- Add malformed-input and recovery-oriented parser tests.
- Strengthen backend tests so object/executable checks validate more than file existence or exit code.
- Improve external tool discovery and actionable error messages for missing `clang`/`lld`.
- Decide and document the supported host/target matrix for the subset release.
- Add CI that builds and runs the full suite on the supported environment.

Exit criteria:

- Docs are accurate.
- CI is green on the supported platform.
- Diagnostics are regression-tested.
- Native emission failure modes are documented and test-covered.

### Milestone 2: Package and Compilation Model

Goal: move from single-file compilation to project-scale compilation.

- Design package boundaries, imports, and source discovery.
- Extend the driver to compile more than one source file into one semantic/HIR/MIR package.
- Add tests for cross-file name resolution, visibility, duplicate detection, and backend entry selection.

Why this matters:

- A compiler cannot realistically be called complete if all real programs must live in one source file.

### Milestone 3: Generics

Goal: implement generic compilation as a real language feature rather than parse-only syntax.

- Support generic function calls.
- Choose and implement the instantiation strategy for generic functions and generic types.
- Define how generics appear in HIR/MIR and how the backend lowers them.
- Add positive and negative tests for generic resolution, type mismatches, visibility leaks, and backend emission.

Dependencies:

- This should land before trait implementations and dispatch.

### Milestone 4: Traits and Implementations

Goal: turn traits into executable semantics.

- Design the `impl` surface.
- Add trait implementation checking, method lookup, and conflict rules.
- Choose the dispatch model for the first implementation strategy.
- Lower trait-backed calls through HIR, MIR, and backend codegen.

Dependencies:

- Generics should be implemented first or alongside this work.

### Milestone 5: Proof / Permit / Typestate Completion

Goal: finish the language contract around authority and affine proof values.

- Decide which proof and permit rules are fundamental language guarantees.
- Close the remaining gap between current move/affinity checks and full typestate transition support.
- Add golden tests for authority creation, scope restrictions, token movement, branch joins, and rejected escapes.
- Update the language docs so the feature is described as a stable contract instead of an evolving experiment.

### Milestone 6: Backend Maturity and Portability

Goal: make native codegen broader and more production-ready.

- Broaden type/layout coverage.
- Move toward cleaner SSA-friendly lowering where it improves maintainability and output quality.
- Add debug info and optimization-level handling.
- Support at least one additional target beyond Windows x64 COFF.
- Improve artifact validation and backend-specific regression tests.

Dependencies:

- Avoid major backend redesign until milestones 3 through 5 settle the language-facing IR shape.

## Suggested Ordering Constraints

- Do not invest heavily in optimization or cross-target work before the language surface is stable.
- Do not implement trait dispatch before deciding generic instantiation.
- Do not call the compiler product-finished while it is still single-file only.
- Keep docs and tests moving with each milestone; do not leave them for a cleanup pass at the end.

## Definition Of Done

The compiler can reasonably be called "finished" when all of the following are true:

- The intended language surface is explicitly defined and matches implementation.
- The package model supports real multi-file programs.
- Generics and traits are either fully implemented or explicitly removed from the language surface.
- Proof/permit/typestate behavior is documented as a stable contract and regression-tested.
- The backend has a clear supported target matrix and passes semantic regression tests, not just smoke tests.
- Docs, CI, and release workflow are accurate enough that a new contributor can build, test, and extend the compiler without archaeology.

## Near-Term Priority

If work resumes immediately, the best next sequence is:

1. Complete Milestone 0.
2. Push Milestone 1 to a true subset release bar.
3. Decide whether Milestone 2 or Milestones 3 and 4 come next based on the intended language scope.
4. Defer major backend polish until the language-facing IR contracts are stable.
