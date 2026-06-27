# Evident Release Checklist

This checklist defines the repeatable validation path for the current polished-subset release. It is intentionally scoped to the supported Windows x64 target described in `README.md`, `docs/COMPILER_FINISH_PLAN.md`, and `docs/NATIVE_BACKEND_PLAN.md`.

See `docs/TOOLCHAIN_REPRODUCIBILITY.md` for the exact reproducibility promise. The current release path is evidence-bound and rebuildable under the recorded environment; it is not yet a hermetic bit-for-bit reproducible build.

## Supported Release Scope

- Host: Windows x64.
- Build toolchain: CMake, Ninja, and MSVC x64 from an x64 Visual Studio developer shell.
- Native target: `x86_64-pc-windows-msvc`.
- Native emission toolchain: LLVM `clang` with `lld-link`, selected from `PATH` or through `EVIDENT_CLANG`.
- Package format: relocatable CPack ZIP containing `bin/evidc.exe`, `README.md`, `LICENSE`, and installed docs.

Anything outside this matrix is development work unless the supported validation matrix is updated first.

## Before Tagging

1. Start from a clean intended release branch and inspect the worktree:

   ```bash
   git status --short
   ```

   Then run the read-only release source-tree audit:

   ```bash
   cmake -DSOURCE_DIR=. -P cmake/AssertReleaseSourceTree.cmake
   ```

   This audit fails when the intended release branch is dirty or when generated build, release, or local-only output paths such as `build/`, `build-*`, `out/`, `cmake-build-*`, `CMakeUserPresets.json`, root-level in-source CMake/Ninja/CTest/CPack artifacts such as `CMakeCache.txt`, `CMakeFiles/`, `build.ninja`, `Testing/`, or `_CPack_Packages/`, release artifacts such as `evident-<version>-windows-x64.zip`, `<zip>.sha256`, `evident-release-evidence.txt`, or `release-artifact/`, or local-only artifacts such as `.vs/`, `.vscode/`, `.minimax/`, `.idea/`, `*.obj`, `.DS_Store`, or `repomix-output.txt` are tracked in source control.

2. Enter an x64 Visual Studio developer shell.
3. Confirm required tools resolve in that shell:

   ```bash
   cmake --version
   ninja --version
   clang --version
   lld-link --version
   ```

4. Configure, build, and test through the shared preset:

   ```bash
   cmake --preset windows-x64-ninja
   cmake --build --preset windows-x64-ninja
   ctest --preset windows-x64-ninja
   ```

5. Confirm the compiler reports and probes the selected native toolchain:

   ```bash
   build/windows-x64-ninja/evidc.exe --print-toolchain
   build/windows-x64-ninja/evidc.exe --check-toolchain
   ```

6. Build the release ZIP and checksum:

   ```bash
   cmake --build --preset windows-x64-ninja-package-checksum
   ```

7. Confirm the package validation tests passed. They assert that install and ZIP outputs include the compiler executable, README, LICENSE, and every installed `docs/*.md` file. Install validation rejects unsafe or duplicate expected install allowlist paths, rejects unexpected installed files or directories, validates the installed compiler as a PE executable, and has local negative coverage for unexpected installed payloads and unsafe expected install allowlist paths. It also hash-checks those docs against the source files. ZIP validation rejects unsafe, duplicate, or out-of-root expected package-entry allowlist paths, rejects absolute paths, duplicate entries, unexpected file or directory payloads, backslash separators, bare package-root entries without a trailing slash, empty path components, and `.` / `..` archive path components, requires every entry to stay under the expected package root, extracts the package, validates the packaged compiler as a PE executable, compares packaged `--version`, `--help`, and `--print-toolchain` output against the CLI goldens, checks packaged `--check-toolchain` output shape and verifies that the version probes identify clang and LLD, and smoke-tests `--dump-tokens`. Checksum validation rebuilds the `.sha256` file, and the checksum writer rejects non-ZIP package paths and checksum output paths other than the ZIP path plus `.sha256`. Validation requires the package path to name a `.zip` archive, requires the sidecar path to equal the ZIP path plus `.sha256`, normalizes CRLF/LF line endings, verifies the sidecar is one canonical line containing the ZIP SHA256 and ZIP filename followed by one final newline, and has negative regression coverage proving non-ZIP package paths, sidecars generated before ZIP tampering, misplaced checksum sidecar paths, sidecars naming a different package, sidecars missing the final newline, and sidecars with extra trailing blank lines are rejected.
8. In GitHub Actions, use either the push build for the intended release commit or a manual `workflow_dispatch` run for the same commit, then confirm the release source-tree audit ran before configure/build/test, the setup/build/test/package step completed without command failures, the checksum verification step passed, and the `evident-windows-x64-zip` artifact was uploaded with the ZIP, matching `.sha256` file, and `evident-release-evidence.txt`. The workflow asserts that exactly one ZIP/checksum pair exists, validates that pair with the shared package checksum validator so the sidecar path, canonical text, filename, and ZIP bytes match, and confirms that the final evidence file contains a runner image OS identifying Windows and non-empty image version, Visual Studio instance metadata, resolved tool paths, tool versions, compiler toolchain output, package filename, size, SHA256, and a source commit matching `GITHUB_SHA` before upload. The same evidence contract is regression-tested locally by CTest. Every remote action reference on the release path is pinned to a full commit SHA, every checkout step sets `persist-credentials: false`, the build job keeps `GITHUB_TOKEN` at `contents: read`, the attestation job grants only `actions: read`, `attestations: write`, `contents: read`, and `id-token: write`, and the local workflow contract test rejects mutable official or third-party action tags, unexpected `GITHUB_TOKEN` write permissions, and persisted checkout credentials. After upload, confirm the workflow reported a non-empty SHA256 `artifact-digest` value in the log or job summary. Download that artifact when you need the CI-built ZIP for manual release inspection.
9. For public-repository push builds or manual `workflow_dispatch` release-validation runs, confirm the `attest-windows-x64-release` job rechecked the downloaded ZIP and `.sha256` sidecar with the same package checksum validator and revalidated the downloaded `evident-release-evidence.txt` against the downloaded ZIP filename, byte size, SHA256, and `GITHUB_SHA` before generating artifact attestations for the ZIP, checksum sidecar, and evidence file, then verified each attestation against the repository, `.github/workflows/ci.yml`, and source ref. For private or internal repositories, confirm the GitHub plan supports artifact attestations for that repository visibility, or document the missing attestation as a release deviation.
10. Smoke-test the packaged compiler from an extracted ZIP:

   ```bash
   evident-<version>-windows-x64/bin/evidc.exe --version
   evident-<version>-windows-x64/bin/evidc.exe --help
   evident-<version>-windows-x64/bin/evidc.exe --print-toolchain
   evident-<version>-windows-x64/bin/evidc.exe --check-toolchain
   ```

## Evidence To Record

Record the following in the release notes or tag notes:

- full 40-character commit SHA, matching `GITHUB_SHA` for CI builds
- host image or machine description, including GitHub runner image OS identifying Windows and image version when using GitHub Actions
- Visual Studio Build Tools instance metadata from `vswhere`
- MSVC `cl` output identifying x64 target architecture
- absolute resolved paths plus tool-version output identifying CMake, Ninja, clang, and LLD
- `evidc --version` output matching the version embedded in `evident-<version>-windows-x64.zip`
- `evidc --print-toolchain` output with expected native target, supported target, selected clang driver, override environment variable, and linker mode
- `evidc --check-toolchain` output with those expected fields plus version-probe lines identifying clang and LLD
- source-tree audit status from the `[release source tree audit]` section of `evident-release-evidence.txt`
- `evident-release-evidence.txt` from the CI artifact, when using GitHub Actions
- full preset commands run
- CTest `369/369` pass summary
- package filename in the `evident-<version>-windows-x64.zip` pattern and size
- package SHA256 from the `.sha256` sidecar file
- CI artifact name and digest when using GitHub Actions
- artifact attestation verification status when using a public GitHub Actions release build
- any known deviations from the supported matrix
- whether the release followed the evidence-bound hosted-runner path or a separately pinned toolchain path

## Release Gate

Do not describe a release as production-ready unless:

- the supported validation matrix in `README.md` still matches the commands above
- the read-only release source-tree audit passes on the intended release branch
- the CI workflow runs the same release source-tree audit before configure/build/test
- `ctest --preset windows-x64-ninja` passes
- the package target completes
- install validation rejects unsafe or duplicate expected install allowlist paths
- install validation rejects unexpected installed files or directories
- ZIP validation rejects unsafe, duplicate, or out-of-root expected package-entry allowlist paths
- the ZIP contains only the expected executable, README, license, and installed docs under one package root with no unsafe archive paths or empty path components
- checksum validation rejects non-ZIP package paths
- the `.sha256` sidecar path is exactly the ZIP path plus `.sha256`, and after CRLF/LF normalization it is one canonical line containing the ZIP SHA256 and ZIP filename followed by one final newline
- the CI workflow fails on setup, build, test, and package command failures
- every remote action reference used by the release workflow is pinned to a full commit SHA
- every checkout step used by the release workflow sets `persist-credentials: false`
- the build job keeps `GITHUB_TOKEN` at `contents: read`, and the attestation job grants only `actions: read`, `attestations: write`, `contents: read`, and `id-token: write`
- the CI upload path verifies a single ZIP/checksum artifact pair with the shared package checksum validator, including exact `<zip>.sha256` sidecar path and content binding, before publishing it
- the CI artifact includes validated release evidence with a source commit matching `GITHUB_SHA`, runner image OS identifying Windows and non-empty image version, source-tree audit status inside `[release source tree audit]`, selected Visual Studio path and version inside `[vswhere]`, MSVC `cl` output identifying x64 target architecture, absolute resolved tool paths, tool-version output identifying CMake, Ninja, clang, and LLD, non-empty compiler toolchain output sections, expected compiler toolchain fields including a selected driver that identifies clang, checked-toolchain version probes identifying clang and LLD, `evidc --version` output matching the version embedded in the release ZIP filename, the current `369/369` CTest pass summary inside `[ctest output]`, and package filename, size, and SHA256 inside `[release ZIP]`, with no package detail lines before `[release ZIP]`, the top provenance block matching the supported Windows x64 matrix, singleton fields and known sections present exactly once in the expected order, no unexpected evidence sections, and the validated command list matching the supported release path exactly
- the upload step reports a non-empty SHA256 `artifact-digest`
- the attestation job rechecks the downloaded ZIP and `.sha256` sidecar with the shared package checksum validator before provenance attestation generation and verification
- the attestation job revalidates the downloaded release evidence file against the downloaded ZIP filename, byte size, SHA256, and `GITHUB_SHA` before provenance attestation generation and verification
- public GitHub Actions push or manual `workflow_dispatch` release-validation builds generate and verify artifact attestations for the ZIP, checksum sidecar, and evidence file
- current known limitations are documented in `README.md` and `docs/COMPILER_FINISH_PLAN.md`
