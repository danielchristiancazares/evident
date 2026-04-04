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
- record / proof / state construction with named fields
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
- permit values restricted to direct argument or scoped `grant` use
- function body return-type checking
- calls with `fails` used only via `try` or `match`
- `try` only inside compatible `fails` contexts
- `fail` only with the enclosing function's failure reason
- exhaustive `match` over `state`
- exhaustive `failed(...)` coverage for matches over `fails` calls
- wildcard pattern rejection
- payload-pattern shape checks

## Still out of scope

This version does **not** yet implement:

- package/import-based multi-file compilation
- generic function call support
- full typestate transition proofs beyond the current permit/proof/affine enforcement
- optimization passes
- cross-target native backends beyond Windows x64 COFF
- debug info or optimized native code generation

## Planning

- Language spec: `docs/EVIDENT_LANGUAGE_SPEC.md`
- Finish plan: `docs/COMPILER_FINISH_PLAN.md`
- Native backend roadmap: `docs/NATIVE_BACKEND_PLAN.md`

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/evidc --dump-ast examples/feature_mode.evd
./build/evidc --dump-hir examples/feature_mode.evd
./build/evidc --dump-mir examples/feature_mode.evd
./build/evidc --emit-stub out.stub.txt examples/feature_mode.evd
./build/evidc --emit-llvm out.ll examples/feature_mode.evd
./build/evidc --emit-asm out.s examples/feature_mode.evd
./build/evidc --emit-obj out.obj examples/feature_mode.evd
./build/evidc --emit-exe out.exe tests/native_main_constant.evd
```

Native emission currently targets `x86_64-pc-windows-msvc` and defaults to that triple. Executable emission requires a public `fn main() -> Int` (for example inside `domain module ... { ... }`) with no parameters and no `fails`.

## Tests

```bash
cd build && ctest --output-on-failure
```

The test suite includes positive examples plus rejection tests for declaration errors, visibility leaks, pseudo-optionals, foreign-function violations, unknown types, non-exhaustive matches, unhandled calls that may fail, invalid `try`, invalid `fail`, and wildcard patterns.
It also includes MIR golden-output comparisons, LLVM IR golden-output comparisons, assembly/object emission smoke tests, linked executable exit-code tests, and backend entrypoint rejection tests.

## Project layout

```text
include/evident/
  Source.hpp      -- file loading and source locations
  Diagnostic.hpp  -- diagnostic sink and rendering
  Token.hpp       -- token model
  Lexer.hpp       -- hand-written lexer
  Ast.hpp         -- surface AST
```

(See the tree for Parser, Semantic, Hir, Mir, Backend, Driver.)
