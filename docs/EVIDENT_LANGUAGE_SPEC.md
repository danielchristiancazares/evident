# Evident Language Specification

Status: Living draft

Last updated: 2026-03-29

## Scope

This document is the living language specification for Evident.

`docs/CPP_DESIGN.md` and `docs/RUST_DESIGN.md` explain the design philosophy and show how similar constraints can be approximated in host languages. This document records the language itself: its current surface, its current semantic contract, and the active design direction for unresolved areas.

Unless stated otherwise, examples in this document are informative rather than normative.

## Conformance Language

The key words `MUST`, `MUST NOT`, `REQUIRED`, `SHALL`, `SHALL NOT`, `SHOULD`, `SHOULD NOT`, `RECOMMENDED`, `NOT RECOMMENDED`, `MAY`, and `OPTIONAL` in this document are to be interpreted as described in BCP 14 (RFC 2119 and RFC 8174) when, and only when, they appear in all capitals.

Normative keywords in `[Current]` sections describe the checked-in language subset and implementation contract.

Normative keywords in `[Working]` sections describe the intended design direction. They do not yet define a stable compatibility promise.

`[Open]` and `[Deferred]` sections are informative.

## Status Model

- `[Current]` means implemented and expected in the checked-in compiler and test suite.
- `[Working]` means the current design direction. It should guide new design work, but it is not yet a compatibility promise.
- `[Open]` means still under active design discussion.
- `[Deferred]` means intentionally outside the current subset.

## Interpretation

- For implementation questions, `[Current]` behavior should match the compiler and tests.
- For design questions, this document is the primary Evident reference.
- The design essays are rationale, not a substitute for the language specification.

## Defined Terms

- **Declaration terminator**: an optional `;` that may appear after `permit` declarations, `foreign fn` declarations without bodies, non-foreign `fn` declarations without bodies, and trait method declarations.
- **Direct function argument**: an argument expression that is syntactically a single-segment path expression in direct argument position, such as `submit(pass, mesh)` where `pass` is used directly.
- **Instantiated type**: a type after generic parameter substitution has been applied, including recursive substitution of type arguments.
- **Proof-bearing type**: an instantiated type whose effective discipline is affine because it is a `proof` or structurally contains proof-bearing payload.
- **Structural generic abstraction**: a generic abstraction whose semantics do not inspect, branch on, or specialize by the meaning of its type parameters, except for mechanically propagated discipline and capability restrictions.
- **Substrate or infrastructure module [Working]**: an explicitly designated module intended for semantically blind generic abstractions such as collections, caches, queues, middleware, serializers, and similar plumbing. No dedicated surface syntax exists yet.
- **State**: a runtime sum type that encodes alternatives requiring control-flow branching and pattern matching.
- **Phase [Working]**: a static lifecycle refinement over a single shared runtime layout.
- **Proof**: transportable evidence of a fact.
- **Permit**: scoped authority token with limited dynamic extent.
- **Separate structs**: distinct declarations used when a transition changes representation.

## Core Contract

- Evident programs SHOULD be semantically evident. A reader SHOULD be able to infer what exists, what is allowed, and what has been proven from the type structure itself.
- Invalid states SHOULD be designed out of the reachable program.
- Domain semantics SHOULD prefer concrete named types and first-class language constructs over generic indirection.
- Authority and proof are different categories and MUST remain distinct in both syntax and semantics.
- Control-flow obligations MUST be explicit. Failure, authority scope, and state alternatives MUST NOT hide behind convention.

## Compilation Model

- `[Current]` A conforming implementation of the current subset MUST process one source file at a time.
- `[Current]` A source file MAY contain nested `module` declarations that form lexical namespaces.
- `[Deferred]` Package and import-based multi-file compilation.

## Visibility

- `[Current]` Top-level declarations are private by default. A declaration MUST be prefixed with `public` to be exported.
- `[Current]` A public API MUST NOT expose a private type.

## Declaration Forms

`[Current]` A conforming implementation of the current subset MUST recognize these top-level declaration forms:

- `module`
- `struct`
- `state`
- `reason`
- `proof`
- `permit`
- `trait`
- `fn`
- `foreign fn`

Example:

```evd
public struct FeatureConfig {
    endpoint: Text,
    retries: Nat,
}

public state FeatureMode {
    OfflineOnly,
    ProviderBacked { config: FeatureConfig },
}

public reason OpenFailure {
    FileNotFound { path: Text },
    PermissionDenied { path: Text },
}

public proof ConfigValidatedReceipt {
    config: FeatureConfig,
}

public permit RenderPassActive
```

## Compact Grammar

`[Current]` The following grammar is a compact description of the implemented subset. It is intended to be read as an EBNF-style sketch rather than a token-level lexical grammar.

```text
translation-unit  = { declaration } ;

declaration       = [ "public" ] (
                      module-decl
                    | struct-decl
                    | state-decl
                    | reason-decl
                    | proof-decl
                    | permit-decl
                    | trait-decl
                    | foreign-fn-decl
                    | fn-decl
                    ) ;

module-decl       = "module" identifier "{" { declaration } "}" ;
struct-decl       = "struct" identifier [ generic-params ] field-block ;
state-decl        = "state" identifier [ generic-params ] variant-block ;
reason-decl       = "reason" identifier variant-block ;
proof-decl        = "proof" identifier [ field-block ] [ declaration-terminator ] ;
permit-decl       = "permit" identifier [ declaration-terminator ] ;
trait-decl        = "trait" identifier [ generic-params ] "{"
                      { "fn" identifier function-signature [ declaration-terminator ] }
                    "}" ;
foreign-fn-decl   = "foreign" "fn" identifier function-signature [ declaration-terminator ] ;
fn-decl           = "fn" identifier function-signature ( block | [ declaration-terminator ] ) ;

function-signature = [ generic-params ] parameter-list "->" type
                     [ "yields" type ]
                     [ "grants" type ]
                     [ "proves" type ] ;

generic-params    = "<" identifier { "," identifier } ">" ;
parameter-list    = "(" [ parameter { "," parameter } [ "," ] ] ")" ;
parameter         = identifier ":" type ;
type              = path [ "<" type { "," type } ">" ] ;
path              = identifier { "::" identifier } ;

field-block       = "{" [ field { "," field } [ "," ] ] "}" ;
field             = identifier ":" type ;
variant-block     = "{" [ variant { "," variant } [ "," ] ] "}" ;
variant           = identifier [ field-block ] ;

block             = "{"
                      { let-statement [ ";" ] | expression ";" }
                      [ expression ]
                    "}" ;
let-statement     = "let" identifier "=" expression ;

expression        = match-expr | with-expr | try-expr ;
match-expr        = "match" expression "{"
                      [ match-arm { "," match-arm } [ "," ] ]
                    "}" ;
match-arm         = pattern "=>" expression ;
with-expr         = "with" postfix-expr "as" identifier block ;
try-expr          = [ "try" ] fail-expr ;
fail-expr         = "fail" path [ record-initializer ] | prove-expr ;
prove-expr        = "prove" path [ record-initializer ] | postfix-expr ;
postfix-expr      = primary-expr { "(" [ expression { "," expression } [ "," ] ] ")" } ;
primary-expr      = number | string | block | path | construct-expr ;
construct-expr    = path record-initializer ;
record-initializer = "{" [ field-init { "," field-init } [ "," ] ] "}" ;
field-init        = identifier [ ":" expression ] ;

pattern           = succeeded-pattern | failed-pattern | variant-pattern ;
succeeded-pattern = "succeeded" "(" ( identifier | "_" ) ")" ;
failed-pattern    = "failed" "(" variant-pattern ")" ;
variant-pattern   = path [ "{" ( ".." | binding { "," binding } [ "," ] ) "}" ] ;
binding           = identifier [ ":" identifier ] ;

declaration-terminator = ";" ;
```

`[Current]` In the current parser, a `let` statement inside a block MAY omit its trailing semicolon, although explicit semicolons are RECOMMENDED for readability.

## Built-In Types

`[Current]` A conforming implementation of the current subset MUST recognize these built-in type names:

- `Int`
- `Nat`
- `Float`
- `Char`
- `Text`
- `Bytes`
- `Byte`
- `CString`
- `CInt`
- `CSize`
- `Unit`
- `Never`
- `List`
- `List1`
- `Map`
- `Map1`

`[Current]` `Unit` is the empty success/result type.

`[Current]` `Never` is the diverging type used for expressions that do not continue, such as `fail`.

`[Open]` The long-term surface spelling and semantics of collection families are not settled. The current compiler reserves `List`, `List1`, `Map`, and `Map1`, but the language may move toward a more explicit built-in collection syntax and contract.

## Type Categories

### Struct

- `[Current]` A `struct` declaration defines a nominal product type with named fields.
- `[Current]` Struct values MUST be constructed with named fields.
- `[Working]` Transitioning between related concrete layouts SHOULD use distinct `struct` declarations when representation actually changes.

Example:

```evd
public struct FileHandle {
    raw: Int,
}

public fn open_handle(raw: Int) -> FileHandle {
    FileHandle { raw }
}
```

### State

- `[Current]` A `state` declaration defines a closed sum type, i.e., a runtime branch point with alternatives that must be matched.
- `[Current]` State variants MAY be payload-free or carry named fields.
- `[Current]` A `match` over a `state` value MUST be exhaustive.
- `[Current]` Wildcard pattern arms MUST be rejected.
- `[Current]` Pseudo-optional shapes MUST be rejected. A `state` MUST NOT merely encode `Present` versus `Absent`.

Example:

```evd
public state FeatureMode {
    OfflineOnly,
    ProviderBacked { config: FeatureConfig },
}
```

### Reason

- `[Current]` A `reason` declaration defines a closed sum type for yielded failure.
- `[Current]` A function's `yields` clause MUST name a `reason`.
- `[Current]` `reason` declarations MUST NOT be generic in the current subset.
- `[Current]` `reason` types MUST NOT appear in ordinary stored data positions.

### Proof

- `[Current]` A `proof` declaration defines a proof-of-fact token: transportable evidence that a fact has been established.
- `[Current]` Proof values are affine. They MAY be moved, but they MUST NOT be copied or reused after move.
- `[Current]` Proof values MUST be created with `prove`.
- `[Current]` Instantiated generic wrappers MUST inherit affine behavior from their payloads. For example, `Wrapper<Receipt>` is affine when `Receipt` is a proof.
- `[Working]` Proofs represent facts that may be stored, returned, and transported, but never duplicated.

### Permit

- `[Current]` A `permit` declaration defines scoped authority.
- `[Current]` Permit values are compile-time-only authority tokens.
- `[Current]` Permit values MUST NOT be stored in fields or returned from functions.
- `[Current]` Permit values MUST be used only as direct function arguments.
- `[Current]` A `with` scope controls where a permit binding exists; it does not loosen the direct-argument rule.
- `[Working]` Permits are scope-bound authority, not data.

### Trait

- `[Current]` `trait` declarations are parsed and represented in the compiler.
- `[Deferred]` Trait implementations, solving, and method dispatch.

## Phase Families

- `[Working]` A phase family SHOULD be attached to exactly one struct layout.
- `[Working]` Each phase in a phase family SHOULD be a distinct nominal type.
- `[Working]` All phases in a phase family SHOULD share the same runtime fields and layout, so transitions are static lifecycle refinements.
- `[Working]` A phase transition SHOULD consume one phase and yield another.
- `[Working]` Phases are compile-time distinctions, not runtime tags. They SHOULD NOT be matched or inspected at runtime.
- `[Working]` If code needs dynamic uncertainty between alternatives, it SHOULD use `state`.
- `[Working]` If representation differs across alternatives, it SHOULD use separate `struct` declarations; otherwise, use `state` for runtime alternatives.
- `[Working]` Public APIs SHOULD NOT abstract over phases using ordinary generics.
- `[Open]` The exact surface syntax for declaring a phase family and its transitions is not settled.

## Functions

### Function Signatures

`[Current]` Functions MUST have explicit parameter and return types.

Example:

```evd
public fn choose_mode(config: FeatureConfig) -> FeatureMode {
    ProviderBacked { config }
}
```

### `yields`

- `[Current]` `yields` declares the `reason` type a function may fail with.
- `[Current]` Calls to yielding functions MUST be handled with `try` or `match`.
- `[Current]` `try` is valid only inside a function whose `yields` type matches.
- `[Current]` `fail` MUST produce a variant of the enclosing function's yielded `reason`.

Example:

```evd
public fn parse(raw: Text) -> Config yields ParseFailure {
    fail Bad
}

public fn boot(raw: Text) -> Config yields ParseFailure {
    try parse(raw)
}
```

### `grants`

- `[Current]` `grants` marks a function as minting a permit for use in `with`.
- `[Current]` A granting function MUST return `Unit`.
- `[Current]` `with` requires a grant call that grants a permit and returns `Unit`.

Example:

```evd
public fn start_pass() -> Unit grants RenderPassActive {
}

public fn boot(mesh: Mesh) -> FrameReceipt {
    with start_pass() as pass {
        submit(pass, mesh);
        make_receipt(mesh)
    }
}
```

### `proves`

- `[Current]` `proves` marks a function as authorized to mint a specific proof type.
- `[Current]` A `prove` expression MUST construct the proof named by the enclosing `proves` clause.

Example:

```evd
public fn make_receipt(mesh: Mesh) -> FrameReceipt proves FrameReceipt {
    prove FrameReceipt { count: 7 }
}
```

### `foreign fn`

- `[Current]` A `foreign fn` MUST NOT have a body.
- `[Current]` A `foreign fn` MUST NOT declare `yields`.
- `[Current]` A `foreign fn` MUST NOT declare `proves`.

## Expressions and Statements

`[Current]` A conforming implementation of the current subset MUST support this function-body language:

- block expressions
- `let` bindings
- path expressions
- calls
- named-field construction
- `match`
- `try`
- `fail`
- `with`
- `prove`

### Literals

- `[Current]` Integer literals produce `Int`.
- `[Current]` String literals produce `Text`.

### Blocks

- `[Current]` A block MAY contain `let` statements, expression statements, and a trailing result expression.
- `[Current]` A block with no trailing result expression evaluates to `Unit`.
- `[Current]` Control flow that diverges with `Never` MUST NOT contribute a reachable post-block environment.

### Construction

- `[Current]` Struct, proof, and state payload construction use named field syntax.
- `[Current]` Payload-free state variants MAY be used directly by name.

### Match

- `[Current]` `match` over `state` values MUST cover all reachable variants.
- `[Current]` Variant payload patterns support:
  - `{ field }`
  - `{ field: alias }`
  - `{ .. }`
- `[Current]` `_` wildcard patterns MUST be rejected.
- `[Current]` Diverging arms MUST NOT poison later reachable move state. If an affine value is moved only on a `Never` arm, it remains available on the branches that actually reach the join.

Example:

```evd
match mode {
    OfflineOnly => fail FileNotFound { path: "offline" },
    ProviderBacked { config } => FileHandle { raw: 1 },
}
```

### Yielded-Call Match

- `[Current]` A call to a yielding function MAY be matched directly.
- `[Current]` The success arm uses `succeeded(name)`.
- `[Current]` Failure arms use `failed(ReasonVariant ...)`.
- `[Current]` Failure coverage MUST be exhaustive for the yielded reason.

Example:

```evd
match parse(raw) {
    succeeded(config) => config,
    failed(Bad) => Config { raw: "fallback" },
    failed(MissingKey { key }) => Config { raw: key },
}
```

### `with`

- `[Current]` `with grant_call() as permit_name { ... }` introduces a scoped permit binding.
- `[Current]` The permit binding exists only within the `with` body.
- `[Current]` Even within that body, the permit MUST appear only as a direct function argument.

### `prove`

- `[Current]` `prove ProofType { ... }` constructs a proof value.
- `[Current]` Proof construction is valid only within an appropriate `proves` context.

## Undefined or Deferred Behavior

- `[Current]` A non-foreign function MAY omit its body in the current subset.
- `[Current]` A conforming implementation of the current subset MAY accept such declarations.
- `[Deferred]` Complete execution semantics for calling bodyless non-foreign declarations.

## Semantic Rules

`[Current]` A conforming implementation of the current subset MUST reject programs exhibiting at least the following:

- no duplicate declarations within a scope
- no duplicate fields, variants, parameters, trait methods, or generic parameters
- no unknown type references
- no public API leaks of private types
- no empty `state` or `reason` declarations
- no reserved public names such as `Present`, `Missing`, `AllowAll`, or single-letter identifiers
- no pseudo-optional `state` shapes
- no `yields` targeting a non-`reason`
- no `foreign fn` bodies
- no `foreign fn` use of `yields` or `proves`
- no permit types in stored positions or return types
- no proof construction outside `prove`
- no proof reuse after move
- no permit escape through local storage
- no direct use of yielded calls without `try` or `match`
- no `try` outside a compatible `yields` context
- no `fail` using the wrong `reason`
- no non-exhaustive `match`
- no wildcard patterns
- no payload-pattern shape mismatches

## Generics and Parametricity

### Current Compiler State

- `[Current]` Generic parameters are parsed on `struct`, `state`, `trait`, and `fn` declarations.
- `[Current]` Type arguments are parsed on type references.
- `[Current]` Generic type declarations participate in semantic checking and instantiated discipline analysis.
- `[Current]` Instantiated generic containers correctly inherit proof affinity from their payload types.
- `[Current]` Generic function calls are not yet implemented as a full language feature.

### Working Design Direction

- `[Working]` Generics SHOULD be reserved for semantic blindness, not for smuggling domain meaning through placeholder types.
- `[Working]` Domain facts SHOULD normally be modeled with concrete named types or first-class constructs such as `state`, `reason`, `proof`, `permit`, and explicit phases.
- `[Working]` Parametric abstractions MAY be used for infrastructure and plumbing that are intentionally blind to payload meaning.
- `[Working]` Good generic candidates include collection invariants, caches, queues, middleware wrappers, retry/logging/metrics decorators, serializers, and event-bus plumbing.
- `[Working]` Bad generic candidates include wrappers whose primary purpose is to name a domain fact that SHOULD instead be explicit and concrete.
- `[Working]` If a generic abstraction needs to inspect the meaning of `T`, branch on `T`, or specialize behavior by `T`, it SHOULD be treated as a poor fit for Evident.

### Intended Compiler Restrictions

- `[Working]` The intended compiler direction is to give "heavily suspect" generic domain modeling real teeth rather than treating it as style advice.
- `[Working]` Generic `proof`, `permit`, and `reason` declarations SHOULD be rejected.
- `[Working]` User-defined generic `state` declarations SHOULD be rejected.
- `[Working]` Public APIs SHOULD NOT use ordinary generics to abstract over phase families.
- `[Working]` Function-result sum types such as `LoadOutcome[T]` SHOULD usually be expressed with a concrete happy-path return type plus `yields` and concrete `reason` variants, rather than exported generic outcome wrappers.
- `[Working]` Built-in collection families SHOULD remain a special case. They are compiler-owned structural forms, not evidence that arbitrary generic state wrappers are encouraged.
- `[Working]` If user-defined generics remain in the language, they SHOULD be limited to structural `struct` declarations and generic functions whose meaning remains blind to their type parameters.
- `[Working]` Public generics SHOULD be quarantined to explicitly designated substrate or infrastructure modules rather than used as ordinary domain-modeling tools.
- `[Working]` Names such as `Custody[T]`, `Validated[T]`, `Authorized[T]`, `Settled[T]`, or `Draft[T]` SHOULD be treated as strong evidence that a generic abstraction is assigning domain meaning to `T` and SHOULD instead be modeled concretely.

### Proof-Bearing Instantiations

- `[Working]` Generic infrastructure does not become semantically harmless when instantiated with proof-bearing types.
- `[Working]` Instantiation MUST propagate the full discipline of the element or payload type through generic containers and generic functions.
- `[Working]` A structurally blind generic operation MAY still be authority-relevant once instantiated with proof-bearing values, and reviews SHOULD treat it that way even if the abstraction itself never inspects `T`.
- `[Working]` If a generic operation only moves proof-bearing elements, it MAY be valid.
- `[Working]` If a generic operation duplicates proof-bearing elements, stores permits, or otherwise violates instantiated proof/permit rules, it MUST be rejected after instantiation.

Concrete stress cases:

- `[Working]` A future generic helper that moves every element from one collection into another MAY be valid for `Receipt` or other proof-bearing element types because the operation preserves affine movement rather than duplicating values.
- `[Working]` A future generic helper that duplicates, clones, fans out, or retries by retaining the original payload MUST be rejected when instantiated with proof-bearing element types.
- `[Working]` A future generic cache or queue MAY store proof-bearing values only if ordinary proof storage is otherwise legal for that position; genericity does not weaken the rule.
- `[Working]` A future generic cache, queue, or collection instantiated with a `permit` element type MUST still be rejected because permit values may not be stored.
- `[Working]` A future logging or metrics wrapper around an operation returning proof-bearing values MAY be valid only if it observes without duplicating or retaining those values beyond what their instantiated discipline permits.

### Open Questions

- `[Open]` What surface syntax should designate substrate or infrastructure modules strongly enough for the compiler to quarantine public generics there?
- `[Open]` Beyond structural `struct` declarations and generic functions, should Evident expose any additional user-defined generic forms at all?
- `[Open]` Should collection families become fully compiler-owned surface forms rather than ordinary built-in generic names?
- `[Open]` What concrete surface syntax should declare phase families, their shared layout, and their consuming transitions?
- `[Open]` What is the exact trait/generic interaction model once trait implementations exist?

## Traits and Implementations

- `[Current]` Trait declarations exist as part of the parsed and lowered surface.
- `[Deferred]` `impl` syntax.
- `[Deferred]` trait conformance checking.
- `[Deferred]` trait method dispatch.

## Out of Current Scope

- `[Deferred]` package/import-based multi-file compilation
- `[Deferred]` first-class phase-family surface syntax and transition checking
- `[Deferred]` trait implementations and dispatch
- `[Deferred]` a fully specified collection library surface
- `[Deferred]` a completed typestate transition system beyond the current proof/permit/affine model

## Maintenance Rule

When a language decision is made, update this document in the same change if the decision affects syntax, typing, control flow, visibility, authority, proof semantics, or genericity.
