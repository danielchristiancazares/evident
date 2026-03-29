# Evident compiler (C++23)

This repository now implements a working Evident front end for the current language subset.

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

Top-level declarations:

- `module`
- `public`
- `struct`
- `state`
- `reason`
- `proof`
- `permit`
- `trait`
- `fn`
- `foreign fn`
- `yields`

Function-body syntax:

- block expressions
- `let` bindings
- path expressions
- calls
- struct / proof / state construction with named fields
- `match`
- `try`
- `fail`
- `with`
- `prove`
- yielded-call matching via `succeeded(...)` / `failed(...)`
- variant payload patterns including `{ field }`, `{ field: alias }`, and `{ .. }`

## Semantic checks

The compiler currently enforces:

- duplicate declarations in a scope
- duplicate fields, variants, parameters, trait methods, and generic parameters
- reserved or overly generic public names such as `Present`, `Missing`, and `AllowAll`
- empty `state` / `reason` declarations
- pseudo-optional public/state shapes
- unknown type references
- leaking private types through public APIs
- `yields` targeting a non-`reason` type
- foreign functions using `yields`, `proves`, or defining bodies
- reason types forbidden in ordinary data positions
- permit types forbidden in stored positions and return types
- proof values restricted to `prove` and affine move-safe use
- permit values restricted to direct argument or scoped `with` use
- function body return-type checking
- yielded calls used only via `try` or `match`
- `try` only inside compatible `yields` contexts
- `fail` only with the enclosing function's yielded reason
- exhaustive `match` over `state`
- exhaustive `failed(...)` coverage for yielded-call matches
- wildcard pattern rejection
- payload-pattern shape checks

## Still out of scope

This version does **not** yet implement:

- package/import-based multi-file compilation
- generic function call support
- trait implementations, solving, or method dispatch
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

Native emission currently targets `x86_64-pc-windows-msvc` and defaults to that triple. Executable emission requires a top-level `public fn main() -> Int` with no parameters and no `yields`.

## Tests

```bash
cd build && ctest --output-on-failure
```

The test suite includes positive examples plus rejection tests for declaration errors, visibility leaks, pseudo-optionals, foreign-function violations, unknown types, non-exhaustive matches, unhandled yielded calls, invalid `try`, invalid `fail`, and wildcard patterns.
It also includes MIR golden-output comparisons, LLVM IR golden-output comparisons, assembly/object emission smoke tests, linked executable exit-code tests, and backend entrypoint rejection tests.

## Project layout

```text
include/evident/
  Source.hpp      -- file loading and source locations
  Diagnostic.hpp  -- diagnostic sink and rendering
  Token.hpp       -- token model
  Lexer.hpp       -- hand-written lexer
  Ast.hpp         -- surface AST
  Parser.hpp      -- recursive-descent parser
  Semantic.hpp    -- semantic analysis / type checking
  Hir.hpp         -- typed lowered IR with function bodies
  Mir.hpp         -- CFG-style MIR with explicit locals and terminators
  Backend.hpp     -- LLVM-backed native backend surface
  Driver.hpp      -- CLI pipeline
src/
  *.cpp           -- implementations
cmake/
  RunAndCompareOutput.cmake
examples/
  feature_mode.evd
tests/
  *.evd / *.mir.txt
```


## Native direction

The current backend path is native code on Windows x64 COFF through emitted LLVM IR. A C emitter may still be useful as a bootstrap or debugging tool, but it is not the language target. See `docs/NATIVE_BACKEND_PLAN.md`.
