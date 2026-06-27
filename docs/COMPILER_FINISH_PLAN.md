# Compiler Finish Plan

This document defines what "finished" should mean for the current Evident compiler and lays out the recommended order of work.

## Current State

As of 2026-06-26, the checked-out tree is a working subset compiler rather than a broken prototype.

- A local x64 Visual Studio developer-shell run of `ctest --preset windows-x64-ninja --output-on-failure` passed `358/358` tests.
- The implemented pipeline is `source -> lexer -> parser -> semantic analysis -> HIR -> MIR -> LLVM IR -> native artifact emission`.
- The current suite covers positive compile tests, explicit generic function call, explicit generic record construction, nested generic record construction, package/import generic function and record specialization, multiple concrete generic instantiation HIR/MIR/native executable checks, builtin collection arity diagnostics, `List<T>` / `NonEmptyList<T>` HIR/MIR/native backend checks, `CString` / `Map<K, V>` / `NonEmptyMap<K, V>` native ABI checks, generic declaration-quarantine, unsupported generic declaration-form diagnostics, generic contract-type argument diagnostics, generic structural-blindness diagnostics, foreign-function contract diagnostics, public type and public function contract visibility-leak diagnostics, generic visibility-leak diagnostics, generic call/record diagnostic golden outputs, single-file and package-scale proof/permit authority diagnostic golden outputs, explicit-input, manifest-driven, and source-discovered package HIR/native executable checks, split-module HIR/diagnostic checks, package duplicate/import/visibility/authority diagnostics, file-local package import-gate diagnostics including generic type arguments, package CLI/source-discovery/manifest failure diagnostics, CLI help/version/toolchain golden outputs, duplicate native emit option rejection, CI workflow, release-docs, CTest total, invalid corpus, test corpus registration, and C++ design escape-hatch contract validation, MIR and LLVM golden outputs, diagnostic golden outputs, native object/executable smoke tests, COFF/PE artifact-header validation, install-layout and package-ZIP validation, backend entrypoint and materialized-`Never` rejection tests, and permit/proof/affine/phase checks.
- `CMakePresets.json`, `.github/workflows/ci.yml`, and `docs/RELEASE_CHECKLIST.md` define the primary Windows x64 validation and release path: enter a Visual Studio x64 developer shell, configure/build/test/package with the `windows-x64-ninja` presets, and run the full CTest suite with LLVM `clang`/`lld-link` available for native emission tests.

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

- Explicit generic function calls now type-check, lower through HIR/MIR, and native-emit through concrete function instantiation when all type arguments are written. Explicit and nested generic record construction also type-check, lower through HIR/MIR, and native-emit through concrete record instantiation for the tested foundation-record paths, including multiple concrete instantiations and imported package generic calls. Type inference, broader generic user-defined type lowering cases, and generic interactions beyond the current explicit foundation subset remain open.
- Traits are not part of the implemented core language. There is no `trait`, trait-bound, method-dispatch,
  or `impl` surface yet; those remain larger-language work.
- The expression language is still intentionally small and only covers the currently tested subset.
- The CLI now accepts multiple explicit input files or `--package <dir>` and compiles the result as one semantic/HIR/MIR package. Package directories may use an `evident.pkg` source-list manifest; otherwise `--package` falls back to recursive `.evd` discovery. Top-level `import module::path;` declarations are parsed, checked against the package module graph, and act as an opt-in file-local dependency gate for cross-top-level fully qualified references. Split declarations of the same module path merge when their module kind matches.

### Proof / Permit / Typestate Semantics

- The tree already contains meaningful `prove`, `grant`, permit, and affine-move enforcement.
- The broader typestate transition story is not yet closed out as a complete language feature set.
- Permit/proof minting rules are documented as a stable subset contract and regression-tested for direct proof construction, `proves` authority, permit argument syntax, permit escape, grant shape, grant failure typing, same-module `grants` / `proves`, and affine reuse. Broader typestate transition semantics still need completion before the language can be called complete.

### Native Backend

- Native emission is currently limited to `x86_64-pc-windows-msvc`, and unsupported target selection is regression-tested as an explicit backend diagnostic.
- The backend still uses a straightforward alloca-heavy LLVM IR lowering rather than a cleaner SSA-friendly strategy.
- Type/layout coverage is still first-pass quality, although the backend now covers all currently named primitive builtins plus opaque pointer/length ABI carriers for the compiler-owned collection families.
- Toolchain discovery, provenance reporting, and failure reporting are usable: `EVIDENT_CLANG` selects the driver, `--print-toolchain` reports the configured native target and selected driver without compiling input or launching external tools, `--check-toolchain` rejects unsupported native targets before probing the selected driver and `lld-link` with `--version`, missing-driver diagnostics are golden-tested, the checked-toolchain path regression-tests missing `lld-link` on `PATH`, Windows launches the driver directly through `CreateProcessW`, and non-Windows launcher code now avoids shell interpolation via `posix_spawnp`. Broader host validation still deserves hardening.

### Testing and Productization

- Current single-file semantic rejection cases, backend entrypoint failures, CLI/package failures, duplicate native emit option failures, CLI help/version/toolchain outputs, checked toolchain version-probe identity, checked-toolchain missing-linker failure, malformed-parser cases, generic resolution and visibility failures, authority/typestate/affine failures, and missing-toolchain failures now have regression tests. An invalid corpus registration contract rejects unregistered invalid source fixtures and diagnostic goldens, and a test corpus registration contract rejects unregistered standalone `.evd` fixtures and expected-output fixtures, with local negative coverage for the broader test corpus contract. A CTest total contract rejects stale release-evidence test counts when the registered suite changes. A release source-tree contract validates a read-only pre-tag audit that catches dirty release branches and tracked generated build-output, release-artifact, or local-only paths. Broad reject smoke tests now cover the registered parser, semantic, authority, affine, typestate, package, and backend rejection corpus as an exit-code safety net beside the diagnostic goldens.
- Representative object and executable tests now validate emitted COFF/PE x64 headers, including COFF object section/symbol/string-table consistency, COFF object section-table, raw-data, relocation-table, and line-number-table bounds for every section, COFF and PE `.text` section shape, PE executable characteristics, console subsystem, PE section-table and raw-data bounds for every section, non-overlapping COFF/PE raw section ranges, nonzero entry point inside `.text`, and header-size consistency, and representative assembly emission now checks for the expected backend entry symbol plus Windows x64/SEH shape markers. Successful emission helpers also assert that the compiler stays quiet on stdout/stderr and writes a non-empty artifact before content checks. Many remaining artifact checks are still mostly existence or exit-code checks rather than deeper semantic validation.
- Executable tests now assert emitted executable existence, non-empty output artifacts, expected process exit codes, and expected stdout/stderr streams, but they still mostly use exit codes as the semantic signal because the current language subset has no general user-facing output facility.
- A Windows x64 CI workflow, shared CMake presets, install rules, install-layout and package-ZIP regression tests, ZIP package and checksum targets, release checklist, toolchain reproducibility policy, and CI ZIP/checksum/evidence artifact upload now exist. Install validation now rejects unsafe or duplicate expected install allowlist paths and unexpected installed files or directories, unsafe expected install allowlist paths and unexpected installed payloads have local negative coverage, install and ZIP validation hash-check README/license/docs contents against the source files, ZIP validation rejects unsafe archive paths, unsafe or duplicate expected package-entry allowlist paths, and unexpected file or directory payloads, extracts the package, compares the packaged compiler's version/help/toolchain outputs against CLI goldens, checks packaged toolchain-probe output shape and identity, smoke-tests token output, the checksum writer rejects non-ZIP package paths and misplaced checksum output paths, checksum validation verifies the package path names a `.zip` archive, verifies the `.sha256` sidecar path, ZIP bytes, ZIP filename, and canonical one-line sidecar format after CRLF/LF normalization, and rejects non-ZIP package paths, tampered package bytes, a misplaced checksum sidecar path, a sidecar naming a different package, a sidecar missing the final newline, or a sidecar with extra trailing blank lines, unsafe ZIP-entry validation has local negative coverage for backslash separators, absolute paths, drive-letter absolute paths, bare package-root entries without a trailing slash, empty path components, `.` / `..` components, duplicate entries, unsafe expected allowlist entries, out-of-root expected allowlist entries, empty-component expected allowlist entries, directory expected allowlist entries, and duplicate expected allowlist entries, the release source-tree audit catches dirty release branches, tracked generated build-output paths, root-level in-source CMake/Ninja/CTest/CPack artifacts, release ZIP/checksum/evidence artifacts, and ignored local-only artifacts before tagging, CI runs that same release source-tree audit before configure/build/test, CI fails on setup/build/test/package command failures, and the CI upload path asserts a single ZIP/checksum pair, validates that pair with the shared package checksum validator before upload, rechecks the downloaded ZIP/checksum pair with the same validator before attestation, appends package size/hash to `evident-release-evidence.txt`, records the full 40-character source commit SHA, runner image OS identifying Windows and non-empty image version, release source-tree audit status inside `[release source tree audit]`, selected Visual Studio path and version inside `[vswhere]`, MSVC `cl` output identifying x64 target architecture, absolute resolved tool paths, tool-version output identifying CMake, Ninja, clang, and LLD, compiler version output matching the release ZIP version, expected compiler toolchain fields with a selected driver that identifies clang and checked-toolchain version probes identifying clang and LLD, the current `358/358` CTest pass summary in `[ctest output]`, and the package filename, size, and SHA256 in `[release ZIP]`, validates the final evidence file with a locally regression-tested release-evidence contract before publishing it, rejects malformed release package names, malformed commit SHAs, evidence commits that do not match the expected source commit, missing runner image values, runner image OS that does not identify Windows, missing release source-tree audit status, incomplete Visual Studio metadata, MSVC compiler evidence that does not identify x64 target architecture, mismatched top-level release scope fields, duplicate singleton provenance fields, duplicate, unexpected, or out-of-order evidence sections, tool-version output that does not identify CMake, Ninja, clang, or LLD, compiler version output that does not match the release ZIP version, missing or mismatched compiler toolchain fields, selected clang driver output that does not identify clang, empty compiler toolchain version-probe lines, checked-toolchain version output that does not identify clang or LLD, extra validated commands, package detail lines before `[release ZIP]`, misplaced package details, misplaced or stale CTest pass summaries, and relative tool paths in release evidence, fails if the upload step does not report a SHA256 `artifact-digest`, pins every remote action reference to a full commit SHA, disables persisted checkout credentials, keeps the build job `GITHUB_TOKEN` at `contents: read`, limits the attestation job to `actions: read`, `attestations: write`, `contents: read`, and `id-token: write`, revalidates the downloaded release evidence against the downloaded ZIP filename, byte size, SHA256, and `GITHUB_SHA` before provenance attestation generation and verification, generates and verifies artifact attestations for public-repository push and manual `workflow_dispatch` builds, and is itself covered by a local CTest workflow-contract validator that rejects mutable official or third-party action tags, unexpected `GITHUB_TOKEN` write permissions, persisted checkout credentials, push-only attestation conditions, source-tree audit omissions or misordering, missing shared checksum validation before upload or attestation, and attestation-before-validation ordering on the release path. The current release path is evidence-bound and rebuildable under the recorded environment rather than hermetic or bit-for-bit reproducible; first hosted-run evidence and fully pinned rebuild infrastructure are still open.

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

- Expand diagnostic golden tests beyond the current representative malformed-parser, type-resolution, visibility, failure-flow, authority, typestate, affine, and backend failures.
- Continue adding malformed-input and recovery-oriented parser tests where they expose distinct parser paths.
- Strengthen backend tests so object/executable checks validate more than file existence or exit code.
- Continue improving external tool discovery beyond the current `EVIDENT_CLANG` override and missing-driver diagnostic.
- Maintain the supported Windows x64 host/target matrix as a tested release-docs contract; any supported-matrix expansion must update `README.md`, `docs/RELEASE_CHECKLIST.md`, `docs/TOOLCHAIN_REPRODUCIBILITY.md`, `docs/NATIVE_BACKEND_PLAN.md`, and the local contract tests together.
- Keep CI building, testing, and packaging through the shared presets on the supported environment.
- Keep generated build directories out of source control. The ignore rules cover `build/`, `build-*`, `out/`, and `cmake-build-*`; any legacy tracked build output should be removed intentionally before cutting a release.

Exit criteria:

- Docs are accurate.
- CI is green on the supported platform.
- Diagnostics are regression-tested.
- Native emission failure modes are documented and test-covered.
- `cmake --install`, the ZIP package, and the ZIP checksum sidecar are regression-tested to contain a runnable compiler executable, README, license, user-facing docs, and a verifiable release hash.

### Milestone 2: Package and Compilation Model

Goal: move from single-file compilation to project-scale compilation.

- Define durable package dependency metadata if the product needs package identity, external dependencies, or build profiles beyond the current source-list manifest.
- Expand tests beyond the current cross-file name resolution, duplicate/import detection, file-local import-gated dependency behavior, original-file diagnostics, backend entry selection, visibility, authority, source-discovery success, manifest success/failure, and package-entrypoint failure coverage when durable package metadata is added.

Why this matters:

- A compiler cannot realistically be called complete if all real programs must live in one source file.

### Milestone 3: Generics

Goal: implement generic compilation as a real language feature rather than parse-only syntax.

- Generalize the current explicit generic function and record instantiation beyond the first concrete paths.
- Extend monomorphization to remaining generic user-defined type cases.
- Define the remaining instantiated generic cases in MIR and backend lowering.
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

- Preserve the documented proof and permit minting rules as fundamental language guarantees.
- Close the remaining gap between current move/affinity checks and full typestate transition support.
- Expand golden tests for authority creation, scope restrictions, token movement, branch joins, and rejected escapes.
- Keep the language docs aligned as broader typestate semantics are added.

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
- Do not call the compiler product-finished while the package model lacks a settled dependency boundary story, source discovery, or cross-file authority/visibility coverage.
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
