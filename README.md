# Evident compiler (C++23)

This repository implements a working Evident front end for the current language subset.

Pipeline:

1. source loading
2. lexing
3. parsing of declarations, function bodies, expressions, and patterns
4. semantic analysis and type checking
5. typed HIR lowering
6. MIR lowering to explicit locals / blocks / terminators
7. LLVM IR emission
8. optional native assembly / object / executable emission via `clang` + `lld`

## Implemented surface language

Translation units are a sequence of module declarations. Each module has an explicit kind:

- `domain`, `boundary`, `foundation`, or `hazard` before `module Name { ... }`

Declaration keywords include:

- `public`
- `record`, `state`, `reason`, `proof`, `permit`, `phase`
- `fn`, `foreign fn`
- `fails` (failure reason on functions), `grants`, `proves`

Function-body syntax:

- block expressions
- `let` bindings
- path expressions and field access (`.field`)
- calls
- explicit generic function calls such as `identity<Int>(value)`
- explicit permit parameters and call arguments with `as name`
- record / proof / state / concrete phase construction with named fields
- explicit, nested, and multiple concrete generic record instantiations such as `Box<Int> { value: 7 }`
- `match`
- `try`, `fail`
- `grant ... as ... { ... }`
- `prove`
- matching calls that may fail via `succeeded(...)` / `failed(...)`
- variant payload patterns including `{ field }`, `{ field: alias }`, and `{ .. }`

## Semantic checks

The compiler currently enforces:

- duplicate declarations in a scope
- duplicate fields, variants, parameters, and generic parameters
- generic declarations quarantined to supported `foundation` forms
- explicit generic call and record-construction type arguments
- reserved or overly generic public names such as `Present`, `Missing`, and `AllowAll`
- empty `state` / `reason` declarations
- pseudo-optional public/state shapes
- unknown type references
- leaking private types through public APIs
- `fails` targeting a non-`reason` type
- foreign functions using `fails`, `grants`, `proves`, generics, or permit parameters, or defining bodies
- reason types forbidden in ordinary data positions
- permit types forbidden in stored positions and return types
- proof values restricted to `prove` and affine move-safe use
- permit values restricted to explicit `as name` arguments or scoped `grant` use
- `grant` failure typing across grantor and body failures
- phase family names rejected as value types or concrete type annotations
- concrete phase construction restricted to the declaring module
- concrete phase values treated as affine
- function body return-type checking
- failing expressions used only via `try` or `match`
- `try` only inside compatible `fails` contexts
- `fail` only with the enclosing function's failure reason
- exhaustive `match` over `state`
- exhaustive `failed(...)` coverage for matches over `fails` calls
- wildcard pattern rejection
- payload-pattern shape checks

## Still out of scope

This version does **not** yet implement:

- package identity or external dependency metadata beyond the source-list manifest
- broader generic user-defined type lowering cases beyond explicit, nested, and multiple concrete generic record construction
- full typestate transition proofs beyond the current permit/proof/affine enforcement
- optimization passes
- cross-target native backends beyond Windows x64 COFF
- debug info or optimized native code generation

## Planning

- Language spec: `docs/EVIDENT_LANGUAGE_SPEC.md`
- Finish plan: `docs/COMPILER_FINISH_PLAN.md`
- Native backend roadmap: `docs/NATIVE_BACKEND_PLAN.md`
- Release checklist: `docs/RELEASE_CHECKLIST.md`

## Build

Supported Windows x64 validation path:

```bash
cmake --preset windows-x64-ninja
cmake --build --preset windows-x64-ninja
ctest --preset windows-x64-ninja
cmake --build --preset windows-x64-ninja-package-checksum
```

Run that preset from an x64 Visual Studio developer shell when validating the MSVC build path. The preset configures `build/windows-x64-ninja`, enables tests, and sets `EVIDENT_CLANG=clang` for native emission tests.

Generic local build path:

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/evidc --help
./build/evidc --version
./build/evidc --print-toolchain
./build/evidc --check-toolchain
./build/evidc --dump-ast examples/feature_mode.evd
./build/evidc --dump-hir examples/feature_mode.evd
./build/evidc --dump-mir examples/feature_mode.evd
./build/evidc --emit-stub out.stub.txt examples/feature_mode.evd
./build/evidc --emit-llvm out.ll examples/feature_mode.evd
./build/evidc --emit-asm out.s examples/feature_mode.evd
./build/evidc --emit-obj out.obj examples/feature_mode.evd
./build/evidc --emit-exe out.exe tests/native_main_constant.evd
```

The `--help`, `--version`, and `--print-toolchain` outputs are stable user-facing CLI surfaces and are covered by golden tests. The `--check-toolchain` command also rejects unsupported native targets, prints the stable toolchain fields for supported targets, then probes the selected `clang --version` and `lld-link --version`; its stable prefix, version-line shape, and unsupported-target failure are regression-tested without pinning one LLVM build version.

Multiple input files can be passed to compile one package translation unit:

```bash
./build/evidc --dump-hir tests/package_support.evd tests/package_consumer.evd
./build/evidc --emit-exe package.exe tests/package_support.evd tests/package_consumer.evd tests/package_main.evd
```

Package directories can be compiled with recursive `.evd` source discovery:

```bash
./build/evidc --dump-hir --package tests/package_dir
./build/evidc --emit-exe package.exe --package tests/package_dir
```

If a package directory contains an `evident.pkg` manifest, the manifest controls the source list and order:

```text
# one relative .evd path per line
src/support.evd
src/app.evd
```

The manifest format intentionally stays small for now: blank lines and lines beginning with `#` are ignored, source paths must be relative `.evd` files inside the package directory, duplicate entries are rejected, and `..` parent traversal is rejected.

The current package model is intentionally minimal: explicit input files are processed in the order provided, `--package <dir>` uses `evident.pkg` when present, and otherwise recursively discovers `.evd` files in deterministic path order. All package inputs share one semantic/HIR/MIR package. Cross-file paths such as `pkg_support::SharedNumber` resolve across the supplied inputs, diagnostics point back to the original source file, and duplicate declarations in a shared scope are rejected.

Top-level import declarations use `import module::path;`. They are source-file declarations checked against the package module graph: the imported path must exist, must name a module, and may not duplicate another import in the same source file. A package with no valid imports keeps the current package-wide fully qualified name resolution. Once a package declares at least one valid import, cross-top-level fully qualified references in each source file must be under a matching import from that same source file. Multiple declarations of the same module path are merged when their module kind matches; split declarations with different module kinds are rejected.

Native emission currently targets `x86_64-pc-windows-msvc` and defaults to that triple. It invokes `clang` from `PATH` and uses `-fuse-ld=lld` for executable linking. The first native ABI pass covers scalar builtins, `CString` pointers, `Text` / `Bytes`, and opaque pointer/length carriers for `List<T>`, `NonEmptyList<T>`, `Map<K, V>`, and `NonEmptyMap<K, V>`. Set `EVIDENT_CLANG` to an absolute `clang` executable path when the desired LLVM driver is not first on `PATH`; a whitespace-only override is rejected before toolchain probing or native artifact emission. Use `--print-toolchain` to print the selected native target, supported target, clang driver, override environment variable, and linker mode without compiling an input file or launching external tools. Use `--check-toolchain` when you want the compiler to reject unsupported native targets, launch the selected driver with `--version`, verify that `lld-link` is available on `PATH`, and report both probed version lines.

String literals default to `Text`; in expected-type positions such as `CString` returns, arguments, and record fields they can type as `Text`, `Bytes`, or `CString`.

Executable emission requires a public `fn main() -> Int` (for example inside `domain module ... { ... }`) with no parameters and no `fails`.

## Supported validation matrix

The current subset-release validation target is:

| Host | Build compiler | Native target | Native toolchain |
|------|----------------|---------------|------------------|
| Windows x64 | MSVC x64 via CMake + Ninja | `x86_64-pc-windows-msvc` | LLVM `clang` with `lld-link` |

The GitHub Actions workflow in `.github/workflows/ci.yml` runs this matrix on `windows-2022` for pushes, pull requests, and manual `workflow_dispatch` release validation. It runs the read-only release source-tree audit before configure/build/test, enters an x64 Visual Studio developer shell, verifies `cmake`, `ninja`, `cl`, `clang`, and `lld-link`, fails the job on any setup/build/test/package command failure, builds the compiler with the `windows-x64-ninja` CMake preset, runs the full CTest suite with the matching test preset, builds the ZIP package plus `.sha256` checksum, verifies that exactly one ZIP/checksum pair exists, validates that pair with the shared package checksum validator so the sidecar path, canonical text, filename, and ZIP bytes match, records the full source commit SHA, runner image OS identifying Windows plus non-empty image version, Visual Studio instance metadata, release source-tree audit status inside `[release source tree audit]`, absolute resolved tool paths, tool-version outputs that identify CMake, Ninja, clang, and LLD, MSVC `cl` output identifying x64 target architecture, compiler version output, expected compiler toolchain fields including a selected driver that identifies clang, checked-toolchain version probes identifying clang and LLD, and the current `390/390` CTest pass summary inside `[ctest output]` in `evident-release-evidence.txt`, validates that evidence file against the workflow `GITHUB_SHA` before upload, uploads the ZIP, checksum, and evidence file as the `evident-windows-x64-zip` workflow artifact, reports the GitHub `artifact-digest` value after upload, and generates and verifies GitHub artifact attestations for public-repository push and manual `workflow_dispatch` builds. The attestation job rechecks the downloaded ZIP and `.sha256` sidecar with the same package checksum validator and revalidates the downloaded release evidence against the downloaded ZIP filename, byte size, SHA256, and workflow `GITHUB_SHA` before provenance attestation generation and verification. Attestation verification binds each artifact to this repository, `.github/workflows/ci.yml`, and the source ref that produced the build. The workflow keeps the build job `GITHUB_TOKEN` at `contents: read`; the attestation job is the only job with `actions: read`, `attestations: write`, `contents: read`, and `id-token: write`. Every remote action reference used by that workflow is pinned to a full commit SHA, each checkout step sets `persist-credentials: false`, and the local CI workflow contract test rejects mutable official or third-party action tags, unexpected `GITHUB_TOKEN` write permissions, and persisted checkout credentials on the release path.

The release-evidence validator rejects missing runner image values, runner image OS output that does not identify Windows, incomplete `[vswhere]` Visual Studio metadata, MSVC compiler evidence that does not identify x64 target architecture, mismatched top-level release scope fields, a commit that does not match the expected source commit when one is supplied, duplicate singleton provenance fields, duplicate evidence sections, out-of-order evidence sections, unexpected evidence sections, empty tool-version or compiler toolchain output sections, tool-version output that does not identify CMake, Ninja, clang, or LLD, missing or mismatched compiler toolchain fields, selected clang driver output that does not identify clang, empty compiler toolchain version-probe lines, checked-toolchain version output that does not identify clang or LLD, compiler version output that does not match the release ZIP version, extra validated commands, misplaced CTest summaries, package detail lines before `[release ZIP]`, or misplaced package details before upload, so each required field has one authoritative value in the expected evidence flow, the top provenance block records runner image OS identifying Windows and non-empty image version while matching the supported Windows x64 matrix, the selected Visual Studio path and version are recorded inside `[vswhere]`, the command list matches the supported release path exactly, the CTest pass summary is recorded inside `[ctest output]`, and the package filename, size, and SHA256 are recorded inside `[release ZIP]`.

The release source-tree audit rejects dirty worktrees and tracked generated build, release, or local-only output before tagging, including generated build directories, `CMakeUserPresets.json`, root-level in-source CMake/Ninja/CTest/CPack artifacts such as `CMakeCache.txt`, `CMakeFiles/`, `build.ninja`, `Testing/`, and `_CPack_Packages/`, release artifacts such as `evident-<version>-windows-x64.zip`, `<zip>.sha256`, `evident-release-evidence.txt`, and `release-artifact/`, and local-only artifacts such as `.vs/`, `.vscode/`, `.minimax/`, `.idea/`, `*.obj`, `.DS_Store`, and `repomix-output.txt`.

See `docs/TOOLCHAIN_REPRODUCIBILITY.md` for the current reproducibility promise. The supported release path is evidence-bound and rebuildable under the recorded environment; it is not yet a hermetic bit-for-bit reproducible build.

## Install And Package

The supported preset build installs the compiler executable plus the repository README, license, and docs:

```bash
cmake --install build/windows-x64-ninja --prefix install/evident
```

The release package path is a relocatable ZIP generated through CPack. For release validation, build the checksum target because it depends on the ZIP package and writes the checksum sidecar:

```bash
cmake --build --preset windows-x64-ninja-package-checksum
```

Use `cmake --build --preset windows-x64-ninja-package` only when you need to build the ZIP without refreshing the checksum.

The package is written under `build/windows-x64-ninja` as `evident-<version>-windows-x64.zip` and contains `bin/evidc.exe` plus the installed README, license, and docs. Release evidence validation rejects package names outside that `evident-<version>-windows-x64.zip` pattern.
The checksum target writes `evident-<version>-windows-x64.zip.sha256` next to the ZIP in standard `<sha256>  <filename>` format. The checksum writer rejects non-ZIP package paths and output paths other than the package path plus `.sha256`. Checksum validation requires the package path to name a `.zip` archive and the sidecar path to be exactly the package path plus `.sha256`, then normalizes CRLF/LF line endings before requiring one checksum record with a final newline.
Install-layout validation rejects unsafe or duplicate expected install allowlist paths, rejects unexpected installed files or directories, validates the installed compiler as a PE executable, and hash-validates README, license, and docs against the source files. Unexpected installed payloads and unsafe expected install allowlist paths have local negative coverage.
The package validation test rejects unsafe, duplicate, or out-of-root expected package-entry allowlist paths, then checks that every ZIP entry stays under the expected package root with no absolute paths, duplicate entries, unexpected file or directory payloads, backslash separators, bare package-root entries without a trailing slash, empty path components, or `.` / `..` path components, extracts that ZIP, validates the packaged compiler as a PE executable, hash-validates the packaged README, license, and docs against the source files, compares packaged `--version`, `--help`, and `--print-toolchain` output against the CLI goldens, checks packaged `--check-toolchain` output shape and verifies that the version probes identify clang and LLD, and smoke-tests `--dump-tokens`.
Checksum validation requires the normalized sidecar to match both the ZIP bytes and the ZIP filename, with negative coverage for non-ZIP package paths, tampered bytes, misplaced checksum sidecar paths, wrong package names, sidecars missing the final newline, and sidecars with extra trailing blank lines.
Use `docs/RELEASE_CHECKLIST.md` as the release gate before tagging or publishing that ZIP.

## Tests

```bash
cd build && ctest --output-on-failure
```

For the supported Windows x64 path, prefer:

```bash
ctest --preset windows-x64-ninja
```

The test suite includes positive examples plus rejection tests for declaration errors, visibility leaks, pseudo-optionals, foreign-function violations, unknown types, package duplicate/import detection, package import dependency gates, package CLI/source-discovery/manifest failures, cross-file package visibility and authority errors, non-exhaustive matches, unhandled calls that may fail, invalid `try`, invalid `fail`, and wildcard patterns.
Representative parser, semantic, and backend failures also have diagnostic golden-output comparisons, with local registration contracts and negative coverage that reject unregistered invalid source fixtures, diagnostic goldens, standalone `.evd` fixtures, and expected-output fixtures. A CTest total contract rejects stale release-evidence test counts when the registered suite changes, and a C++ design contract rejects core escape hatches forbidden by `docs/CPP_DESIGN.md`.
It also includes package-scale HIR comparison, MIR golden-output comparisons, LLVM IR golden-output comparisons, CLI help/version/toolchain golden-output comparisons, CI workflow contract validation for the release artifact path, assembly/object emission smoke tests, COFF object header and section-table validation, PE executable header and section-table validation for generated executables before exit-code checks, install-layout and extracted package-ZIP validation including README/license/docs contents, native ABI coverage for `CString` and compiler-owned collection carriers, and backend entrypoint rejection tests.

## Project layout

```text
include/evident/
  Source.hpp      -- file loading, source locations, and segmented package source mapping
  Diagnostic.hpp  -- diagnostic sink and rendering
  Token.hpp       -- token model
  Lexer.hpp       -- hand-written lexer
  Ast.hpp         -- surface AST
```

(See the tree for Parser, Semantic, Hir, Mir, Backend, Driver.)
