# Toolchain Reproducibility

This document defines the reproducibility promise for the current polished-subset release path.

## Supported Release Toolchain

The supported release validation path is intentionally narrow:

- Host: Windows x64.
- Hosted CI runner: GitHub Actions `windows-2022`.
- Build environment: x64 Visual Studio developer shell.
- CMake generator: Ninja through the `windows-x64-ninja` presets.
- C++ compiler: MSVC x64 from the selected Visual Studio instance.
- Native emission target: `x86_64-pc-windows-msvc`.
- Native emission driver: LLVM `clang`, with `lld-link` available on `PATH`.

The workflow uses `windows-2022`, not `windows-latest`, so the major Windows and Visual Studio family is explicit in source control. GitHub still maintains the contents of hosted runner images over time, so the release artifact must record the exact live runner image and toolchain metadata used for that build.

Reference points:

- GitHub-hosted runner documentation: <https://docs.github.com/actions/using-github-hosted-runners/about-github-hosted-runners>
- GitHub runner image inventory: <https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md>

## Current Reproducibility Promise

Current Evident release artifacts are **evidence-bound and locally rebuildable under the recorded environment**.

This means a release must publish enough evidence to identify:

- the full 40-character source commit SHA, matching `GITHUB_SHA` for CI builds
- the runner label, runner image OS identifying Windows, and non-empty live image version
- the Visual Studio installation path and version selected by `vswhere`
- the release source-tree audit status inside `[release source tree audit]`
- absolute resolved paths for CMake, Ninja, `cl`, `clang`, and `lld-link`
- tool-version output identifying CMake, Ninja, clang, and LLD
- MSVC `cl` output identifying x64 target architecture
- `evidc --version` output matching the version embedded in the release ZIP filename
- `evidc --print-toolchain` output with expected native target, supported target, selected clang driver, override environment variable, and linker mode
- `evidc --check-toolchain` output with those expected fields plus version-probe lines identifying clang and LLD
- the exact preset commands run
- the expected CTest `391/391` pass summary inside `[ctest output]`
- the `evident-<version>-windows-x64.zip` filename, byte size, and SHA256
- the `artifact-digest` value reported by `actions/upload-artifact` after upload
- GitHub artifact attestation status for public-repository push builds or manual `workflow_dispatch` release-validation runs

The current workflow runs the read-only release source-tree audit before configure/build/test, writes the pre-upload provenance fields to `evident-release-evidence.txt`, and validates that file before uploading the release ZIP artifact. The validator rejects missing runner image values, runner image OS output that does not identify Windows, missing `[release source tree audit]` status, incomplete `[vswhere]` Visual Studio metadata, MSVC compiler evidence that does not identify x64 target architecture, mismatched top-level release scope fields, evidence commits that do not match the expected source commit when one is supplied, duplicate singleton provenance fields, duplicate evidence sections, out-of-order evidence sections, unexpected evidence sections, empty tool-version or compiler toolchain output sections, tool-version output that does not identify CMake, Ninja, clang, or LLD, missing or mismatched compiler toolchain fields, selected clang driver output that does not identify clang, empty compiler toolchain version-probe lines, checked-toolchain version output that does not identify clang or LLD, compiler version output that does not match the release ZIP version, extra validated commands, misplaced CTest summaries, package detail lines before `[release ZIP]`, or misplaced package details before upload, so each required field has one authoritative value in the expected evidence flow, the top provenance block records runner image OS identifying Windows and non-empty image version while matching the supported Windows x64 matrix, the release source-tree audit pass line is recorded inside `[release source tree audit]`, the selected Visual Studio path and version are recorded inside `[vswhere]`, the command list matches the supported release path exactly, the CTest pass summary is recorded inside `[ctest output]`, and the package filename, size, and SHA256 are recorded inside `[release ZIP]`. The upload step validates the ZIP/checksum pair with `cmake/AssertPackageChecksum.cmake`, requiring the checksum sidecar path to be the ZIP path plus `.sha256` and the canonical sidecar text to match the ZIP bytes and filename, then reports the GitHub `artifact-digest` value separately in the workflow log and job summary, because the digest is only known after the artifact has been assembled and uploaded. Public-repository push builds and manual `workflow_dispatch` release-validation runs also recheck the downloaded ZIP and `.sha256` sidecar with the same package checksum validator, revalidate the downloaded release evidence against the downloaded ZIP filename, byte size, SHA256, and `GITHUB_SHA`, generate GitHub artifact attestations for the ZIP, checksum sidecar, and evidence file, then verify each attestation against the repository, workflow file, and source ref.
The workflow contract also requires every remote action reference to be pinned to a full commit SHA, including any third-party action reference added later. Checkout steps set `persist-credentials: false` so later workflow steps do not inherit a stored Git credential from checkout. The build job keeps `GITHUB_TOKEN` at `contents: read`; the attestation job is the only job with `actions: read`, `attestations: write`, `contents: read`, and `id-token: write`, matching the permissions needed to create binary artifact attestations.

## What Is Not Promised Yet

The current release process does not promise bit-for-bit reproducible ZIP bytes across independent rebuilds. The main reasons are:

- GitHub-hosted runner images are maintained external inputs.
- Visual Studio, LLVM, CMake, Ninja, and Windows SDK patch levels are not checked into this repository.
- CPack ZIP generation may include archive metadata such as file timestamps.
- Native object and executable emission depends on the selected external LLVM and linker toolchain.

Do not claim a release is hermetic, deterministic, or bit-reproducible unless those inputs have been pinned and a rebuild check proves byte identity.

## Release Gate

A release may use the current supported path only when all of these are true:

1. The read-only release source-tree audit passes on the intended release branch, including rejection of tracked generated build directories, `CMakeUserPresets.json`, root-level in-source CMake/Ninja/CTest/CPack artifacts, release ZIP/checksum/evidence artifacts, and ignored local-only artifacts.
2. The CI workflow runs the same release source-tree audit before configure/build/test.
3. `ctest --preset windows-x64-ninja` passes on the intended release commit.
4. `cmake --build --preset windows-x64-ninja-package-checksum` completes.
5. The install layout validation rejects unsafe or duplicate expected install allowlist paths and unexpected installed entries, and validates the installed compiler as a PE executable.
6. The ZIP package and canonical one-line `.sha256` sidecar validate, including packaged compiler PE structure, safe expected package-entry allowlist paths, checksum-writer rejection of non-ZIP package paths or misplaced checksum output paths, and exact `<zip>.sha256` sidecar path binding after CRLF/LF normalization.
7. The uploaded CI artifact contains:
   - exactly one `evident-*-windows-x64.zip`
   - exactly one matching `<zip>.sha256` sidecar
   - `evident-release-evidence.txt`
8. `evident-release-evidence.txt` passes `cmake/AssertReleaseEvidence.cmake`, including tool-version output identifying CMake, Ninja, clang, and LLD, expected compiler toolchain fields with a selected driver that identifies clang, checked-toolchain version probes identifying clang and LLD, and the rule that package detail lines appear only inside `[release ZIP]`.
9. The upload step reports a non-empty SHA256 `artifact-digest`.
10. The attestation job rechecks the downloaded ZIP and `.sha256` sidecar with the shared package checksum validator before provenance attestation generation and verification.
11. The attestation job revalidates the downloaded release evidence file against the downloaded ZIP filename, byte size, SHA256, and `GITHUB_SHA` before provenance attestation generation and verification.
12. Public GitHub Actions push or manual `workflow_dispatch` release-validation builds have verified artifact attestations for the ZIP, checksum sidecar, and evidence file.
13. Every remote action reference used by the release workflow is pinned to a full commit SHA.
14. Every checkout step used by the release workflow sets `persist-credentials: false`.
15. The build job keeps `GITHUB_TOKEN` at `contents: read`, and the attestation job grants only `actions: read`, `attestations: write`, `contents: read`, and `id-token: write`.
16. The release notes include the evidence file or a verbatim summary of its source commit, toolchain, package filename, byte size, SHA256, artifact digest, and attestation status.

Any deviation from the supported matrix must be documented in the release notes.

## Path To Fully Pinned Rebuilds

To upgrade this from evidence-bound rebuildability to stronger reproducibility, use this order:

1. Pin a self-hosted or image-built Windows x64 environment outside the mutable hosted-runner fleet.
2. Pin exact Visual Studio Build Tools, Windows SDK, LLVM, CMake, and Ninja versions.
3. Make the package archive deterministic, including stable file order, paths, permissions, and timestamps.
4. Add a CI job that rebuilds the release ZIP from the same commit in an independent workspace and compares the final ZIP SHA256.
5. Keep the existing evidence file even after byte reproducibility is achieved, because provenance remains useful for incident response and support.
