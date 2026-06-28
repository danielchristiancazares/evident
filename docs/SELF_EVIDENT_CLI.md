# `self-evident` CLI Mini Spec

`self-evident` is the human- and agent-facing Evident command.

Its default job is to answer one question quickly:

```text
Is this source self-evident?
```

The command is optimized for the edit-check-fix loop used by people, editors, and LLM agents. It validates source
without requiring native code emission or a platform toolchain.

## Command Shape

```bash
self-evident check [path]
self-evident check --package <dir>
self-evident explain <code>
self-evident doctor
```

`evidc` may remain the low-level compiler executable. `self-evident` is the approachable command for checking, learning,
and agent integration.

## `self-evident check`

`check` validates Evident source and stops before native emission.

Examples:

```bash
self-evident check
self-evident check src/main.evd
self-evident check --package .
```

When no path is provided, `self-evident check` discovers the current package root and behaves as:

```bash
self-evident check --package .
```

when the current directory is an Evident package root.

`check` runs:

- source loading and package discovery
- lexing and parsing
- type checking and semantic analysis
- package and import-boundary checks
- failure-flow checks for `fails`, `try`, `fail`, and failing `match`
- proof, permit, phase, affine, and construction-control checks

`check` does not:

- emit LLVM IR, assembly, objects, or executables
- invoke `clang`, `lld`, `lld-link`, or another native toolchain
- require native target validation

## Diagnostics

Diagnostics are part of the user interface. They should be stable, local, and repair-oriented.

Text diagnostics use this shape:

```text
error[E117]: call may fail with ParseFailure
  --> src/config.evd:18:17

help: use `try parse_config(raw)` inside a function that declares `fails ParseFailure`,
      or match the call with `succeeded(...)` and `failed(...)`.
```

Each diagnostic should include:

- a stable code
- exact source location
- a plain explanation
- one preferred repair when the repair is mechanically clear
- optional context notes when the rule depends on another declaration

Example permit diagnostic:

```text
error[E203]: permit argument required for `render`
  --> src/render.evd:42:9

help: pass the permit explicitly: `render(mesh, as pass)`
```

Example authority diagnostic:

```text
error[E221]: proof `ConfigValidated` may only be created by a function that declares `proves ConfigValidated`
  --> src/config.evd:31:17

help: add `proves ConfigValidated` to the enclosing same-module function, or return a value that does not mint this proof.
```

## Machine-Readable Output

`check` supports a stable machine-readable stream for editors and agents:

```bash
self-evident check --message-format=json
```

The JSON format should include:

- diagnostic code
- severity
- source file
- byte or line/column span
- primary message
- notes
- suggested replacement or rewrite, when available

JSON output is intended to support an agent loop:

1. Generate or edit Evident source.
2. Run `self-evident check --message-format=json`.
3. Apply the suggested local fix when one is present.
4. Repeat until `check` exits successfully.

## Exit Codes

| Code | Meaning |
| ---: | ------- |
| `0` | Source is valid. |
| `1` | Source was checked and has diagnostics. |
| `2` | The command line, package root, filesystem, or environment is invalid. |

`check` should reserve exit code `2` for problems that prevent source validation from running.

## `self-evident explain`

`explain` prints a short explanation for a diagnostic code.

```bash
self-evident explain E117
```

The output should include:

- what the rule protects
- one invalid example
- one valid example
- the preferred fix pattern

## `self-evident doctor`

`doctor` reports local setup information.

It checks:

- the discovered package root
- the selected compiler executable
- the supported native target
- optional native toolchain availability

`doctor` must not be required for `check`. A source-only check remains available even when native code emission tools are
missing.

## Design Principles

- Checking should be fast enough to run after every edit.
- Source-only validation should be independent from native toolchain setup.
- Diagnostics should teach the local rule without requiring the reader to open the full language specification.
- Suggested fixes should prefer explicit Evident syntax: `try`, exhaustive `match`, `as permit`, `grant`, and `prove`.
- Agent-facing output should be stable enough for automated repair loops.
