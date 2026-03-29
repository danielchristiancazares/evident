# Evident compiler (C++23)

This repository now implements a working Evident front end for the current language subset.

Pipeline:

1. source loading
2. lexing
3. parsing of declarations, function bodies, expressions, and patterns
4. semantic analysis and type checking
5. HIR lowering
6. stub backend emission

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
- foreign functions using `yields` or defining bodies
- reason types forbidden in ordinary data positions
- permit types forbidden in stored positions and return types
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

- generic function call inference
- trait solving / method dispatch
- typestate transition proofs
- permit/proof minting restrictions beyond type-position checks
- optimization passes
- real code generation beyond the stub backend

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/evidc --dump-ast examples/feature_mode.evd
./build/evidc --dump-hir examples/feature_mode.evd
./build/evidc --emit-stub out.stub.txt examples/feature_mode.evd
```

## Tests

```bash
cd build && ctest --output-on-failure
```

The test suite includes positive examples plus rejection tests for declaration errors, visibility leaks, pseudo-optionals, foreign-function violations, unknown types, non-exhaustive matches, unhandled yielded calls, invalid `try`, invalid `fail`, and wildcard patterns.

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
  Hir.hpp         -- lowered summary IR + stub emitter
  Driver.hpp      -- CLI pipeline
src/
  *.cpp           -- implementations
examples/
  feature_mode.evd
tests/
  *.evd
```
