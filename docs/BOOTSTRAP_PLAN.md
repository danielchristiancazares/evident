# Bootstrap Plan

This document defines what "able to bootstrap itself" means for Evident.

The current repository is a C++23 seed compiler. That seed is useful, but it is not self-hosting evidence. A self-hosting Evident compiler requires an Evident implementation of the compiler that the toolchain can compile into a working compiler executable.

## Bootstrap Definition

Evident can be called bootstrap-capable only when all of the following are true:

1. The compiler implementation exists as an Evident package, not only as C++ source.
2. The C++ seed compiler can compile that Evident package into a stage-1 compiler executable.
3. The stage-1 compiler can compile the same Evident compiler package into a stage-2 compiler executable.
4. The stage-2 compiler passes the same required conformance, diagnostics, native emission, package, and release-contract tests as the seed-built compiler.
5. The stage-1 and stage-2 compiler outputs are compared through a documented equivalence check. Byte-for-byte equality is preferred once artifact reproducibility is solved; until then, accepted evidence must compare stable semantic outputs, CLI behavior, diagnostic goldens, and release package validation.

## Current Status

The compiler is not yet self-hosting.

The repository now contains an initial Evident package outline at `bootstrap/compiler`. That package is a compilable bootstrap scaffold, not a compiler implementation. It models package identity, a deterministic source graph, initial lexical token stream data, parsed package syntax data, and a semantic symbol catalog so stage-oriented self-hosting work has real Evident source and compiler-domain data to carry through the pipeline while the larger language and runtime gaps close. Its package identity is derived from the bootstrap environment surface, and it carries host file, path, environment, and tool boundary data plus bootstrap stage-provenance and equivalence records. The Windows CTest suite emits and runs this scaffold through `run_bootstrap_compiler_scaffold` so seed-compiler validation keeps the package natively compilable.

The current blocker is not a single backend issue. The repository still lacks:

- a complete Evident implementation package for the compiler itself
- a stable standard-library or runtime boundary for compiler file I/O, path handling, process exit, and diagnostics
- enough language surface to express the compiler implementation without falling back to hidden ambient authority or unchecked runtime state
- a bootstrap test harness that builds stage 1 and stage 2 compilers and validates equivalence
- a release evidence section that records the bootstrap stages and their exact source commit, toolchain, and validation results

## Bootstrap Architecture Direction

The production bootstrap path should stay native:

```text
C++ seed evidc -> compile Evident compiler package -> stage-1 evidc
stage-1 evidc -> compile same Evident compiler package -> stage-2 evidc
stage-2 evidc -> run conformance, diagnostics, package, and release checks
```

C emission can remain a temporary debugging or migration aid, but it is not the production bootstrap proof. The final bootstrap proof must show that the Evident compiler source can produce a native compiler executable through the documented Evident pipeline.

## Required Language And Toolchain Work

Bootstrap requires at least these capabilities:

- package metadata durable enough to identify the compiler package and its internal source graph
- deterministic source discovery and original-file diagnostics for the compiler package
- enough collection, text, bytes, and map behavior to implement lexing, parsing, diagnostics, and symbol tables
- explicit boundary modules for host file I/O, environment, process exit, and tool invocation
- stable diagnostic rendering so stage outputs can be compared
- native backend coverage for the compiler implementation's concrete data shapes
- release evidence that records the seed compiler, stage-1 compiler, stage-2 compiler, and validation commands

These requirements do not weaken the language contract in `docs/EVIDENT_LANGUAGE_SPEC.md`. Bootstrap work must preserve explicit permits, proof construction, affine discipline, consequence-first naming, and boundary-local collapse of foreign ambiguity.
