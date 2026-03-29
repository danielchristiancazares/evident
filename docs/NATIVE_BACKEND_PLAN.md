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

## Immediate next work items

1. broaden type coverage beyond the current builtin / aggregate subset
2. replace stack-heavy first-pass LLVM IR with cleaner SSA-friendly lowering
3. add better diagnostics and portability around external tool discovery
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

- builtins such as `Int`, `Nat`, `Float`, `Char`, `Byte`, `CInt`, `CSize`
- `Text` / `Bytes` as `{ ptr, len }`
- field-order `struct` / `proof`
- tagged-union `state` / `reason`
- yielded-call boundaries via synthetic `YieldResult` wrappers

Executable emission currently requires a top-level `public fn main() -> Int` with no parameters and no `yields`. The older `--emit-stub` output remains a diagnostic path, not the production backend.
