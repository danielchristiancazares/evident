# Evident Language Specification

**Status:** Redesigned working draft  
**Last updated:** 2026-03-30

## 1. Scope

This document defines the target surface and semantic contract for Evident.

This draft is a redesign. It intentionally replaces the prior ambient-authority model with explicit permit passing, removes retroactive phase backpatching, makes module kind spelling explicit everywhere, and makes construction control part of the normative core.

This document is a language-and-toolchain contract, not a source-compatibility promise for any currently checked-in compiler.

Unless a section is explicitly marked **[Deferred]**, the requirements below are normative.

## 2. Conforming Toolchain

A conforming Evident toolchain consists of a compiler plus the mandatory static checks required by this document.

This document does not distinguish whether a rejection is emitted by the parser, the type checker, or a required static analysis pass. If a program violates a **MUST** or **MUST NOT** requirement, that program is non-conforming.

## 3. Core Contract

Evident is designed around categorical honesty and explicit consequence.

A conforming Evident design MUST preserve all of the following:

* A reader MUST be able to infer, from the surface alone, what values exist, what can fail, what has been proven, what permit is being used, and what lifecycle position each phased value occupies.
* Invalid states MUST be designed out of the reachable program.
* Durable runtime alternatives, failure explanations, proofs of fact, scoped authority, and lifecycle positions are different categories and MUST remain distinct in both syntax and semantics.
* Boundary ambiguity MUST collapse in the exact boundary function that first receives it. It MUST NOT propagate into ordinary domain surfaces.
* Authority MUST never be ambient. An authorized call MUST name the permit it uses.
* Granting, proving, construction, and phase transition rules MUST NOT depend on hidden compiler inference or surrounding scope magic.
* Generic abstraction MUST remain structurally blind and quarantined to `foundation` modules.
* Public names MUST be consequence-first. Public surfaces MUST describe downstream meaning rather than mechanism or origin history.
* Sentinel values, pseudo-optionals, wildcard bypass variants, and hidden parser-invented defaults MUST NOT be used to smuggle ambiguity into the program.

## 4. Defined Terms

For this document:

**Data type** means a built-in type, `record`, `state`, `proof`, concrete `phase` type, or compiler-owned collection instantiated only with data types.

**Contract type** means a `reason` or `permit`.

**Affine-bearing type** means a data type whose discipline is affine because it is a `proof`, a concrete `phase` type, or structurally contains one.

**Copyable data type** means a data type that is not affine-bearing.

**Permit binding** means a named, scope-local authority introduced by `grant ... as name { ... }` or by a permit parameter in a function declaration. A permit binding is not an ordinary value.

**Grantor function** means a function that declares `grants P` for some permit type `P`.

**Phase family** means a `phase` declaration that creates concrete lifecycle-position types such as `AppConfig::Draft` and `AppConfig::Validated`.

**Phase family name** means the bare identifier of a `phase` declaration, such as `AppConfig`. A phase family name is not itself a value type.

**Exact boundary function** means the first boundary-facing function that receives foreign ambiguity such as nullability, external booleans, stringly discriminators, integerly typed enums, parser looseness, or other schema uncertainty.

**Foundation module** means a module reserved for semantically blind plumbing and the only place where user-defined generics may be declared.

**Hazard module** means a module reserved for low-level system interaction, FFI, concurrency coordination, runtime orchestration, and other operations whose hazards are inherently runtime-visible.

## 5. Module Model

Declarations other than `module` declarations MUST appear inside a module.

Every module declaration, top-level or nested, MUST spell its module kind explicitly. Module kind inheritance does not exist.

Evident defines four module kinds.

### 5.1 `domain module`

A `domain module` is the normal home for domain truth.

A `domain module` MAY declare:

* `record`
* `state`
* `reason`
* `proof`
* `permit`
* `phase`
* `fn`

A `domain module` MUST NOT declare `foreign fn`.

A `domain module` MUST NOT declare user-defined generics.

### 5.2 `boundary module`

A `boundary module` is the place where foreign ambiguity collapses into strict internal forms.

A `boundary module` MAY declare:

* `record`
* `state`
* `reason`
* `proof`
* `permit`
* `phase`
* `fn`
* `foreign fn`

A `boundary module` MUST collapse foreign looseness in the exact boundary function that first receives it.

Public exports from a `boundary module` MUST use data types in ordinary value positions and explicit contract clauses where needed. Raw foreign ambiguity MUST NOT appear in public boundary exports.

A `boundary module` MUST NOT declare user-defined generics.

### 5.3 `foundation module`

A `foundation module` is reserved for semantically blind plumbing.

A `foundation module` MAY declare:

* `record`
* `fn`

A `foundation module` MAY declare user-defined generics, subject to the restrictions in Section 16.

A `foundation module` MUST NOT declare public `state`, `reason`, `proof`, `permit`, or `phase` declarations.

A `foundation module` MUST NOT assign domain meaning to generic parameters.

### 5.4 `hazard module`

A `hazard module` isolates runtime-visible hazards such as OS interaction, network interaction, scheduling, FFI, concurrency coordination, low-level orchestration, and compatibility glue.

A `hazard module` MAY declare:

* `record`
* `state`
* `reason`
* `proof`
* `permit`
* `phase`
* `fn`
* `foreign fn`

A `hazard module` MAY define hazard-local proofs, permits, reasons, states, and phases when those categories are genuine consequences of hazardous interaction.

A `hazard module` MUST NOT be the default home for ordinary domain truth.

A `hazard module` MUST NOT declare user-defined generics.

## 6. Visibility and Construction Control

Top-level and nested declarations are private by default.

A declaration MUST be prefixed with `public` to be exported from its enclosing module.

Fields of `record`, `proof`, and `phase` declarations are private by default. A field MUST be prefixed with `public` to be accessible from outside the declaring module.

A public API MUST NOT expose a private type in any of the following positions:

* ordinary parameter type
* permit parameter type
* return type
* `fails` clause
* `grants` clause
* `proves` clause
* public record field type
* public proof field type
* public phase field type

Construction control is part of the core language:

* A `record` value may be directly constructed in a module only if the record is visible there and every field named by that construction is visible there.
* Because record construction uses named fields and must provide all fields, a hidden field blocks direct external construction.
* A `state` variant may be constructed wherever the `state` and chosen variant are visible.
* A `proof` may be constructed only by `prove`.
* A concrete `phase` type may be directly constructed only inside its declaring module.
* A permit is never directly constructed by user code.

## 7. Declaration Forms

The language core recognizes these declaration forms:

* `module`
* `record`
* `state`
* `reason`
* `proof`
* `permit`
* `phase`
* `fn`
* `foreign fn`

The language core does not define `trait`, `impl`, open method dispatch, or reflection-based typing features.

## 8. Compact Grammar

The following grammar is an EBNF-style sketch of the intended surface. It is not a token-level lexical grammar.

```text
translation-unit   = { module-decl } ;

module-decl        = [ "public" ] module-kind "module" identifier
                     "{" { declaration } "}" ;
module-kind        = "domain" | "boundary" | "foundation" | "hazard" ;

declaration        = [ "public" ] (
                       module-decl
                     | record-decl
                     | state-decl
                     | reason-decl
                     | proof-decl
                     | permit-decl
                     | phase-decl
                     | foreign-fn-decl
                     | fn-decl
                     ) ;

record-decl        = "record" identifier [ generic-params ] field-block ;
state-decl         = "state" identifier variant-block ;
reason-decl        = "reason" identifier variant-block ;
proof-decl         = "proof" identifier [ field-block ] [ ";" ] ;
permit-decl        = "permit" identifier [ ";" ] ;
phase-decl         = "phase" identifier "{"
                       "fields" field-block
                       "positions" position-block
                     "}" ;

fn-decl            = "fn" identifier [ generic-params ] parameter-list "->" type
                     { contract-clause }
                     block ;

foreign-fn-decl    = "foreign" "fn" identifier foreign-parameter-list "->" type [ ";" ] ;

contract-clause    = "fails" type
                   | "grants" type
                   | "proves" type ;

generic-params     = "<" identifier { "," identifier } ">" ;
parameter-list     = "(" [ parameter { "," parameter } [ "," ] ] ")" ;
parameter          = value-parameter | permit-parameter ;
value-parameter    = identifier ":" type ;
permit-parameter   = "as" identifier ":" type ;

foreign-parameter-list
                   = "(" [ value-parameter { "," value-parameter } [ "," ] ] ")" ;

type               = path [ "<" type { "," type } ">" ] ;
path               = identifier { "::" identifier } ;

field-block        = "{" [ field { "," field } [ "," ] ] "}" ;
field              = [ "public" ] identifier ":" type ;

variant-block      = "{" [ variant { "," variant } [ "," ] ] "}" ;
variant            = identifier [ payload-block ] ;
payload-block      = "{" [ payload-field { "," payload-field } [ "," ] ] "}" ;
payload-field      = identifier ":" type ;

position-block     = "{" identifier { "," identifier } [ "," ] "}" ;

block              = "{"
                       { statement }
                       [ expression ]
                     "}" ;
statement          = let-statement ";"
                   | expression ";" ;
let-statement      = "let" identifier "=" expression ;

expression         = try-expr
                   | match-expr
                   | grant-expr
                   | fail-expr
                   | prove-expr
                   | postfix-expr ;

try-expr           = "try" expression ;
match-expr         = "match" expression "{"
                       [ match-arm { "," match-arm } [ "," ] ]
                     "}" ;
match-arm          = pattern "=>" expression ;

grant-expr         = "grant" call-expr "as" identifier block ;
fail-expr          = "fail" path [ record-initializer ] ;
prove-expr         = "prove" path [ record-initializer ] ;

postfix-expr       = primary-expr { field-access | call-suffix } ;
field-access       = "." identifier ;
call-suffix        = "(" [ call-arg { "," call-arg } [ "," ] ] ")" ;
call-arg           = expression | permit-arg ;
permit-arg         = "as" identifier ;

primary-expr       = number | string | block | path | construct-expr ;
construct-expr     = path record-initializer ;
record-initializer = "{" [ field-init { "," field-init } [ "," ] ] "}" ;
field-init         = identifier [ ":" expression ] ;

pattern            = succeeded-pattern | failed-pattern | variant-pattern ;
succeeded-pattern  = "succeeded" "(" ( identifier | "_" ) ")" ;
failed-pattern     = "failed" "(" reason-pattern ")" ;
reason-pattern     = path [ "{" ( ".." | binding { "," binding } [ "," ] ) "}" ] ;
variant-pattern    = path [ "{" ( ".." | binding { "," binding } [ "," ] ) "}" ] ;
binding            = identifier [ ":" identifier ] ;

call-expr          = postfix-expr ;   // semantic restriction: must end in a call
```

## 9. Built-In Types

A conforming implementation MUST recognize these built-in type names:

* `Int`
* `Nat`
* `Float`
* `Char`
* `Text`
* `Bytes`
* `Byte`
* `CString`
* `CInt`
* `CSize`
* `Unit`
* `Never`

A conforming implementation MUST also recognize these compiler-owned collection families:

* `List<T>`
* `NonEmptyList<T>`
* `Map<K, V>`
* `NonEmptyMap<K, V>`

`Unit` is the empty success type.

`Never` is the diverging type of expressions that do not continue, such as `fail`.

There is no built-in `Bool`, no built-in optional type, and no nullable reference type in the core language.

The completed collection library surface is **[Deferred]**.

## 10. Modeling Rules and Boundary Discipline

Public modeling MUST follow the rules in this section.

### 10.1 Consequence-first naming

Public names MUST describe downstream consequence or established fact.

A public `state` variant MUST name either the resulting behavior or the established runtime alternative.

A public `reason` variant MUST name the failure consequence or the concrete reason for denial.

A public `proof` name MUST describe the fact that has been proven.

A public `permit` name MUST describe the authority that has been granted.

A public phase position name MUST describe lifecycle position, not parser history or vague status.

Public names MUST NOT describe only mechanism, origin function, or generic container state.

### 10.2 No pseudo-optionals

A `state` MUST NOT merely encode presence versus absence.

Public variant names such as `Absent`, `Present`, `Missing`, `Unknown`, `Known`, `None`, or equivalent absence wrappers are forbidden when one branch means only “no value”.

If the domain truly has alternatives, those alternatives MUST be named by consequence.

### 10.3 No wildcard bypass variants

Public configuration, policy, and authority models MUST NOT include a universal escape-hatch variant such as:

* `Any`
* `All`
* `AllowAll`
* `Unrestricted`

Policy MUST be modeled with explicit finite alternatives and explicit merge rules.

### 10.4 No sentinel values

Sentinel values and in-band placeholders MUST NOT encode absence, failure, denial, not-yet-initialized state, or not-found state in public surfaces.

Examples of forbidden public sentinel practice include zero IDs used as “not assigned”, empty text used as “missing”, all-zero handles used as “not opened”, and equivalent in-band markers.

### 10.5 Exact boundary collapse

Foreign ambiguity MUST collapse in the exact boundary function that first receives it.

This includes:

* external booleans
* nullability
* external optionality
* stringly typed discriminators
* integerly typed enums
* parser defaults
* partially missing configuration
* FFI looseness
* foreign schema uncertainty

Such ambiguity MUST NOT be stored, forwarded, or bound to locals whose scope outlives the immediate collapse logic.

The boundary either:

* rejects the input with an explicit `reason`, or
* translates it into a fully inhabited internal form.

The parser or boundary adapter MUST NOT silently invent domain values.

## 11. Value Discipline

Evaluation is by immutable values. The core language defined by this document has no assignment and no mutable variable rebinding.

Every data type is either copyable or affine-bearing.

A data type is affine-bearing if it is:

* a `proof`,
* a concrete `phase` type, or
* any record, state, or collection that structurally contains one.

All other data types are copyable.

The semantic model is value-copy for copyable data. Implementations MAY optimize storage and copying so long as observable behavior matches the rules below.

### 11.1 Binding, arguments, and returns

For copyable data types:

* `let x = e` copies the result of `e` into `x`
* passing an argument copies it into the callee
* returning a value copies it into the caller

For affine-bearing data types:

* `let x = e` moves the result of `e` into `x`
* passing an argument moves it into the callee
* returning a value moves it into the caller

A moved affine-bearing binding MUST NOT be reused.

### 11.2 Permit bindings

A permit binding is not an ordinary value.

A permit binding:

* is introduced only by a permit parameter or by `grant ... as name { ... }`
* is passed explicitly with `as name`
* is not copied
* is not moved
* is not stored
* is not returned
* may be used repeatedly while its lexical scope remains active

Passing a permit with `as name` authorizes the call and does not consume the permit binding.

### 11.3 Field access

Field access on a copyable receiver yields a copy of the selected field.

Field access on an affine-bearing receiver is valid only when the selected field type is copyable. Such access yields a copy of the field and does not move the receiver.

Direct extraction of an affine-bearing field from another affine-bearing value is not part of the core language.

### 11.4 Matching and consumption

`match` on a copyable `state` does not consume the subject.

`match` on an affine-bearing `state` consumes the subject. Copyable payload bindings are copied into the arm. Affine-bearing payload bindings are moved into the arm.

`match` over a failing expression follows the same success/failure control-flow rules regardless of whether the success value is copyable or affine-bearing.

## 12. Type Categories

### 12.1 `record`

A `record` defines a nominal product type with named fields.

Fields are private by default.

Record values MUST be constructed with named fields.

A record may be directly constructed only where all of its fields are visible.

Example:

```evd
public record FeatureConfig {
    public endpoint: Text,
    public retries: Nat,
}
```

A user-defined generic `record` MAY appear only in a `foundation module`.

A `record` that structurally contains an affine-bearing field becomes affine-bearing.

### 12.2 `state`

A `state` defines a closed sum type of runtime alternatives.

A `state` variant MAY be payload-free or carry named payload fields.

State payload fields are visible wherever the chosen variant is visible.

A `match` over a `state` value MUST be exhaustive.

Wildcard catch-all matching is forbidden.

Pseudo-optional shapes are forbidden. A `state` MUST NOT merely encode absence.

Example:

```evd
public state FeatureMode {
    OfflineOnly,
    ProviderBacked { config: FeatureConfig },
}
```

A `state` MUST NOT be generic.

### 12.3 `reason`

A `reason` defines a closed sum type for failure explanation and control flow.

A function’s `fails` clause MUST name exactly one `reason` type.

A `reason` MUST NOT be generic.

Reason payload fields are visible wherever the reason type is visible.

A `reason` type MUST NOT appear in stored data positions, including:

* record fields
* state payload fields
* proof fields
* phase fields
* collection element types
* generic arguments in ordinary data positions

A `reason` path MAY appear only in:

* a `fails` clause
* a `fail` expression
* a `failed(...)` match arm pattern

Example:

```evd
public reason ParseFailure {
    BadSyntax,
    MissingKey { key: Text },
}
```

### 12.4 `proof`

A `proof` defines a proof-of-fact token.

A `proof` MAY have zero or more named fields.

Proof fields are private by default.

Proof values are affine. They MAY be moved, but they MUST NOT be copied or reused after move.

A `proof` MUST be constructed only with `prove`.

A `proof` MAY be stored, returned, or placed inside records, states, and collections, subject to affine discipline.

Only functions declared in the same module as a proof type MAY declare `proves` for that proof type.

Example:

```evd
public proof ConfigValidated {
    public config_id: Text,
}
```

A `proof` MUST NOT be generic.

### 12.5 `permit`

A `permit` defines scope-local authority.

A `permit` is not data.

A permit binding exists only as a named lexical authority. It is always visible where it is usable.

A permit type MUST NOT appear in any ordinary data position, including:

* record fields
* state payload fields
* proof fields
* phase fields
* ordinary function parameters
* function return types
* collection element types
* generic arguments
* patterns
* ordinary expressions

A permit type MAY appear only in:

* a `permit` declaration
* a permit parameter (`as name: PermitType`)
* a `grants` clause

A permit binding MAY appear only in:

* a `grant ... as name { ... }` binder
* a permit argument written as `as name`

Only functions declared in the same module as a permit type MAY declare `grants` for that permit type.

Example:

```evd
public permit RenderPassActive
```

A `permit` MUST NOT be generic.

### 12.6 `phase`

A `phase` declaration defines a closed lifecycle family with shared fields and named positions.

A `phase` declaration is self-contained. It does not target a previously declared `record`.

Example:

```evd
public phase AppConfig {
    fields {
        public id: Text,
        public payload: Text,
    }

    positions {
        Draft,
        Validated,
    }
}
```

This declaration creates two concrete value types:

* `AppConfig::Draft`
* `AppConfig::Validated`

The phase family name `AppConfig` is not a value type.

All concrete phase types in a family share the declared field layout.

Concrete phase types are affine. They MAY be moved, but they MUST NOT be copied or reused after move.

Concrete phase types are not runtime tags. They MUST NOT be matched. If runtime uncertainty must be inspected, the model MUST use `state`.

If different lifecycle positions require different fields, the model MUST use separate records or a `state`. A phase family MUST NOT be used to smuggle per-position shape differences through sentinels, unused fields, or pseudo-optionals.

Direct construction of a concrete phase type is valid only within the declaring module.

A `phase` family MUST NOT be generic.

## 13. Type-Position Rules

The following type-position rules are reserved by category:

* An ordinary value parameter type MUST be a data type.
* A permit parameter type MUST be a `permit`.
* A function return type MUST be a data type.
* A `fails` clause MUST name a `reason`.
* A `grants` clause MUST name a `permit`.
* A `proves` clause MUST name a `proof`.
* A `reason` type is valid only in reason-handling positions.
* A `permit` type is valid only in permit parameter positions and `grants` clauses.
* A `proof` type is an ordinary data type, but it is affine.
* A concrete `phase` type is an ordinary data type, but it is affine.
* A phase family name is not a value type.

## 14. Functions and Contract Clauses

Functions MUST have explicit parameter and return types.

A function MAY declare:

* zero or one `fails` clause
* zero or one `grants` clause
* zero or more `proves` clauses

A contract clause MUST NOT be repeated with the same target type.

The order of contract clauses is not semantically meaningful.

### 14.1 `fails`

`fails R` declares that the function may fail with reason type `R`.

`R` MUST be a `reason`.

A call to a failing function is a failing expression.

A failing call MUST be handled with `try` or `match`.

Reason compatibility is exact. `try` is valid only when the callee’s declared reason type exactly matches the enclosing function’s declared reason type.

### 14.2 `grants`

`grants P` declares that the function may bestow permit `P`.

`P` MUST be a `permit`.

Only functions in the same module as `P` may declare `grants P`.

A function with `grants` MUST return `Unit`.

A function with `grants` MAY also declare `fails`.

A function with `grants` MUST be invoked only through `grant`. An ordinary direct call to a grantor function is invalid.

### 14.3 `proves`

`proves Q` declares that the function is authorized to mint proof type `Q`.

`Q` MUST be a `proof`.

Only functions in the same module as `Q` may declare `proves Q`.

A `prove` expression inside the function MUST name one of the proof types listed by its enclosing `proves` clauses.

### 14.4 `foreign fn`

A `foreign fn` declares an external function without a body.

A `foreign fn` MAY appear only in `boundary` or `hazard` modules.

A `foreign fn` parameter list may contain only ordinary value parameters.

A `foreign fn` MUST NOT declare:

* `fails`
* `grants`
* `proves`

## 15. Expressions and Statements

The function-body language includes:

* blocks
* `let` bindings
* path expressions
* field access
* calls
* named-field construction
* `match`
* `try`
* `fail`
* `grant`
* `prove`

### 15.1 Blocks

A block MAY contain statements followed by an optional trailing result expression.

A block with no trailing result expression evaluates to `Unit`.

Diverging control flow with type `Never` MUST NOT contribute a reachable post-block environment.

### 15.2 Construction

Records, state payload variants, and concrete phase types use named-field construction syntax.

Examples:

```evd
FeatureConfig { endpoint: "https://x", retries: 3 }

FeatureMode::ProviderBacked { config: cfg }

AppConfig::Draft { id: "cfg-1", payload: raw }
```

Direct construction of a `record` is valid only where all record fields are visible.

Direct construction of a `proof` outside `prove` is invalid.

Direct construction of a concrete `phase` type outside its declaring module is invalid.

### 15.3 Calls and explicit permit passing

Permit use is explicit in both signatures and calls.

Example:

```evd
public fn submit(mesh: Mesh, as pass: RenderPassActive) -> Unit
    fails DrawFailure
{
    {}
}
```

A call to that function must spell the permit argument explicitly:

```evd
submit(mesh, as pass)
```

Ordinary arguments follow ordinary parameter positions.

Permit arguments written as `as name` follow permit parameter positions.

Passing a permit does not consume it.

### 15.4 `match` over a `state`

A `match` subject in the core language MUST be either:

* a `state` value, or
* a failing expression

No other `match` subject forms are defined by the core language.

A `match` over a `state` value MUST cover all reachable variants.

Wildcard arms are forbidden.

Payload patterns support:

* `{ field }`
* `{ field: alias }`
* `{ .. }`

Example:

```evd
match mode {
    FeatureMode::OfflineOnly => fallback,
    FeatureMode::ProviderBacked { config } => config,
}
```

### 15.5 `match` over a failing expression

A failing expression MAY be matched directly.

The success arm uses `succeeded(name)`.

Failure arms use `failed(ReasonVariant ...)`.

Failure coverage MUST be exhaustive for the declared `reason`.

Example:

```evd
match parse(raw) {
    succeeded(config) => config,
    failed(ParseFailure::BadSyntax) => fallback,
    failed(ParseFailure::MissingKey { key }) => recover(key),
}
```

### 15.6 `try`

`try e` is valid only when `e` is a failing expression and the enclosing function declares the exact same `reason` type.

On success, `try e` yields the success value of `e`.

On failure, `try e` propagates that failure to the enclosing function.

### 15.7 `fail`

`fail ReasonPath { ... }` diverges with `Never`.

A `fail` expression is valid only within a function whose `fails` clause names that exact `reason` type.

### 15.8 `grant`

`grant call as permit_name { body }` invokes a grantor function, binds a visible permit name for the lexical extent of `body`, and yields the result of `body`.

The operand of `grant` MUST be a direct call expression whose callee declares `grants P` for some permit type `P`.

The binder introduced by `as permit_name` has type `P` and is usable only inside the `grant` body.

The permit binding is explicit. It does not arrive from surrounding scope, and it does not become ambient.

The permit binding ceases to exist when control leaves the `grant` body.

If the grantor call fails, the body is not evaluated.

Failure typing for `grant` is exact:

* if neither the grantor call nor the body is failing, the `grant` expression is non-failing
* if exactly one of them is failing with reason `R`, the `grant` expression is failing with reason `R`
* if both are failing, they MUST use the same reason type `R`, and the `grant` expression is failing with reason `R`
* otherwise the `grant` expression is invalid

Example:

```evd
public permit RenderPassActive

public reason DrawFailure {
    PassDenied,
    SubmissionRejected,
}

public fn start_pass() -> Unit
    grants RenderPassActive
    fails DrawFailure
{
    {}
}

public fn submit(mesh: Mesh, as pass: RenderPassActive) -> Unit
    fails DrawFailure
{
    {}
}

public fn draw(mesh: Mesh) -> Unit
    fails DrawFailure
{
    try grant start_pass() as pass {
        try submit(mesh, as pass)
    }
}
```

### 15.9 `prove`

`prove ProofType { ... }` constructs a proof value.

A `prove` expression is valid only within a function that declares `proves ProofType`.

The expression yields a proof value of the named proof type.

Example:

```evd
public phase AppConfig {
    fields {
        public id: Text,
        public payload: Text,
    }

    positions {
        Draft,
        Validated,
    }
}

public proof ConfigValidated {
    public config_id: Text,
}

public record ValidationResult {
    public config: AppConfig::Validated,
    public receipt: ConfigValidated,
}

public reason ValidationFailure {
    EmptyPayload,
}

public fn validate(config: AppConfig::Draft) -> ValidationResult
    fails ValidationFailure
    proves ConfigValidated
{
    let next = AppConfig::Validated {
        id: config.id,
        payload: config.payload,
    };

    let receipt = prove ConfigValidated { config_id: next.id };

    ValidationResult {
        config: next,
        receipt,
    }
}
```

## 16. Generics and Parametricity

User-defined generics are intentionally small and quarantined.

### 16.1 Allowed generic forms

Only a `foundation module` MAY declare user-defined generics.

Within a `foundation module`, the only generic declaration forms are:

* generic `record`
* generic `fn`

There is no generic syntax for:

* `state`
* `reason`
* `proof`
* `permit`
* `phase`

There is no trait system, reflection system, constraint syntax, or open polymorphism in the core language.

### 16.2 Structural blindness

A user-defined generic abstraction MUST remain structurally blind.

A generic abstraction MUST NOT assign domain meaning to its type parameters.

Good foundation examples include abstractions equivalent to:

* queue
* cache
* bag
* buffer
* collection wrapper
* serialization envelope
* middleware shell

Bad foundation examples include abstractions equivalent to:

* `Validated<T>`
* `Authorized<T>`
* `Settled<T>`
* `Draft<T>`

Those names indicate that the type parameter is carrying lifecycle, authority, proof, or domain fact. Those meanings belong to first-class language constructs, not generic wrappers.

### 16.3 Affine propagation through generics

Instantiation MUST propagate affine discipline.

If a generic record, collection, or function is instantiated with an affine-bearing type, the instantiated form is itself affine-bearing wherever that payload is structurally retained.

A generic abstraction that would duplicate an affine-bearing payload MUST be rejected after instantiation.

A generic abstraction that only moves or relays an affine-bearing payload MAY be valid.

## 17. Required Static Rejections

A conforming Evident toolchain MUST reject at least the following:

* declarations outside modules
* any module declaration without an explicit module kind
* duplicate declarations within a scope
* duplicate record fields, payload fields, phase positions, parameters, or generic parameters
* unknown type references
* public API leakage of private types
* a public boundary export that forwards foreign ambiguity past the exact boundary function
* generic declarations outside a `foundation module`
* generic `state`, `reason`, `proof`, `permit`, or `phase` declarations anywhere
* a `foundation module` that assigns domain meaning to generic parameters
* a `foreign fn` outside a `boundary` or `hazard` module
* a `foreign fn` with a body
* a `foreign fn` with any contract clause
* a `foreign fn` with a permit parameter
* a `reason` type in a stored data position
* a `permit` type in an ordinary parameter, return, field, collection, pattern, or generic-argument position
* an ordinary parameter whose type is a `permit`
* a permit parameter whose type is not a `permit`
* `fails` targeting a non-`reason`
* `grants` targeting a non-`permit`
* `proves` targeting a non-`proof`
* a function with `grants` that does not return `Unit`
* a function that declares `grants P` outside the module that declares `P`
* a function that declares `proves Q` outside the module that declares `Q`
* an ordinary direct call to a function with `grants`
* a `grant` expression whose operand is not a direct call to a grantor function
* a `grant` expression whose grantor call and body fail with different reason types
* direct construction of a `proof` outside `prove`
* a `prove` expression outside a matching `proves` context
* a `fail` expression outside a matching `fails` context
* direct use of a failing expression without `try` or `match`
* `try` outside an exact matching `fails` context
* non-exhaustive `match`
* wildcard catch-all match arms
* a `match` subject that is neither a `state` value nor a failing expression
* pseudo-optional `state` shapes
* public absence-only names
* public wildcard bypass names
* public sentinel-value modeling
* silent parser or boundary default injection of domain values where rejection or explicit translation is required
* direct construction of a concrete `phase` type outside its declaring module
* use of a phase family name as a value type
* direct matching on a concrete `phase` type
* field access to a private record, proof, or phase field from outside its declaring module
* direct construction of a `record` from a module that cannot see all required fields
* direct extraction of an affine-bearing field from an affine-bearing carrier
* reuse of an affine-bearing value after move

## 18. Deferred Areas

The following areas are intentionally outside this draft:

* package and import-based multi-file compilation
* mutation and assignment semantics beyond the immutable core defined here
* trait declarations, trait bounds, and open polymorphism
* explicit phase-family helper syntax for “any position of a family”
* the completed collection library surface
* concurrency and runtime coordination primitives beyond module classification
* macros and compile-time metaprogramming
* custom constructor syntax beyond field visibility, `grant`, and `prove`

## 19. Maintenance Rule

When a language decision changes syntax, typing, visibility, construction control, value discipline, proof semantics, permit semantics, phase semantics, module classification, or genericity, this document MUST be updated in the same change.
