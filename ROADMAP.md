---
name: missing-features-roadmap
overview: "Plan the remaining Evident compiler work in dependency order: keep truth and validation green continuously, finish the language surface before release polishing, pull arbitrary-precision Nat runtime/ABI into substrate completion, keep package/backend infrastructure after core semantics, and leave self-hosting last."
todos:
  - id: truth-sync
    content: Keep docs, release gates, and validation state aligned with landed work.
    status: completed
  - id: substrate-builtins
    content: Finish Text/Bytes, numeric, UTF-8, Char/Byte, and arbitrary-precision Nat runtime/ABI substrate work.
    status: completed
  - id: raw-foreign-rules
    content: Finish raw substrate, foreign ABI, semantic-claim, egress, and boundary/hazard collapse checks.
    status: in_progress
  - id: generics-completion
    content: Complete explicit generic instantiation and monomorphization before trait design.
    status: pending
  - id: traits-impl
    content: Design and implement trait and impl dispatch after generics settle.
    status: pending
  - id: authority-typestate
    content: Finish proof, permit, affine, and typestate transition semantics.
    status: pending
  - id: package-model
    content: Settle durable package metadata and project-scale compilation boundaries.
    status: pending
  - id: backend-maturity
    content: Mature backend runtime, ABI, lowering, debug, optimization, and target support after representation decisions.
    status: pending
  - id: subset-release
    content: Burn down polish debt and strengthen product release tests after the language surface stabilizes.
    status: pending
  - id: bootstrap
    content: Port compiler package to Evident and add stage-1/stage-2 evidence.
    status: pending
isProject: false
---

# Missing Features Roadmap

## Scope
This plan treats `docs/EVIDENT_LANGUAGE_SPEC.md` as the language-design authority and `docs/COMPILER_FINISH_PLAN.md` / `docs/BOOTSTRAP_PLAN.md` as the current implementation-status anchors. The roadmap is ordered by dependency rather than by feature bucket: finish the language surface before release polish, put package and backend infrastructure after core semantics, and keep self-hosting last.

## Phase 0: Continuous Truth And Green Gate
This is a standing discipline, not a heavy one-time phase.

- Keep `docs/COMPILER_FINISH_PLAN.md`, release docs, CTest totals, and roadmap status aligned whenever a feature slice lands.
- Keep the supported validation path green after each slice: configure/build, focused tests, release contracts, and full CTest when behavior changes broadly.
- Treat completed work as completed slices, not as completed future milestones when documented gaps remain.

Current status:
- `traverse`, Text/Bytes bounds/slicing, non-empty Text/Bytes operations, clang AST bare-bool validation, partial raw-adapter checks, and non-truncating frontend `Nat` literal handling are in the tree.
- The current broader release/package/design validation has been kept green, but that does not make the later language-completion phases done.

Key files:
- `docs/COMPILER_FINISH_PLAN.md`
- `docs/EVIDENT_LANGUAGE_SPEC.md`
- `docs/RELEASE_CHECKLIST.md`
- `cmake/AssertReleaseDocs.cmake`
- `cmake/AssertClangTidyBareBooleans.cmake`

## Phase 1: Built-In Substrate Completion
Finish the core value substrate before layering additional language abstractions over it.

- Complete arbitrary-precision `Nat` as a real runtime and ABI value: materialization, storage, passing/returning, arithmetic needed by compiler-owned operations, exact length/count results, and conversion-boundary tests.
- Finish UTF-8 source text scalar handling: full Unicode scalar decoding, scalar-count-aware literal typing, and broader non-ASCII regression tests.
- Close exact numeric boundary behavior for `Nat`, `Char`, `Byte`, `CInt`, and `CSize`, including negative-to-`Nat` / `CSize`, underflow/overflow, Unicode scalar rejection, and exact conversion diagnostics.
- Preserve completed `Text`/`Bytes` and `NonEmptyText`/`NonEmptyBytes` operation behavior while extending regression coverage for remaining edge cases.

Current status:
- Non-empty Text/Bytes operations and front-end non-truncating `Nat` literal transport have landed.
- `Nat` is not substrate-complete until arbitrary-precision runtime/ABI and exact cardinality behavior are implemented.

Key files:
- `src/Semantic.cpp`
- `src/Hir.cpp`
- `src/Mir.cpp`
- `src/Backend.cpp`
- `include/evident/ResolvedType.hpp`
- `tests/*text*`
- `tests/*bytes*`
- `tests/*nat*`

## Phase 2: Raw Substrate, Foreign ABI, And Boundary/Hazard Collapse
Finish the adapter discipline after the raw substrate value sets and conversions are precise.

- Complete raw-adapter exposure classification from Section 10.6, including raw-substrate-exposing and raw-adapter-exposing type expressions.
- Reject exported domain/foundation APIs that expose raw adapter surfaces, including substrate-local reasons, foreign ABI substrate, raw maps, raw byte/text sequence substitutes, plain raw `Text`/`Bytes` surfaces where the public API still exposes them, and raw-adapter nominal relays.
- Complete source-visible `CString` restrictions, exact foreign-symbol binding diagnostics, foreign-ABI-admissible `foreign fn` signatures, and target-specific ABI/prototype matching for `x86_64-pc-windows-msvc`.
- Add exact boundary/hazard collapse checks for runtime looseness, direct-body-only foreign egress preparation, egress source-carrier provenance, consequence-to-ABI mapping, and same-body dataflow restrictions.
- Add semantic-claim anti-laundering checks from Section 10.6: identifier word normalization, marker-family coverage, consequence-carrying type classification, definite consequence containment, exported producer checks, primitive/sequence-backed nominal checks, permit type-only authority coverage, and foundation semantic-blindness.

Current status:
- The compiler has substantial partial coverage: foreign conversion declarations, boundary/hazard-only conversion calls, adapter-local foreign reason restrictions, raw ABI public-surface rejections, raw map/sequence substitute rejections, direct source `CString` literal restrictions, and duplicate foreign link-name rejection.
- The documented gaps in `docs/COMPILER_FINISH_PLAN.md` remain open and are the actual completion criteria for this phase.

Key files:
- `docs/EVIDENT_LANGUAGE_SPEC.md` Sections 10.5 and 10.6
- `src/Semantic.cpp`
- `src/Backend.cpp`
- `tests/invalid_*foreign*`
- `tests/invalid_*raw*`
- `tests/expected_invalid_*foreign*`

## Phase 3: Generics Completion
Finish explicit generic instantiation before designing trait dispatch.

- Generalize explicit generic function and record instantiation beyond the currently tested foundation-record paths.
- Extend monomorphization through HIR, MIR, and backend lowering for all remaining supported generic user-defined cases.
- Decide whether type inference belongs in the next language milestone or remains explicit-only.
- Preserve the existing package generic smoke coverage, and defer expanded project-scale generic scenarios to the package-model phase.

Current status:
- Explicit generic function calls, explicit/nested generic record construction, multiple instantiations, affine generic wrappers, and imported package generic calls have meaningful coverage.
- Traits remain out of scope until this phase is stable.

Key files:
- `src/Semantic.cpp`
- `src/Hir.cpp`
- `src/Mir.cpp`
- `src/Backend.cpp`
- `tests/valid_generic_*`
- `tests/invalid_generic_*`

## Phase 4: Traits And Impl Dispatch
Design and implement traits only after generic instantiation has converged.

- Specify `trait`, trait-bound, method lookup, and `impl` syntax and semantics in the language spec before implementation.
- Implement trait declaration checking, implementation conformance, conflict/coherence rules, method lookup, and dispatch lowering.
- Decide static versus dynamic dispatch boundaries explicitly; do not smuggle dispatch policy through backend convenience.
- Add parser, semantic, HIR, MIR, backend, package, and negative diagnostics for the chosen trait model.

Current status:
- The parser rejects unsupported trait/impl declarations. There is no implemented trait, trait-bound, method-dispatch, or `impl` surface.

Key files:
- `docs/EVIDENT_LANGUAGE_SPEC.md`
- `src/Parser.cpp`
- `src/Semantic.cpp`
- `src/Hir.cpp`
- `src/Mir.cpp`
- `src/Backend.cpp`
- `tests/*trait*`
- `tests/*impl*`

## Phase 5: Authority, Proof, Permit, And Typestate Completion
Close authority semantics after the core data and dispatch surfaces stop moving.

- Preserve existing `prove`, `grant`, permit argument, affine move, and phase construction guarantees.
- Complete broader typestate transitions: phase transitions, branch joins, token movement, recovery behavior, rejected escapes, and runtime-affecting transition lowering.
- Audit every proof, permit, affine, and phase category rule through resolved type classification, semantic checking, HIR, MIR, backend lowering where relevant, and diagnostics.
- Add regression tests for every authority/proof category rule, including package and generic interactions once those phases are stable.

Current status:
- The tree already has meaningful proof, permit, grant, affine-move, and concrete phase construction enforcement.
- Broader typestate transition semantics are not yet a complete language feature set.

Key files:
- `include/evident/ResolvedType.hpp`
- `src/ResolvedType.cpp`
- `src/Semantic.cpp`
- `src/Hir.cpp`
- `src/Mir.cpp`
- `tests/*permit*`
- `tests/*proof*`
- `tests/*phase*`
- `tests/*affine*`

## Phase 6: Package Model And Project-Scale Compilation
Settle project-scale compilation after the core language behavior it must transport is mostly stable.

- Decide whether the current `evident.pkg` source-list manifest is sufficient, or add durable package metadata for identity, external dependencies, build profiles, entry selection, and bootstrap provenance.
- Extend package diagnostics for dependency boundaries, module imports, source discovery, visibility, authority, original-file source mapping, and manifest errors.
- Keep cross-file HIR/MIR/native regression tests aligned with package metadata growth.
- Make package semantics sufficient for the eventual bootstrap compiler package without turning package work into release polish.

Current status:
- Multiple explicit input files and `--package <dir>` compile as one package translation unit. `evident.pkg` source-list manifests and deterministic recursive `.evd` discovery exist. File-local imports gate cross-top-level fully qualified references.

Key files:
- `src/Driver.cpp`
- `src/Source.cpp`
- `src/Semantic.cpp`
- `docs/BOOTSTRAP_PLAN.md`
- `tests/package_*`
- `tests/package_dir/`
- `tests/package_manifest/`

## Phase 7: Backend Runtime And ABI Maturity
Mature backend quality after language value sets, runtime representations, and ABI contracts are no longer moving underneath it.

- Keep `x86_64-pc-windows-msvc` as the release target until language-facing IR and ABI contracts stabilize.
- Complete backend support for the substrate/runtime decisions made earlier, including arbitrary-precision `Nat` ABI, foreign ABI representation, and exact conversion behavior.
- Move toward cleaner SSA-friendly MIR/LLVM lowering where it improves maintainability and output quality.
- Add debug info, optimization-level handling, and stricter native artifact validation.
- Add new targets only after exact value sets, ABI representations, calling convention, and artifact validation are specified.

Current status:
- Native emission is limited to `x86_64-pc-windows-msvc`; unsupported target rejection is tested. The backend remains straightforward and alloca-heavy.

Key files:
- `src/Mir.cpp`
- `src/Backend.cpp`
- `docs/NATIVE_BACKEND_PLAN.md`
- `docs/TOOLCHAIN_REPRODUCIBILITY.md`
- `tests/native_*`
- `tests/expected_backend_*`

## Phase 8: Polished Subset Release Bar
Polish only after the language surface being polished is stable.

- Burn down C++ design-scan debt without weakening `docs/CPP_DESIGN.md`; replace legacy bare `bool` surfaces with consequence-named enums or typestate where they are repo-defined surfaces.
- Expand parser recovery and malformed-input diagnostics after syntax stops churning.
- Strengthen native semantic tests beyond exit codes where the language has observable outputs or payload checks.
- Keep install layout, ZIP, checksum, release evidence, source-tree audit, CI workflow contract, package contracts, and CTest total contracts green.
- Prepare release notes and evidence only when docs and release contracts describe the implemented subset accurately.

Current status:
- The current release/package/design guardrails are strong and should stay green continuously, but they are not a substitute for finishing the remaining language phases.

Key files:
- `docs/CPP_DESIGN.md`
- `README.md`
- `docs/RELEASE_CHECKLIST.md`
- `docs/TOOLCHAIN_REPRODUCIBILITY.md`
- `cmake/AssertCppDesignEscapeHatches.cmake`
- `cmake/AssertReleaseDocs.cmake`
- `.github/workflows/ci.yml`

## Phase 9: Self-Hosting Bootstrap
Bootstrap remains last because it depends on language semantics, package scale, backend/runtime support, release evidence, and host boundary design.

- Replace the current `bootstrap/compiler` scaffold with an actual Evident compiler package.
- Add explicit boundary modules for file I/O, paths, environment, process exit, diagnostics, and tool invocation.
- Build stage 1 with the C++ seed compiler, build stage 2 with stage 1, then validate stage 2 against conformance, diagnostics, native emission, package, and release-contract tests.
- Record seed, stage-1, and stage-2 provenance in release evidence.

Current status:
- `bootstrap/compiler` is a compilable scaffold with package identity, source discovery, layout/manifest, host/runtime, stage provenance, and release-evidence models. It is not a compiler implementation and there is no stage-1/stage-2 harness yet.

Key files:
- `bootstrap/compiler/`
- `docs/BOOTSTRAP_PLAN.md`
- `docs/RELEASE_CHECKLIST.md`
- `.github/workflows/ci.yml`

## Ordering Rules
- Keep truth, docs, and validation green continuously; do not treat that as a substitute for feature completion.
- Finish built-in substrate and arbitrary-precision `Nat` runtime/ABI before relying on exact cardinality, conversion, or foreign-boundary behavior.
- Finish raw substrate, foreign ABI, and boundary/hazard collapse before declaring exported domain/foundation surfaces trustworthy.
- Do not implement trait dispatch before generic instantiation is settled.
- Finish core language semantics before package-scale, backend-maturity, and release-polish work that depends on stable interfaces.
- Do not call the subset releasable while docs and release contracts still describe stale implementation status or while core language semantics remain intentionally incomplete.
- Keep bootstrap last; self-hosting evidence is only meaningful after language, package, backend, boundary, diagnostics, and release evidence are mature enough to carry the compiler itself.