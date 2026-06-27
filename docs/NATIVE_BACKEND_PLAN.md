# Native backend plan

Evident should compile to native machine code.

C is not treated as a backend target here. Emitting C would make Evident a transpiler, which may be useful for bootstrapping or debugging, but it is not the intended production architecture.

## Correct terminology

- The **target** is a concrete native platform, such as `x86_64-unknown-linux-gnu`.
- The **backend** lowers typed IR to native instructions, object files, and relocations for that target.
- **LLVM IR** is an implementation substrate, not the language target.
- **C emission** is transpilation, not native backend code generation.

## Recommended production pipeline

`source -> AST -> HIR -> MIR -> native codegen -> object file -> system linker -> executable`

## Recommended first target

- ISA: `x86_64`
- platform triple: `x86_64-pc-windows-msvc`
- ABI: Windows x64 / MSVC
- object format: `COFF`
- linker: `lld-link` via `clang -fuse-ld=lld`
- tool discovery: `clang` is found on `PATH` by default; set `EVIDENT_CLANG` to an explicit executable path to choose a specific driver; `evidc --print-toolchain` reports the selected target and driver without compiling an input file or launching external tools, and `evidc --check-toolchain` rejects unsupported native targets before probing the selected driver plus `lld-link` with `--version`

## Immediate next work items

1. broaden type coverage beyond the current builtin / aggregate subset
2. replace stack-heavy first-pass LLVM IR with cleaner SSA-friendly lowering
3. keep improving diagnostics, provenance, and portability around external tool discovery
4. add debug info, optimization levels, and release-quality native output
5. add additional target triples beyond Windows x64 COFF

## Two valid native strategies

### LLVM-assisted

`source -> AST -> HIR -> MIR -> LLVM IR -> object file -> linker`

This is still a native compiler because the final artifact is native machine code.

### Direct backend

`source -> AST -> HIR -> MIR -> machine IR -> object file -> linker`

This gives full backend ownership, but it is more work.

## Current repository posture

The repository now lowers validated AST into typed HIR, then into explicit MIR with locals, blocks, `invoke`, and variant switches. That MIR lowers into textual LLVM IR for `x86_64-pc-windows-msvc`, and the driver can emit:

- `--emit-llvm <path>`
- `--emit-asm <path>`
- `--emit-obj <path>`
- `--emit-exe <path>`

The first native ABI/layout pass currently implements:

- builtins such as `Int`, `Nat`, `Float`, `Char`, `Byte`, `CInt`, `CSize`, and `CString`
- `Text` / `Bytes`, `List<T>` / `NonEmptyList<T>`, and `Map<K, V>` / `NonEmptyMap<K, V>` as `{ ptr, len }`
- field-order `record` / `proof`
- tagged-union `state` / `reason`
- failing-call boundaries via synthetic yield-result wrappers in LLVM

Executable emission currently requires a public `fn main() -> Int` with no parameters and no `fails` (for example inside a `domain module`). The older `--emit-stub` output remains a diagnostic path, not the production backend.

Native assembly, object, and executable emission invoke the LLVM `clang` driver. If the process cannot be launched, the backend reports the selected driver and suggests installing LLVM or setting `EVIDENT_CLANG`. The `--print-toolchain` command reports the configured native target, the supported target, the selected clang driver, the override environment variable, and the linker mode without launching external tools. The `--check-toolchain` command first rejects unsupported native targets, then prints the same stable fields, launches the selected driver with `--version`, verifies `lld-link --version`, and reports both first version lines.

The test suite includes representative COFF object and PE executable validation for the supported x64 target by checking artifact headers, section-table bounds, COFF symbol/string-table bounds, `.text` section shape, and PE entry-point placement. Representative assembly validation also checks the backend entry symbol and Windows x64/SEH markers, in addition to broader smoke tests for assembly, object, and executable emission.

The primary CI path is the Windows x64 workflow in `.github/workflows/ci.yml`. It runs the read-only release source-tree audit before configure/build/test, enters an x64 Visual Studio developer shell, builds with the `windows-x64-ninja` CMake preset, runs the full CTest suite with the matching test preset and `EVIDENT_CLANG=clang`, then verifies the ZIP package and checksum targets.

The release workflow records the release source-tree audit status, runner image OS identifying Windows plus non-empty image version, Visual Studio metadata, MSVC x64 compiler banner, resolved tool paths, tool-version output identifying CMake, Ninja, clang, and LLD, compiler version output matching the release ZIP version, expected compiler toolchain fields with a selected driver that identifies clang, and release ZIP filename, size, and SHA256 in validated release evidence before upload. The attestation job revalidates the downloaded ZIP, checksum sidecar, and release evidence before provenance attestation generation. See `docs/TOOLCHAIN_REPRODUCIBILITY.md` for the current rebuildability promise and the remaining work needed for hermetic, bit-for-bit reproducible release artifacts.
