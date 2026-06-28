# Evident Language Specification

**Status:** Redesigned working draft  
**Last updated:** 2026-06-27

## 1. Scope

This document defines the target surface and semantic contract for Evident.

This draft is a redesign. It intentionally replaces the prior ambient-authority model with explicit permit passing, removes retroactive phase backpatching, makes module kind spelling explicit everywhere, and makes construction control part of the normative core.

This document is a language-and-toolchain contract, not a source-compatibility promise for any currently checked-in compiler.

Unless a section is explicitly marked **[Deferred]**, the requirements below are normative.

## 2. Conforming Toolchain

A conforming Evident toolchain consists of a compiler plus the mandatory static checks required by this document.

This document does not distinguish whether a rejection is emitted by the parser, the type checker, or a required static analysis pass. If a program violates a **MUST** or **MUST NOT** requirement, that program is non-conforming.

The external-symbol protocol assertions attached to a bodyless `foreign fn` declaration are declaration-side obligations
on the author of that declaration. A conforming toolchain MUST statically enforce the source-visible `foreign fn` rules in
this document, but it is not required to prove the lifetime, mutability, stability, or storage behavior of an external
symbol from Evident source alone. Such assertions are trusted raw-adapter assertions. If an asserted external-symbol
protocol fact is false, the program is non-conforming and behavior that depends on that false assertion is outside Evident
semantics.

## 3. Core Contract

Evident is designed around categorical honesty and explicit consequence.

A conforming Evident design MUST preserve all of the following:

* A reader MUST be able to infer, from the surface alone, what values exist, what can fail, what has been proven, what permit is being used, and what lifecycle position each phased value occupies.
* Invalid states MUST be designed out of the reachable program.
* Durable runtime alternatives, failure explanations, proofs of fact, scoped authority, and lifecycle positions are different categories and MUST remain distinct in both syntax and semantics.
* Foreign ambiguity and runtime looseness at boundary or hazard ingress MUST collapse in the exact boundary function that first receives it. It MUST NOT propagate into ordinary domain surfaces.
* Authority MUST never be ambient. An authorized call MUST name the permit it uses.
* Granting, proving, construction, and phase transition rules MUST NOT depend on hidden compiler inference or surrounding scope magic.
* Generic abstraction MUST remain structurally blind and quarantined to `foundation` modules.
* Public-name enforcement MUST use closed, source-visible reservations so consequence-first naming never depends on hidden compiler interpretation.
* Source-visible sentinel values, pseudo-optionals, wildcard bypass variants, and hidden parser-invented defaults MUST NOT be used to smuggle ambiguity into the program.

## 4. Defined Terms

For this document:

**Data type** means a built-in type, `record`, `state`, `proof`, concrete `phase` type, or compiler-owned collection instantiated only with data types.

**Contract type** means a `reason` or `permit`.

**Affine-bearing type** means a data type whose discipline is affine because it is a `proof`, a concrete `phase` type, or structurally contains one.

**Copyable data type** means a data type that is not affine-bearing.

**Permit binding** means a named, scope-local authority introduced by `grant ... as name { ... }` or by a permit parameter in a function declaration. A permit binding is not an ordinary value.

**Permit-authorized operation** means a call expression that supplies at least one permit argument with `as name` to a
callee permit parameter.

**Grantor function** means a function that declares `grants P` for some permit type `P`.

**Phase family** means a `phase` declaration that creates concrete lifecycle-position types such as `AppConfig::Draft` and `AppConfig::Validated`.

**Phase family name** means the bare identifier of a `phase` declaration, such as `AppConfig`. A phase family name is not itself a value type.

**Exact boundary function** means the first boundary-facing or hazard-facing function that receives foreign ambiguity or runtime looseness such as nullability, external booleans, stringly discriminators, integerly typed enums, parser looseness, FFI looseness, status-code looseness, invalid handles, partial runtime state, callback-order uncertainty, resource lifetime uncertainty, or other schema or runtime uncertainty.

**Inhabited internal value** means a non-raw value whose type is a consequence-carrying data type under Section 10.6 or a
transparent primitive/sequence record under Section 10.6. A bare core primitive, bare core sequence, raw ABI substrate
value, raw-adapter export, or raw-adapter-exposing value is not an inhabited internal value.

**Foundation module** means a module reserved for semantically blind plumbing and the only place where user-defined generics may be declared.

**Hazard module** means a module reserved for low-level system interaction, FFI, concurrency coordination, runtime orchestration, and other operations whose hazards are inherently runtime-visible.

## 5. Module Model

Declarations other than `module` declarations MUST appear inside a module.

Every module declaration, top-level or nested, MUST spell its module kind explicitly. Module kind inheritance does not exist.

An implementation MAY accept more than one source file in a single package compilation. In that case, the supplied files are parsed in deterministic package order as one package translation unit, name resolution is package-wide, and diagnostics MUST report the original source file and location. A package MUST NOT contain duplicate declarations in the same non-module scope. Multiple declarations of the same module path merge into one module scope only when they use the same module kind; split declarations with different module kinds MUST be rejected.

The current compiler supports two package entrypoints: an explicit input-file list and `--package <dir>`. A package directory MAY contain an `evident.pkg` manifest with one relative `.evd` source path per non-empty, non-comment line. Manifest source paths MUST stay inside the package directory, MUST NOT contain `..`, MUST end in `.evd`, and MUST be unique. If the manifest is absent, `--package <dir>` recursively discovers `.evd` sources in deterministic path order.

A source file MAY declare top-level imports with `import module::path;`. An import path MUST resolve to a module, and a source file MUST NOT repeat the same import path. A package with no valid imports keeps package-wide fully qualified name resolution for compatibility with the current source-list model. Once a package declares at least one valid import, cross-top-level fully qualified references in each source file MUST be under a matching import from that same source file.

This package model is source-set compilation only. External package identity, package dependency resolution, build
profiles, version constraints, and cross-package linking metadata are deferred.

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

Public exports from a `boundary module` that are not raw-adapter exports as defined in Section 10.6 MUST use data types
in ordinary value positions and explicit contract clauses where needed. Raw-adapter exports MAY expose raw or foreign
substrate types only under Section 10.6. Other public boundary exports MUST NOT contain raw foreign ambiguity.

A `boundary module` MUST NOT declare user-defined generics.

### 5.3 `foundation module`

A `foundation module` is reserved for semantically blind plumbing.

A `foundation module` MAY declare only:

* `record`
* `fn`

A `foundation module` MAY declare user-defined generics on `record` and `fn` declarations only, subject to the
restrictions in Section 16.

A `foundation module` MUST NOT declare `state`, `reason`, `proof`, `permit`, `phase`, or `foreign fn` declarations at
any visibility.

A `foundation module` MUST NOT assign domain meaning to generic parameters.

Every `foundation` declaration, regardless of visibility, MUST be semantically blind structural plumbing. For this rule,
the declaration's semantic surface includes every user-authored declaration-surface name listed in Section 10.7, not only
exported names. A `foundation` declaration's semantic surface MUST NOT contain a semantic-claim marker from Section 10.6.
A `foundation` nominal declaration MUST NOT be a semantic-claim nominal declaration; for `foundation module`
declarations, the primitive/sequence-backed classification in Section 10.6 applies at every visibility. Private
`foundation` records MAY use raw, text, bytes, and generic carriers only as semantically blind structural plumbing. They
MUST NOT construct, name, or expose validation, authority, lifecycle, protocol, status, handle, receipt, proof-token,
credential, or content-predicate meaning.

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

A `hazard module` that receives foreign or runtime looseness MUST collapse that looseness according to Section 10.5 before any non-raw-adapter export can expose the result.

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
* public state variant payload type
* public reason variant payload type
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
prove-expr         = "prove" path record-initializer ;

postfix-expr       = primary-expr { field-access | call-suffix | construct-suffix } ;
field-access       = "." identifier ;
call-suffix        = [ generic-args ] "(" [ call-arg { "," call-arg } [ "," ] ] ")" ;
construct-suffix   = generic-args record-initializer ;
generic-args       = "<" type { "," type } ">" ;
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

In this draft, an `identifier` token is ASCII-only. It MUST begin with an ASCII letter or `_`, and every following
character MUST be an ASCII letter, ASCII digit, or `_`. A keyword is not an identifier. The single-token identifier `_` is
reserved for wildcard syntax where the grammar admits it and is invalid as a user-authored declaration-surface name under
Section 10.7.

### 8.1 Source text and string literals

Source files are UTF-8 byte sequences. A conforming implementation MUST reject ill-formed UTF-8 source input. After UTF-8
decoding, source text is a sequence of Unicode scalar values. Surrogate code points are not Unicode scalar values and
MUST NOT appear in source text or be produced by escapes.

A string literal starts with `"` and ends with the next unescaped `"`. It denotes the Unicode scalar sequence produced by
processing its contents from left to right. A line terminator is U+000A, U+000D, U+2028, or U+2029. An unescaped line
terminator, unescaped `"`, unescaped `\`, or unescaped U+0000 inside a string literal is invalid.

The only valid string escapes are:

* `\"` for U+0022
* `\\` for U+005C
* `\n` for U+000A
* `\r` for U+000D
* `\t` for U+0009
* `\0` for U+0000
* `\u{H}` for the Unicode scalar value named by one to six hexadecimal digits `H`

A `\u{H}` escape MUST name a value no greater than U+10FFFF and MUST NOT name a surrogate code point. Any other escape
sequence is invalid. A string literal contains U+0000 when its processed Unicode scalar sequence contains U+0000,
including when that scalar came from `\0` or `\u{0}`.

## 9. Built-In Types

A conforming implementation MUST recognize these admitted built-in type names:

* `Int`
* `Nat`
* `Float`
* `Char`
* `Text`
* `NonEmptyText`
* `Bytes`
* `NonEmptyBytes`
* `Byte`
* `CString`
* `CInt`
* `CSize`
* `Unit`
* `Never`

`Float` is a binary floating-point value type. A conforming implementation MUST represent `Float` as the finite values of
the IEEE 754 binary64 (double-precision) format. The admitted `Float` value set is exactly the finite binary64 values:
every normal and subnormal binary64 number together with a single zero. IEEE 754 NaN, positive infinity, and negative
infinity are not admitted `Float` values. Signed zero is not observable: a conforming implementation MUST normalize the
binary64 `-0` to `0`, so exactly one zero value exists. Because every admitted `Float` value is finite and zero is
unique, `Float` equality is reflexive and the numeric order over `Float` is total. `Float` is a copyable data type.

For the supported `x86_64-pc-windows-msvc` target, the admitted `Float` value set is the finite IEEE 754 binary64 values:
`0`, and every nonzero magnitude from the smallest positive subnormal `2^-1074` (approximately `4.9406564584124654e-324`)
through the largest finite `(2 - 2^-52) * 2^1023` (approximately `1.7976931348623157e308`), together with the negation of
each nonzero magnitude. A conforming implementation MUST round every admitted `Float` result to the nearest representable
binary64 value using the IEEE 754 roundTiesToEven rounding-direction attribute; inexact rounding is the defined result of
an operation and is not a failure.

Any literal, operation, conversion, or foreign or runtime ingress that would produce a NaN, positive infinity, or
negative infinity `Float` value MUST be rejected with an explicit `reason` or collapsed into an admitted finite `Float`
value in the exact boundary, hazard, or operation that produces it. The IEEE 754 invalid-operation, division-by-zero, and
overflow conditions therefore surface as explicit Evident consequences rather than as in-band `Float` values; an
underflowed result collapses to the nearest admitted finite value, which may be `0`. A boundary or hazard adapter MUST
reject or keep outside the strict Evident surface any foreign double that is NaN or infinite before admitting it as
`Float`. The `Float` arithmetic and conversion operation surface is deferred under Section 18; the `Float` type, its
finite value set, and float literals are normative in this draft.

A conforming implementation MUST also recognize these compiler-owned collection families:

* `List<T>`
* `NonEmptyList<T>`
* `Map<K, V>`
* `NonEmptyMap<K, V>`

These collection families MUST be used with exactly the type-argument arity shown above. Non-collection built-in
types MUST NOT be used with type arguments.

This draft admits exactly one native target:

| Target triple | Native emission | Foreign ABI binding | Exact target-specific value sets |
|---------------|-----------------|---------------------|----------------------------------|
| `x86_64-pc-windows-msvc` | supported | supported | specified below |

A conforming implementation of this draft MUST reject native target selection, native artifact emission, and `foreign fn`
binding for any target not listed in this table. Future target admission MUST first specify the exact target-specific value
sets, foreign ABI value representations, symbol binding rules, calling convention, and rejection rules for that target.

`Int` is a signed integer value type. A conforming implementation MUST document the exact representable value set for
`Int` for each admitted target, either as a minimum and maximum value or as an unbounded mathematical value set. `Nat` is an
unbounded non-negative integer value type. `Nat` MUST NOT contain negative values.

For the supported `x86_64-pc-windows-msvc` target, the exact integer value sets are:

| Type | Value set |
|------|-----------|
| `Int` | -9,223,372,036,854,775,808 through 9,223,372,036,854,775,807 inclusive |
| `Nat` | all non-negative mathematical integers |
| `CInt` | -2,147,483,648 through 2,147,483,647 inclusive |
| `CSize` | 0 through 18,446,744,073,709,551,615 inclusive |

`Char` is a target-independent scalar value type whose exact value set is the set of Unicode scalar values: U+0000 through
U+10FFFF inclusive, excluding the surrogate code points U+D800 through U+DFFF. No other value is representable as `Char`.

`Byte` is a target-independent integer value type whose exact value set is 0 through 255 inclusive. The NUL byte is the
`Byte` value 0.

Every `Text`, `Bytes`, `List<T>`, `NonEmptyList<T>`, `Map<K, V>`, and `NonEmptyMap<K, V>` value admitted into Evident
MUST have finite cardinality. Every admitted `Text` value MUST also have a finite UTF-8 encoding byte count. Every valid
`CString` value admitted into Evident MUST have a finite payload byte count. Because `Nat` is unbounded, these invariants
make the compiler-owned length and count operations total and exact. A boundary or hazard adapter MUST reject or otherwise
keep outside the strict Evident surface any foreign sequence, C string, or runtime collection whose cardinality or required
text encoding byte count is not finite.

A number literal is a non-negative decimal integer literal. A number literal has type `Int` by default. In an expected-type
position, a number literal may instead type as `Int`, `Nat`, or `Byte` only when its mathematical value is exactly
representable by the expected type. Inside a `boundary` or `hazard` module, a number literal may also type directly as
`CInt` or `CSize` in an expected-type position only when its mathematical value is exactly representable by the expected
foreign ABI type. A number literal whose value is not representable by its selected type MUST be rejected. A number
literal MUST NOT type directly as `CInt` or `CSize` outside a `boundary` or `hazard` module. Non-literal values that need
a foreign ABI integer type MUST use the explicit conversion operations in Section 9.9.

A float literal is a non-negative decimal literal that contains a fractional part, a decimal exponent, or both, such as
`3.14`, `1.0`, or `6.022e23`. A float literal has type `Float`. It denotes the nearest finite IEEE 754 binary64 value
under roundTiesToEven. A float literal whose magnitude would round to positive or negative infinity MUST be rejected; a
nonzero float literal that rounds to zero is admitted as `0`. An integer number literal does not type as `Float`, and a
float literal does not type as an integer or foreign ABI type.

A number literal typed directly as `CInt` or `CSize` is raw ABI structure, not domain or protocol meaning. Such a literal
MUST NOT be used as a foreign-egress value. A `CInt` or `CSize` value passed to a bodyless `foreign fn` MUST be prepared in
the immediate foreign-call body from an egress source carrier under Section 10.5.

String literal expressions have type `Text` by default. In an expected-type position, a string literal may instead type
as `Text`, `Bytes`, or `CString`. A string literal that contains at least one Unicode scalar value may also type as
`NonEmptyText` in a `NonEmptyText`-expected position, and a string literal whose UTF-8 encoding contains at least one byte
may also type as `NonEmptyBytes` in a `NonEmptyBytes`-expected position. An empty string literal MUST NOT type as
`NonEmptyText` or `NonEmptyBytes`. This rule does not create implicit conversions for non-literal values.

When a string literal types as `Bytes` or `NonEmptyBytes`, the byte sequence is exactly the UTF-8 encoding of the
literal's Unicode scalar sequence, with no trailing terminator. When a string literal types as `CString`, the foreign
C-string representation is the UTF-8 encoding of the literal's Unicode scalar sequence followed by exactly one trailing
NUL byte. A string literal containing U+0000 MUST NOT type as `CString`.

A string literal typed directly as `CString` is raw ABI structure, not domain or protocol meaning. Such a literal MUST NOT
be used as a foreign-egress value. A `CString` value passed to a bodyless `foreign fn` MUST be prepared in the immediate
foreign-call body from an egress source carrier under Section 10.5.

`Unit` is the empty success type.

`Never` is the diverging type of expressions that do not continue, such as `fail`.

There is no built-in `Bool`, no built-in optional type, and no nullable reference type in the core language.

### 9.1 Collection categories and invariants

The compiler-owned collection families are semantically blind data plumbing. They are not user-defined generic
declarations, and they do not permit user code to declare generic `state`, `reason`, `proof`, `permit`, or `phase`
types.

The collection families have these meanings:

* `List<T>` is a finite ordered sequence of zero or more `T` values.
* `NonEmptyList<T>` is a finite ordered sequence of one or more `T` values.
* `Map<K, V>` is a finite set of key bindings from `K` to `V` with at most one binding for each key.
* `NonEmptyMap<K, V>` is a finite map with one or more bindings.

A collection type argument MUST be a data type. A `reason` or `permit` type MUST NOT be used as a collection type
argument.

A collection instantiated with an affine-bearing element or value type is affine-bearing. A collection operation MUST
NOT copy an affine-bearing element, value, entry, or collection. An operation that extracts an affine-bearing payload
from a collection MUST consume the containing collection and move the payload into the result.

Empty `List<T>` and empty `Map<K, V>` values are ordinary structural collection values. They MUST NOT be used in public
domain surfaces as sentinel encodings for failure, denial, not-yet-initialized state, or domain absence. A public domain
surface that requires at least one element or binding MUST use `NonEmptyList<T>` or `NonEmptyMap<K, V>` rather than a
weaker collection plus a convention.

Collection operations MUST NOT expose hidden fallback values, hidden insertion or merge policies, nullable results,
optional results, boolean existence checks, sentinel results, or provider-chosen conflict behavior. An operation whose
result depends on emptiness, key absence, key collision, or duplicate entries MUST do at least one of the following:

* take a stronger input type that rules out the condition,
* fail with an explicit compiler-owned collection `reason`, or
* name the caller-selected collision or duplicate-entry consequence in the operation name.

### 9.2 Map key types

`Map<K, V>` and `NonEmptyMap<K, V>` require a compiler-defined canonical key order. In this draft, a map key type `K`
MUST be exactly one of:

* `Int`
* `Nat`
* `Byte`
* `Float`
* `Char`
* `Text`
* `Bytes`

`CString`, `NonEmptyText`, `NonEmptyBytes`, records, states, proofs, concrete phase types, collection types, and contract
types MUST NOT be map key types.

The canonical order is numeric order for `Int`, `Nat`, `Byte`, and `Float`; Unicode scalar order for `Char`; lexicographic
Unicode scalar order for `Text`; and lexicographic byte order for `Bytes`. A map operation that exposes entries as a
list MUST expose them in canonical key order.

User-defined key admission, custom ordering, and locale-sensitive collation are **[Deferred]**.

### 9.3 Compiler-owned collection companions

A conforming implementation MUST provide the following compiler-owned structural companion types and reasons. These
types are part of the language surface, not declarations that user code may redefine.

```evd
public reason ListCardinalityFailure {
    ListHadNoElements,
}

public reason MapCardinalityFailure {
    MapHadNoEntries,
}

public reason MapBindingFailure {
    RequestedKeyHadNoBinding,
    RequestedKeyAlreadyHadBinding,
}

public reason MapMergeFailure {
    InputsHadSharedKey,
}

public record ListFirstAndRest<T> {
    public first: T,
    public rest: List<T>,
}

public record MapEntry<K, V> {
    public key: K,
    public value: V,
}

public record MapFirstEntryAndRest<K, V> {
    public first: MapEntry<K, V>,
    public rest: Map<K, V>,
}

public record MapBoundValueAndRest<K, V> {
    public value: V,
    public rest: Map<K, V>,
}
```

The generic companion records above are compiler-owned structural records. They do not authorize user-defined generic
records outside `foundation` modules and they MUST NOT be used to encode domain meaning in their type parameters.
Every use of `ListFirstAndRest<T>` MUST use a data type for `T`. Every use of `MapEntry<K, V>`,
`MapFirstEntryAndRest<K, V>`, or `MapBoundValueAndRest<K, V>` MUST use a permitted map key type for `K` and a data type
for `V`.

The public labels inside these companion records are structural collection labels. They are reserved for the
compiler-owned collection surface and do not weaken the public modeling rules in Section 10 for user-authored domain,
boundary, foundation, or hazard APIs.

### 9.4 Collection operation availability

The operation signatures in this section are compiler-owned generic functions. Calls to these operations MUST write all
type arguments explicitly. Signature notes such as `[T copyable]` and `[V copyable]` are static availability conditions,
not source syntax.

The compiler-owned companion type names, reason names, reason variant names, and operation names in this section are
reserved. User code MUST NOT redeclare them.

An operation whose name ends in `_copy` MUST be available only when every payload that would be duplicated by the
operation is copyable. Instantiating a `_copy` operation with an affine-bearing payload type is invalid.

An operation whose name contains `_consume_` does not require payload copying. For copyable instantiations, ordinary
value-copy semantics preserve the caller's binding. For affine-bearing instantiations, passing the collection moves the
caller binding into the operation and returned payloads are moved into the result.

A map operation that would remove, replace, or discard an existing value without returning that value MUST be available
only when the map value type is copyable. Code that removes, replaces, or resolves collisions for affine-bearing map
values MUST use an explicit consume operation that moves the affected value into the result, or a rejecting operation
that fails rather than selecting a hidden policy.

The compiler-owned collection surface is closed for this draft. A conforming implementation MUST NOT expose additional
core collection operations whose behavior is equivalent to optional lookup, boolean containment, fallback lookup,
sentinel extraction, provider-selected insertion policy, provider-selected merge policy, or implicit duplicate-key
resolution.

### 9.5 List operations

A conforming implementation MUST provide these list construction and cardinality operations:

```evd
list_empty<T>() -> List<T>

list_single<T>(value: T) -> NonEmptyList<T>

list_prepend<T>(value: T, values: List<T>) -> NonEmptyList<T>
list_append<T>(values: List<T>, value: T) -> NonEmptyList<T>

list_concat<T>(left: List<T>, right: List<T>) -> List<T>
nonempty_list_concat_left<T>(left: NonEmptyList<T>, right: List<T>) -> NonEmptyList<T>
nonempty_list_concat_right<T>(left: List<T>, right: NonEmptyList<T>) -> NonEmptyList<T>
nonempty_list_concat<T>(left: NonEmptyList<T>, right: NonEmptyList<T>) -> NonEmptyList<T>

list_require_nonempty<T>(values: List<T>) -> NonEmptyList<T>
    fails ListCardinalityFailure

nonempty_list_widen<T>(values: NonEmptyList<T>) -> List<T>
```

`list_require_nonempty` MUST fail with `ListCardinalityFailure::ListHadNoElements` when `values` has no elements. It
MUST NOT invent an element, return a placeholder element, or choose a fallback value.

`list_prepend` MUST place `value` before every element of `values`. `list_append` MUST place `value` after every
element of `values`. The `list_concat` operations MUST preserve all elements from `left` in their original order,
followed by all elements from `right` in their original order.

A conforming implementation MUST provide these list observation and decomposition operations:

```evd
list_count_copy<T>(values: List<T>) -> Nat
    [T copyable]

nonempty_list_count_copy<T>(values: NonEmptyList<T>) -> Nat
    [T copyable]

nonempty_list_first_copy<T>(values: NonEmptyList<T>) -> T
    [T copyable]

nonempty_list_consume_first<T>(values: NonEmptyList<T>) -> ListFirstAndRest<T>
```

Operations that require a first element MUST take `NonEmptyList<T>`. There is no core `first` operation on `List<T>`.
Code with only `List<T>` evidence MUST first call `list_require_nonempty` and handle `ListCardinalityFailure`.
`nonempty_list_first_copy` and `nonempty_list_consume_first` operate on the first element in list order. The `rest`
field of `ListFirstAndRest<T>` contains every remaining element in its original order.

### 9.6 Map operations

A conforming implementation MUST provide these map construction and cardinality operations:

```evd
map_empty<K, V>() -> Map<K, V>

map_single<K, V>(key: K, value: V) -> NonEmptyMap<K, V>

map_require_nonempty<K, V>(entries: Map<K, V>) -> NonEmptyMap<K, V>
    fails MapCardinalityFailure

nonempty_map_widen<K, V>(entries: NonEmptyMap<K, V>) -> Map<K, V>
```

`map_require_nonempty` MUST fail with `MapCardinalityFailure::MapHadNoEntries` when `entries` has no bindings.

Map operations produce collection values. They MUST NOT mutate an existing caller-visible collection in place, consult
provider-local insertion or merge policy, or preserve hidden state outside the returned value and explicit failure
reason.

A conforming implementation MUST provide these map binding operations:

```evd
map_bind_new<K, V>(entries: Map<K, V>, key: K, value: V) -> NonEmptyMap<K, V>
    fails MapBindingFailure

nonempty_map_bind_new<K, V>(entries: NonEmptyMap<K, V>, key: K, value: V) -> NonEmptyMap<K, V>
    fails MapBindingFailure

map_replace_bound<K, V>(entries: Map<K, V>, key: K, value: V) -> NonEmptyMap<K, V>
    fails MapBindingFailure
    [V copyable]

nonempty_map_replace_bound<K, V>(entries: NonEmptyMap<K, V>, key: K, value: V) -> NonEmptyMap<K, V>
    fails MapBindingFailure
    [V copyable]

map_bind_or_replace<K, V>(entries: Map<K, V>, key: K, value: V) -> NonEmptyMap<K, V>
    [V copyable]

nonempty_map_bind_or_replace<K, V>(entries: NonEmptyMap<K, V>, key: K, value: V) -> NonEmptyMap<K, V>
    [V copyable]

map_remove_bound<K, V>(entries: Map<K, V>, key: K) -> Map<K, V>
    fails MapBindingFailure
    [V copyable]

nonempty_map_remove_bound<K, V>(entries: NonEmptyMap<K, V>, key: K) -> Map<K, V>
    fails MapBindingFailure
    [V copyable]

map_consume_bound_value<K, V>(entries: Map<K, V>, key: K) -> MapBoundValueAndRest<K, V>
    fails MapBindingFailure

nonempty_map_consume_bound_value<K, V>(entries: NonEmptyMap<K, V>, key: K) -> MapBoundValueAndRest<K, V>
    fails MapBindingFailure
```

`map_bind_new` and `nonempty_map_bind_new` MUST fail with
`MapBindingFailure::RequestedKeyAlreadyHadBinding` when the key is already bound.

`map_replace_bound`, `nonempty_map_replace_bound`, `map_remove_bound`, `nonempty_map_remove_bound`,
`map_consume_bound_value`, and `nonempty_map_consume_bound_value` MUST fail with
`MapBindingFailure::RequestedKeyHadNoBinding` when the key is not bound.

`map_bind_or_replace` and `nonempty_map_bind_or_replace` encode their collision policy in the function name: a binding
for the requested key is created when absent and replaced when present. They MUST NOT expose whether replacement
occurred as a boolean, optional result, sentinel value, hidden side channel, or provider-specific policy result.

A conforming implementation MUST provide these map observation and entry operations:

```evd
map_count_copy<K, V>(entries: Map<K, V>) -> Nat
    [V copyable]

nonempty_map_count_copy<K, V>(entries: NonEmptyMap<K, V>) -> Nat
    [V copyable]

map_lookup_copy<K, V>(entries: Map<K, V>, key: K) -> V
    fails MapBindingFailure
    [V copyable]

nonempty_map_lookup_copy<K, V>(entries: NonEmptyMap<K, V>, key: K) -> V
    fails MapBindingFailure
    [V copyable]

nonempty_map_first_entry_copy<K, V>(entries: NonEmptyMap<K, V>) -> MapEntry<K, V>
    [V copyable]

nonempty_map_consume_first_entry<K, V>(entries: NonEmptyMap<K, V>) -> MapFirstEntryAndRest<K, V>

map_entries_copy<K, V>(entries: Map<K, V>) -> List<MapEntry<K, V>>
    [V copyable]

nonempty_map_entries_copy<K, V>(entries: NonEmptyMap<K, V>) -> NonEmptyList<MapEntry<K, V>>
    [V copyable]

map_consume_entries<K, V>(entries: Map<K, V>) -> List<MapEntry<K, V>>

nonempty_map_consume_entries<K, V>(entries: NonEmptyMap<K, V>) -> NonEmptyList<MapEntry<K, V>>
```

`map_lookup_copy` and `nonempty_map_lookup_copy` MUST fail with
`MapBindingFailure::RequestedKeyHadNoBinding` when the key is not bound. There is no core map containment function and
no lookup function that returns a boolean, optional, nullable, sentinel, or fallback value.

Entry-list operations MUST use canonical key order. `nonempty_map_first_entry_copy` and
`nonempty_map_consume_first_entry` operate on the entry with the least key in canonical key order. The `rest` field of
`MapFirstEntryAndRest<K, V>` contains every remaining entry.

A conforming implementation MUST provide these map merge operations:

```evd
map_merge_rejecting_shared_keys<K, V>(left: Map<K, V>, right: Map<K, V>) -> Map<K, V>
    fails MapMergeFailure

map_merge_left_nonempty_rejecting_shared_keys<K, V>(left: NonEmptyMap<K, V>, right: Map<K, V>) -> NonEmptyMap<K, V>
    fails MapMergeFailure

map_merge_right_nonempty_rejecting_shared_keys<K, V>(left: Map<K, V>, right: NonEmptyMap<K, V>) -> NonEmptyMap<K, V>
    fails MapMergeFailure

nonempty_map_merge_rejecting_shared_keys<K, V>(left: NonEmptyMap<K, V>, right: NonEmptyMap<K, V>) -> NonEmptyMap<K, V>
    fails MapMergeFailure

map_merge_using_left_bindings_for_shared_keys<K, V>(left: Map<K, V>, right: Map<K, V>) -> Map<K, V>
    [V copyable]

map_merge_left_nonempty_using_left_bindings_for_shared_keys<K, V>(left: NonEmptyMap<K, V>, right: Map<K, V>) -> NonEmptyMap<K, V>
    [V copyable]

map_merge_right_nonempty_using_left_bindings_for_shared_keys<K, V>(left: Map<K, V>, right: NonEmptyMap<K, V>) -> NonEmptyMap<K, V>
    [V copyable]

nonempty_map_merge_using_left_bindings_for_shared_keys<K, V>(left: NonEmptyMap<K, V>, right: NonEmptyMap<K, V>) -> NonEmptyMap<K, V>
    [V copyable]

map_merge_using_right_bindings_for_shared_keys<K, V>(left: Map<K, V>, right: Map<K, V>) -> Map<K, V>
    [V copyable]

map_merge_left_nonempty_using_right_bindings_for_shared_keys<K, V>(left: NonEmptyMap<K, V>, right: Map<K, V>) -> NonEmptyMap<K, V>
    [V copyable]

map_merge_right_nonempty_using_right_bindings_for_shared_keys<K, V>(left: Map<K, V>, right: NonEmptyMap<K, V>) -> NonEmptyMap<K, V>
    [V copyable]

nonempty_map_merge_using_right_bindings_for_shared_keys<K, V>(left: NonEmptyMap<K, V>, right: NonEmptyMap<K, V>) -> NonEmptyMap<K, V>
    [V copyable]
```

The rejecting merge operations MUST fail with `MapMergeFailure::InputsHadSharedKey` if both inputs bind the same key.
The left-binding and right-binding merge operations encode the collision policy in the function name. For shared keys,
the unchosen binding is discarded from the produced map as the explicit consequence of the named operation, which is why
those operations require copyable map values.

A conforming implementation MUST provide these entry-list conversion operations:

```evd
map_from_entries_rejecting_shared_keys<K, V>(entries: List<MapEntry<K, V>>) -> Map<K, V>
    fails MapMergeFailure

nonempty_map_from_entries_rejecting_shared_keys<K, V>(entries: NonEmptyList<MapEntry<K, V>>) -> NonEmptyMap<K, V>
    fails MapMergeFailure

map_from_entries_using_first_bindings<K, V>(entries: List<MapEntry<K, V>>) -> Map<K, V>
    [V copyable]

nonempty_map_from_entries_using_first_bindings<K, V>(entries: NonEmptyList<MapEntry<K, V>>) -> NonEmptyMap<K, V>
    [V copyable]

map_from_entries_using_last_bindings<K, V>(entries: List<MapEntry<K, V>>) -> Map<K, V>
    [V copyable]

nonempty_map_from_entries_using_last_bindings<K, V>(entries: NonEmptyList<MapEntry<K, V>>) -> NonEmptyMap<K, V>
    [V copyable]
```

The rejecting entry-list conversions MUST fail with `MapMergeFailure::InputsHadSharedKey` if the input list contains
more than one entry for the same key. The first-binding and last-binding conversions encode duplicate-key policy in the
function name. No unsuffixed `map_from_entries` operation exists in the core surface.

### 9.7 Collection extensions

Collection literals, comprehensions, higher-order traversal, borrowing iterators, index-based access, slicing, sorting,
and specialized representation controls for the compiler-owned collection families are **[Deferred]**.

`Text` and `Bytes` are not collection families. Their length, element-access, and slicing operations are defined in
Section 9.8 and are not deferred.

### 9.8 Text and Bytes operations

`Char` and `Byte` are scalar built-in data types. `Text` and `Bytes` are raw sequence-valued built-in data types, not
collection families. A `Text` value is a finite ordered sequence of Unicode scalar values, each of which is a `Char`. A
`Bytes` value is a finite ordered sequence of bytes, each of which is a `Byte`. `Text`, `Bytes`, `Char`, and `Byte` are
copyable data types.

`NonEmptyText` is a `Text` value that has one or more Unicode scalar values, and `NonEmptyBytes` is a `Bytes` value that
has one or more bytes. `NonEmptyText` and `NonEmptyBytes` are copyable data types and may appear in any ordinary data
position, including record fields, parameters, returns, and collection element types. They are not collection families,
they take no type arguments, and they MUST NOT be used as map key types; code that needs a text or bytes map key MUST
widen to `Text` or `Bytes` first. Such widened text or bytes maps are valid only for private, adapter-local, or otherwise
non-domain-facing surfaces under the raw-substrate restrictions in Section 10.6. An exported domain or foundation API MUST
NOT expose `Map<Text, V>`, `Map<Bytes, V>`, `NonEmptyMap<Text, V>`, or `NonEmptyMap<Bytes, V>` as a substitute for a
domain key. Until user-defined key admission exists, domain-facing textual or byte associations MUST use a
consequence-carrying binding record in `List` or `NonEmptyList`, a consequence-carrying private-representation domain type,
or remain private. A public domain surface that requires non-empty content MUST use `NonEmptyText`, `NonEmptyBytes`, or a
consequence-carrying domain type built from them rather than raw `Text` or `Bytes` plus a convention. Section 10.6 defines the
public-domain-surface restrictions for raw and foreign substrate types.

The operations in this section are compiler-owned monomorphic functions. They take no type arguments. Positions in a
`Text` value are zero-based and counted in Unicode scalar values; positions in a `Bytes` value are zero-based and counted
in bytes. A `Text` position and a `Text` slice boundary therefore always fall on a Unicode scalar boundary, and no
operation in this section exposes the underlying byte encoding of a `Text` value.

The compiler-owned companion reason names, reason variant names, and operation names in this section are reserved. User
code MUST NOT redeclare them.

A conforming implementation MUST provide these compiler-owned bounds reasons:

```evd
public reason TextBoundsFailure {
    RequestedCharacterIndexOutOfRange,
    RequestedTextSliceOutOfRange,
}

public reason BytesBoundsFailure {
    RequestedByteIndexOutOfRange,
    RequestedBytesSliceOutOfRange,
}
```

A conforming implementation MUST provide these length operations:

```evd
text_length(text: Text) -> Nat

bytes_length(bytes: Bytes) -> Nat
```

`text_length` MUST return the number of Unicode scalar values in `text`. `bytes_length` MUST return the number of bytes
in `bytes`. Both operations are total: they MUST NOT fail, and they MUST NOT expose emptiness as a boolean, optional, or
sentinel result.

A conforming implementation MUST provide these element-access operations:

```evd
text_character_at(text: Text, index: Nat) -> Char
    fails TextBoundsFailure

bytes_byte_at(bytes: Bytes, index: Nat) -> Byte
    fails BytesBoundsFailure
```

`text_character_at` MUST return the Unicode scalar value at the zero-based scalar position `index` in `text`, and MUST
fail with `TextBoundsFailure::RequestedCharacterIndexOutOfRange` when `index` is not less than `text_length(text)`.
`bytes_byte_at` MUST return the byte at the zero-based byte position `index` in `bytes`, and MUST fail with
`BytesBoundsFailure::RequestedByteIndexOutOfRange` when `index` is not less than `bytes_length(bytes)`. Each operation
MUST NOT invent a character or byte, return a placeholder, or choose a fallback value for an out-of-range position.

A conforming implementation MUST provide these slice operations:

```evd
text_slice(text: Text, start: Nat, end: Nat) -> Text
    fails TextBoundsFailure

bytes_slice(bytes: Bytes, start: Nat, end: Nat) -> Bytes
    fails BytesBoundsFailure
```

Each slice operation selects the half-open range from `start` inclusive to `end` exclusive. `text_slice` MUST return the
`Text` value containing exactly the Unicode scalar values at positions `start` through `end` minus one in their original
order, and MUST return an empty `Text` value when `start` equals `end`. `bytes_slice` MUST return the `Bytes` value
containing exactly the bytes at positions `start` through `end` minus one in their original order, and MUST return an
empty `Bytes` value when `start` equals `end`. `text_slice` MUST fail with
`TextBoundsFailure::RequestedTextSliceOutOfRange`, and `bytes_slice` MUST fail with
`BytesBoundsFailure::RequestedBytesSliceOutOfRange`, unless `start` is less than or equal to `end` and `end` is less than
or equal to the length of the input. A slice operation MUST NOT clamp an out-of-range boundary, truncate the requested
range, or otherwise adjust `start` or `end` to a value the caller did not request.

A conforming implementation MUST provide these compiler-owned cardinality reasons:

```evd
public reason TextCardinalityFailure {
    TextHadNoCharacters,
}

public reason BytesCardinalityFailure {
    BytesHadNoBytes,
}
```

A conforming implementation MUST provide these non-empty construction and widening operations:

```evd
text_require_nonempty(text: Text) -> NonEmptyText
    fails TextCardinalityFailure

nonempty_text_widen(text: NonEmptyText) -> Text

bytes_require_nonempty(bytes: Bytes) -> NonEmptyBytes
    fails BytesCardinalityFailure

nonempty_bytes_widen(bytes: NonEmptyBytes) -> Bytes
```

`text_require_nonempty` MUST fail with `TextCardinalityFailure::TextHadNoCharacters` when `text` has no Unicode scalar
values, and otherwise MUST return the same scalar sequence in its original order as a `NonEmptyText`.
`bytes_require_nonempty` MUST fail with `BytesCardinalityFailure::BytesHadNoBytes` when `bytes` has no bytes, and
otherwise MUST return the same byte sequence in its original order as a `NonEmptyBytes`. Each operation MUST NOT invent,
drop, or reorder a scalar or byte. `nonempty_text_widen` and `nonempty_bytes_widen` MUST return the same sequence typed
as `Text` and `Bytes` respectively.

A conforming implementation MUST provide these non-empty observation operations:

```evd
nonempty_text_length(text: NonEmptyText) -> Nat

nonempty_text_first_character(text: NonEmptyText) -> Char

nonempty_bytes_length(bytes: NonEmptyBytes) -> Nat

nonempty_bytes_first_byte(bytes: NonEmptyBytes) -> Byte
```

`nonempty_text_length` MUST return the number of Unicode scalar values in `text`, which is always one or more.
`nonempty_bytes_length` MUST return the number of bytes in `bytes`, which is always one or more.
`nonempty_text_first_character` MUST return the Unicode scalar value at scalar position zero, and
`nonempty_bytes_first_byte` MUST return the byte at byte position zero. Both first-element operations are total: they MUST
NOT fail. Element access at an arbitrary index and slicing are defined only on `Text` and `Bytes`; code holding a
`NonEmptyText` or `NonEmptyBytes` value MUST widen with `nonempty_text_widen` or `nonempty_bytes_widen` before calling
`text_character_at`, `bytes_byte_at`, `text_slice`, or `bytes_slice`. A slice of a `NonEmptyText` or `NonEmptyBytes`
value can be empty, so slicing intentionally yields the widened `Text` or `Bytes` type rather than a non-empty type.

The compiler-owned `TextBoundsFailure`, `BytesBoundsFailure`, `TextCardinalityFailure`, and `BytesCardinalityFailure`
reason types are substrate-local. Direct calls to the compiler-owned operations in this section MAY handle those reasons
inside private collapse logic. A user-authored exported non-raw API MUST translate those reasons into a same-module
caller-facing `reason` before export; exposing one of these reason types in a user-authored exported API
surface makes that declaration raw-adapter-exposing under Section 10.6.

Text and bytes structural operations prove only sequence well-formedness, bounds, cardinality, and exact extraction. They
are not, by themselves, semantic collapse for stringly discriminators, byte discriminators, status bytes, protocol tags,
schema sentinels, handle encodings, policy flags, or other runtime or foreign meanings carried by the source sequence.
Derived primitive and sequence values remain subject to the ingress-derived looseness rule in Section 10.5.

### 9.9 Foreign ABI conversion operations

The operations in this section are compiler-owned monomorphic functions. They take no type arguments. A call to one of
these operations MUST appear only in a `boundary` or `hazard` module.

The compiler-owned companion reason names, reason variant names, and operation names in this section are reserved. User
code MUST NOT redeclare them.

The compiler-owned foreign ABI conversion reason types in this section are adapter-local. User-authored code MAY mention
them only inside `boundary` and `hazard` modules. Boundary and hazard code that exposes a non-raw-adapter API MUST
translate these reasons into a caller-facing `reason` before export.

A conforming implementation MUST provide these compiler-owned foreign ABI conversion reasons:

```evd
public reason ForeignAbiTextFailure {
    CStringPayloadWasNotUtf8,
    CStringPayloadContainedNul,
}

public reason ForeignAbiIntegerFailure {
    ForeignIntegerExceededIntRange,
    ForeignIntegerWasNegative,
    CoreIntegerExceededCIntRange,
    CoreIntegerWasNegative,
    CoreIntegerExceededCSizeRange,
}
```

A conforming implementation MUST provide these `CString` conversion operations:

```evd
cstring_payload_bytes(value: CString) -> Bytes

cstring_payload_text(value: CString) -> Text
    fails ForeignAbiTextFailure

text_to_cstring(text: Text) -> CString
    fails ForeignAbiTextFailure

bytes_to_cstring(bytes: Bytes) -> CString
    fails ForeignAbiTextFailure
```

`cstring_payload_bytes` MUST return exactly the payload bytes before the first terminating NUL byte. It MUST NOT include
the terminator or bytes after the terminator. `cstring_payload_text` MUST fail with
`ForeignAbiTextFailure::CStringPayloadWasNotUtf8` unless those payload bytes form well-formed UTF-8, and otherwise MUST
return the decoded Unicode scalar sequence as `Text`.

`text_to_cstring` MUST fail with `ForeignAbiTextFailure::CStringPayloadContainedNul` when `text` contains U+0000, and
otherwise MUST return a non-null `CString` whose payload is exactly the UTF-8 encoding of the input scalar sequence and
whose representation has exactly one trailing NUL byte. `bytes_to_cstring` MUST fail with
`ForeignAbiTextFailure::CStringPayloadContainedNul` when `bytes` contains the NUL byte, and otherwise MUST return a
non-null `CString` whose payload is exactly the input byte sequence and whose representation has exactly one trailing NUL
byte. These operations MUST NOT silently truncate at an interior NUL, choose a replacement character, apply locale
conversion, or invent fallback bytes.

`CString` values produced by `text_to_cstring` and `bytes_to_cstring` are implementation-owned ABI values. A conforming
implementation MUST keep such a value valid, read-only, and payload-stable for every `foreign fn` call that receives it
during the value's lifetime in Evident. A foreign API that stores a C string pointer beyond the call that receives it,
mutates through the pointer, treats it as an in/out buffer, or requires caller-managed release is a runtime protocol
hazard and MUST be wrapped outside the current strict foreign ABI surface or by a future separately specified hazard
primitive before it reaches ordinary domain code.

A conforming implementation MUST provide these integer conversion operations:

```evd
cint_to_int(value: CInt) -> Int
    fails ForeignAbiIntegerFailure

cint_require_nat(value: CInt) -> Nat
    fails ForeignAbiIntegerFailure

csize_to_int(value: CSize) -> Int
    fails ForeignAbiIntegerFailure

csize_to_nat(value: CSize) -> Nat

int_to_cint(value: Int) -> CInt
    fails ForeignAbiIntegerFailure

nat_to_cint(value: Nat) -> CInt
    fails ForeignAbiIntegerFailure

int_to_csize(value: Int) -> CSize
    fails ForeignAbiIntegerFailure

nat_to_csize(value: Nat) -> CSize
    fails ForeignAbiIntegerFailure
```

Each integer conversion MUST preserve the exact numeric value. Foreign-to-core conversions reject foreign values that
cannot be represented exactly by the requested core type. `cint_to_int` and `csize_to_int` MUST fail with
`ForeignAbiIntegerFailure::ForeignIntegerExceededIntRange` when the foreign value cannot be represented exactly as
`Int`. `cint_require_nat` MUST fail with `ForeignAbiIntegerFailure::ForeignIntegerWasNegative` when the `CInt` value is
negative, and otherwise MUST return the exact non-negative numeric value as `Nat`. `csize_to_nat` MUST return the exact
numeric value as `Nat`.

Core-to-foreign conversions reject core values that cannot be represented exactly by the requested target ABI type.
`int_to_cint` and `nat_to_cint` MUST fail with `ForeignAbiIntegerFailure::CoreIntegerExceededCIntRange` when the core
value cannot be represented exactly as `CInt`. `int_to_csize` MUST fail with
`ForeignAbiIntegerFailure::CoreIntegerWasNegative` when the `Int` value is negative, and MUST fail with
`ForeignAbiIntegerFailure::CoreIntegerExceededCSizeRange` when a non-negative `Int` value cannot be represented exactly
as `CSize`. `nat_to_csize` MUST fail with `ForeignAbiIntegerFailure::CoreIntegerExceededCSizeRange` when the `Nat` value
cannot be represented exactly as `CSize`. No integer conversion may wrap, saturate, truncate, reinterpret bits, or choose
a default numeric value.

Foreign ABI integer conversion proves only numeric representability. It is not, by itself, semantic collapse for
integerly typed enums, status codes, invalid-handle encodings, sentinels, policy flags, protocol discriminators, or other
runtime or foreign meanings carried by the source integer. Converted core integers remain subject to the ingress-derived
looseness rule in Section 10.5.

## 10. Modeling Rules and Boundary Discipline

Modeled surfaces are governed by the closed, compiler-enforced rules in this section.

### 10.1 Consequence-first naming

Consequence-first naming is enforced by the closed reserved modeled-name set and single-character rejection in Section
10.7. A user-authored declaration-surface name MUST NOT be drawn from that reserved set and MUST NOT be a single
character.

The compiler MUST NOT infer additional declaration-surface-name rejections from unstated naming intent outside the
reserved set in this draft.

### 10.2 No pseudo-optionals

A `state` MUST NOT be a pseudo-optional shape.

A `state` is a pseudo-optional shape when either of these closed mechanical conditions holds:

* any variant name matches the Section 10.7 presence-or-absence reserved names; or
* the `state` has exactly two variants, exactly one variant has one or more payload fields, the other variant has no
  payload fields, and the payload-free variant's normalized word sequence is the payload-bearing variant's normalized word
  sequence prefixed by `not` or `no` under the identifier word-normalization algorithm in Section 10.6.

This closed rule rejects shapes such as `Present { ... } | Absent`, `Provided { ... } | NotProvided`, and
`Item { ... } | NoItem`. It does not authorize compiler rejection of arbitrary domain alternatives outside this rule. If
the domain truly has alternatives, its variant names MUST avoid this closed pseudo-optional shape.

### 10.3 No wildcard bypass variants

Configuration, policy, and authority models MUST NOT include a universal escape-hatch variant whose name is in the
Section 10.7 wildcard bypass reserved set:

* `Any`
* `All`
* `AllowAll`
* `Unrestricted`

Policy models whose declaration-surface names use the reserved wildcard bypass names in Section 10.7 are invalid. Explicit finite
alternatives and explicit merge rules remain the required strict-surface pattern for policies in this draft.

### 10.4 No sentinel values

Declaration-surface names in the Section 10.7 placeholder or sentinel reserved set MUST NOT appear in user-authored
modeled surfaces regardless of visibility. Raw empty text or bytes, raw foreign ABI values, and raw literals used as
sentinel facts are governed by the substrate and egress rules in Sections 9, 10.5, and 10.6.

Design patterns that the closed reservations and raw-substrate rules are intended to block include zero IDs used as "not
assigned", empty text used as "missing", all-zero handles used as "not opened", and equivalent in-band markers.

### 10.5 Exact boundary collapse

Foreign ambiguity and runtime looseness MUST collapse in the exact boundary function that first receives it. This rule
applies equally to boundary-facing and hazard-facing ingress.

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
* status-code sentinels
* invalid, zero, or closed handles
* partial runtime states
* callback-order uncertainty
* resource lifetime uncertainty

Such ambiguity or looseness MUST NOT be stored, forwarded, or bound to locals whose scope outlives the immediate collapse
logic.

The boundary or hazard ingress either:

* rejects the input with an explicit `reason`, or
* translates it into a fully inhabited internal form.

The parser, boundary adapter, or hazard adapter MUST NOT silently invent domain values.

A non-raw-adapter `hazard` export MUST expose the result of runtime interaction as a consequence-carrying handle, proof,
phase, state, reason, or scoped permit grant after collapse. It MUST NOT expose raw status codes, invalid handles,
callback ordering facts, partially initialized runtime objects, unstable resource lifetimes, or provider-selected fallback
policy as ordinary domain facts.

A bodyless `foreign fn` is not a collapse point. An exported body-bearing raw-adapter declaration MUST be the exact
collapse point for its own raw ingress; otherwise it is an invalid raw relay export.

An exported body-bearing raw-adapter declaration satisfies the exact-collapse requirement only when all of the following
hold:

* its exported result type and contract clause types are not raw-adapter-exposing,
* every raw-adapter-exposing ordinary parameter is rejected or translated to a non-raw inhabited value in that same body,
  either directly or through a same-module private ingress-validating helper as defined below, and
* the body does not store, return, or relay a raw-adapter-exposing value except to prepare an already inhabited internal
  value for immediate foreign egress.

A body-bearing declaration whose API surface contains raw-adapter-exposing type expressions and that does not satisfy the
exported body-bearing collapse-point requirements above MUST be private same-module adapter plumbing. It MUST NOT be
exported.

For any bodyless raw-adapter declaration or private same-module ingress-validating helper whose API surface contains
raw-adapter-exposing type expressions, the exact boundary-collapse obligation attaches to the first body-bearing
`boundary` or `hazard` declaration that receives a raw-adapter-exposing value from the foreign side, calls that declaration
or helper, receives its result, constructs or reads a raw-adapter-exposing value, or prepares a raw value for foreign
egress from an internal value.

That first body-bearing declaration MAY reference a bodyless raw-adapter declaration or private same-module
ingress-validating helper whose API surface contains raw-adapter-exposing type expressions only as part of its own
immediate ingress/collapse logic. A raw or foreign substrate value obtained from a bodyless raw-adapter declaration MUST be
rejected or translated in that same declaration before it is stored, returned to a non-raw-adapter API, or passed to a
helper that does not itself reject or translate it. Boundary and hazard code MUST NOT relay raw-adapter-exposing values
through adapter layers as a substitute for constructing an inhabited internal value.

A foreign-egress value is any `CString`, `CInt`, or `CSize` value passed as an argument to a bodyless `foreign fn`. A
foreign-egress value MUST be produced explicitly from an egress source carrier in the same body-bearing declaration that
immediately passes it to that `foreign fn`. It MUST NOT be returned from, produced by, or passed through a private helper.
It MUST NOT be stored, returned, exported, passed to a non-foreign callee, or passed to another helper before that
immediate foreign call.

An egress source carrier is either an egress consequence carrier or an egress quantity carrier.

An egress consequence carrier is a value whose type is a consequence-carrying data type under Section 10.6, including the
result of a permit-authorized operation only when that result type is itself consequence-carrying.

When the selected external representation is not obtained by reading an `Int`, `Nat`, `Text`, `Bytes`, `NonEmptyText`, or
`NonEmptyBytes` value that independently qualifies as an egress quantity carrier, the immediate foreign-call body MUST use
an explicit consequence-to-ABI mapping from the egress consequence carrier before preparing the foreign-egress value. A
consequence-to-ABI mapping is a same-body mapping whose subject is the egress consequence carrier and whose alternatives
are source-visible:

* for a `state`, every variant MUST be named explicitly;
* for a concrete `phase`, every admitted position that can reach the call MUST be named explicitly;
* for a proof, record, or compiler-owned companion record, every value path used by the mapping MUST be an
  always-present field path in the definite consequence containment rule from Section 10.6 or an egress representation
  extraction path defined below; and
* for a nested consequence carrier, the mapping MUST continue until the selected representation is a concrete `Int`,
  `Nat`, `Text`, `Bytes`, `NonEmptyText`, or `NonEmptyBytes` value passed to a compiler-owned conversion, or a raw ABI
  egress terminal passed directly to the immediate `foreign fn`.

A consequence-to-ABI mapping MUST be written in the same body-bearing declaration that immediately calls the `foreign fn`.
It MUST NOT use a wildcard arm, default arm, helper call, storage lookup, hidden default, private constant, parser-invented
value, or adapter-local raw value to choose the external representation. A literal written directly in a named mapping arm
is permitted only as the selected representation of that named consequence; the literal is not an egress source carrier
outside that mapping. The mapping result is an egress quantity carrier only for the immediate compiler-owned conversion and
foreign call in that same body.

An egress representation extraction is a same-body read from an already inhabited egress consequence carrier to one
always-present representation terminal. Its root MUST be the egress consequence carrier that the body is mapping, and the
root MUST NOT be a private constant, zero-argument helper result, storage lookup, raw-adapter result, adapter-local raw
value, parser-invented default, or hidden literal. The extraction path itself MUST NOT contain a helper call. The
extraction path follows only representation edges
that are guaranteed to exist in every value of the selected consequence: required public or private record fields, proof
fields, concrete phase fields, always-present compiler-owned companion record fields, and explicitly instantiated generic
record fields after substitution when those fields are required. It follows a `state` payload only inside the explicit
same-body exhaustive mapping arm for the variant that owns that payload. It does not follow empty-capable `List` or `Map`
payloads, and it does not select an arbitrary element from a collection.

An egress representation extraction terminal MUST be exactly one of these:

* a core egress terminal: `Int`, `Nat`, `Text`, `Bytes`, `NonEmptyText`, or `NonEmptyBytes`; or
* a nested consequence-carrying data type, which MUST be mapped again in the same body until it reaches a core egress
  terminal.

A core egress terminal becomes an egress quantity carrier only for the immediate compiler-owned conversion and foreign call
in that same body. A raw ABI substrate value (`CString`, `CInt`, or `CSize`) MUST NOT be extracted from private
representation and passed as proof of a domain fact. Foreign egress is prepared explicitly from a core egress terminal in
the same body-bearing declaration that immediately calls the bodyless `foreign fn`.

An egress quantity carrier is a value whose type is exactly `Int`, `Nat`, `Text`, `Bytes`, `NonEmptyText`, or
`NonEmptyBytes`, and whose value is one of:

* an ordinary parameter of the same body-bearing declaration that immediately calls the `foreign fn`, when that parameter's
  declared type is not raw-adapter-exposing and the value is not ingress-derived looseness,
* a field, payload field, proof field, phase field, collection element, map key, map value, or compiler-owned companion
  record field structurally reached from such an ordinary parameter, when the reached field type is not
  raw-adapter-exposing and the reached value is not ingress-derived looseness,
* the result of a compiler-owned structural operation from Section 9.8 applied in the same body to an egress quantity
  carrier, or
* the result of the explicit consequence-to-ABI mapping above.

An ordinary parameter whose declared type is exactly `Text`, `Bytes`, `CString`, `CInt`, or `CSize`, or otherwise
raw-adapter-exposing, is not an egress quantity carrier. `Text` and `Bytes` values can qualify only when they are derived
in the same body from a non-raw egress quantity carrier by compiler-owned structural operations from Section 9.8 or by an
explicit consequence-to-ABI mapping from an egress consequence carrier.

A primitive or sequence ordinary parameter, reached field, payload field, proof field, phase field, collection element,
map key, map value, or compiler-owned companion record field whose own declaration-surface name, enclosing nominal
declaration name, or enclosing body-bearing declaration name contains a semantic-claim marker from Section 10.6 is not an
egress quantity carrier. It MUST first be represented by a consequence-carrying data type and then prepared through the
same-body consequence-to-ABI rule above.

A primitive or sequence value is permit-derived when it is the direct result of a permit-authorized operation, or when it
is copied, moved, returned, passed as an argument, stored in a field, inserted into a collection, extracted from a field or
collection, or relayed through a helper from such a result. A permit-derived primitive or sequence value remains
permit-derived until it is translated into a consequence-carrying data type by an exported producer that satisfies the
visible-consequence rules in Section 10.6. Storing it in a transparent primitive/sequence record does not clear this
provenance. A permit-derived primitive or sequence value is not an egress quantity carrier and MUST NOT be used as an
egress source carrier.

A primitive or sequence literal, private constant, zero-argument helper result, private raw value, private core primitive or
sequence value, raw-adapter value, or primitive or sequence result from a permit-authorized operation is not an egress
source carrier. It MUST NOT satisfy the foreign-egress preparation requirement. Every `CString`, `CInt`, or `CSize` value
passed to a bodyless `foreign fn` MUST be prepared in the immediate foreign-call body either by a compiler-owned conversion
from an egress source carrier whose core type matches the conversion's input type, or by the raw ABI egress-terminal
extraction rule above.

The immediate-collapse obligation above applies when a body-bearing `boundary` or `hazard` declaration receives a
raw-adapter-exposing value at foreign or runtime ingress, calls a bodyless raw-adapter declaration, calls an exported
body-bearing raw-adapter collapse point, calls a private ingress-validating helper, receives a raw-adapter-exposing result
from a bodyless raw-adapter declaration, constructs or reads a raw-adapter-exposing value as part of boundary or hazard
ingress, or prepares a raw value for foreign egress. Such a
body-bearing declaration MUST do one of:

* reject or translate that value to a non-raw inhabited result in that same declaration,
* call a same-module private ingress-validating helper directly as part of that declaration's immediate collapse logic,
  where the helper rejects or translates the value before returning to the caller, or
* prepare that value explicitly from an already inhabited internal value for immediate foreign egress in the same
  body-bearing declaration.

A same-module private ingress-validating helper used in this way MUST NOT store, return, or relay a raw-adapter-exposing
value. It may return only the collapsed non-raw result or explicit failure to its direct caller. Any body-bearing caller of
a bodyless exported raw-adapter declaration remains responsible for immediate collapse before a non-raw API can observe the
result.

A core primitive or sequence value is ingress-derived looseness when it is derived by length, element access, slicing,
cardinality collapse, decoding, foreign ABI integer conversion, or any other primitive/sequence extraction from raw
`Text`, raw `Bytes`, `CString`, `CInt`, `CSize`, or foreign/runtime ingress. Before it is stored, returned from a
non-raw-adapter API, or passed outside the immediate collapse logic, it MUST be rejected or translated in the same
body-bearing collapse declaration, or through a same-module private ingress-validating helper permitted above, into one of:

* a consequence-carrying data type under Section 10.6, including a consequence-carrying nominal data type, `state`,
  `reason`, `proof`, concrete `phase`, or scoped permit grant; or
* a transparent primitive/sequence record whose public fields expose the exact quantity, sequence, or numeric value.

A bare ingress-derived primitive or sequence value MUST remain private collapse plumbing and MUST NOT be an egress quantity
carrier.

This immediate-collapse rule does not apply to private non-exported semantically blind raw `Text`, raw `Bytes`, or raw
byte/character sequence plumbing unless that code receives foreign or runtime ingress, calls a raw-adapter declaration or
raw-adapter export, prepares foreign egress, or exposes the raw value through an exported or non-raw-adapter API. A private
helper is a private adapter helper for this rule only when it participates in the immediate ingress/collapse path.

### 10.6 Structural substrate and semantic-claim linting

Raw `Text` and raw `Bytes` are substrate sequence types and MAY be empty. Empty raw `Text` or raw `Bytes` MUST NOT encode
absence, failure, denial, not-yet-initialized state, not-found state, or a missing required domain value. `CString`,
`CInt`, and `CSize` are foreign ABI substrate types.

Foreign ABI substrate values are target-ABI values. `CInt` is the target C `int` value range. `CSize` is the target C
`size_t` value range. A conforming implementation MUST define the exact `CInt` and `CSize` ranges for each supported
target. `CString` is an opaque non-null foreign C-string value whose payload is the byte sequence before its terminating
NUL byte. A valid `CString` MUST have a terminating NUL byte and a finite payload byte count. The bytes after the first
terminating NUL byte are not part of the value. A valid `CString` is read-only and
payload-stable for the full dynamic extent in which Evident reads it or passes it to a foreign symbol; its payload bytes
and terminating NUL MUST NOT change during that extent. The current core language does not define a source-level type for
a nullable, unterminated, dangling, mutable, shared-unstable, manually released, non-finite, or otherwise unchecked raw C
string pointer; such ingress is outside the current strict foreign ABI surface and remains a deferred hazard primitive.

A type expression is raw-substrate-exposing when it is exactly `Text`, `Bytes`, `CString`, `CInt`, or `CSize`; when it is
exactly `List<Byte>`, `NonEmptyList<Byte>`, `List<Char>`, or `NonEmptyList<Char>`; when it is a compiler-owned collection
companion record instantiated from one of those raw byte or character sequence families; or when any of its explicit type
arguments is raw-substrate-exposing. This recursive rule applies to compiler-owned collections, compiler-owned companion
records, and user-defined generic records from `foundation` modules. A single `Byte` or `Char` value is not
raw-substrate-exposing, but exported domain or foundation sequence surfaces of raw bytes or raw characters are substrate
surfaces and MUST be represented instead by a consequence-carrying nominal domain type.
`NonEmptyText`, `NonEmptyBytes`, and nominal non-generic domain types are not
raw-substrate-exposing. A nominal type without explicit type arguments exposes its name, not its private representation;
its own exported fields and payloads are checked where the type is declared.

A type expression is raw-adapter-exposing when it is raw-substrate-exposing, when it is exactly one of the compiler-owned
substrate reason types `TextBoundsFailure`, `BytesBoundsFailure`, `TextCardinalityFailure`, `BytesCardinalityFailure`,
`ForeignAbiTextFailure`, or `ForeignAbiIntegerFailure`, when it names a raw-adapter export, or when any of its explicit
type arguments is raw-adapter-exposing. The raw-adapter-exposing and raw-adapter-export classifications are computed
together to a least fixed point.

A declaration's exported API surface is the set of type expressions exposed by that declaration:

* for `fn`, ordinary parameter types, the return type, and contract clause types
* for `foreign fn`, ordinary parameter types and the return type
* for `record`, public field types
* for `proof`, public field types
* for `phase`, public phase field types
* for `state`, payload field types of all variants of an exported `state`
* for `reason`, payload field types of all variants of an exported `reason`

An exported declaration's public semantic surface is the set of exported names that can trigger semantic-claim checks:

* the exported declaration name
* names of exported functions that produce, construct, mint, grant, validate, canonicalize, transition, or otherwise
  expose the declaration
* ordinary parameter names of exported functions
* public field names of exported `record`, `proof`, and `phase` declarations
* variant names and payload field names of exported `state` and `reason` declarations
* position names of exported `phase` declarations

Raw-adapter classification uses the exported API surface. Semantic-claim classification uses the public semantic surface.

Identifier word normalization is used by semantic-claim detection in this section and by reserved modeled-name matching in
Section 10.7. A conforming implementation MUST compute normalized words exactly as follows:

1. Start from an ASCII-only identifier token admitted by Section 8.
2. Treat `_` and ASCII digits as word separators. They do not produce words, and multiple separators collapse.
3. Split between a lowercase ASCII letter and a following uppercase ASCII letter.
4. Split before the last uppercase ASCII letter in an uppercase run when that last uppercase letter is followed by a
   lowercase ASCII letter. An acronym run remains one word; the next mixed-case word starts at its final uppercase letter.
5. Lowercase all resulting words with ASCII case folding.

No other Unicode, locale, or natural-language segmentation participates in this draft.

Semantic-claim detection is closed. For Section 10.6, a name in a public semantic surface claims a semantic fact only when
the name contains one of the semantic claim marker words in the table below under identifier word normalization. Names
outside this table do not trigger semantic-claim producer rules. Explicit semantic-claim syntax, user-defined claim
vocabularies, and compiler interpretation of arbitrary English names are **[Deferred]**.

| Claim category | Semantic claim marker words |
|----------------|-----------------------------|
| validation and curation | `valid`, `validated`, `validate`, `validation`, `validity`, `validator`, `verify`, `verification`, `curated`, `curate`, `curation`, `accepted`, `acceptance`, `canonical`, `canonicalized`, `canonicalize`, `canonicalization`, `syntax`, `syntactic`, `safe`, `safety`, `secure`, `security`, `trusted`, `trust`, `sanitized`, `sanitize`, `escaped`, `escape`, `encoded`, `encode`, `decoded`, `decode`, `normalized`, `normalize`, `authenticated`, `authenticate`, `attested`, `attest`, `signed`, `signature` |
| protocol, schema, and status | `protocol`, `schema`, `status`, `state`, `mode`, `tag`, `discriminator`, `sentinel`, `code`, `reported`, `unavailable` |
| policy and authority | `policy`, `authority`, `authorized`, `authorize`, `authorization`, `grant`, `granted`, `token`, `capability`, `credential`, `permit`, `permitted`, `permission`, `access`, `id`, `identifier`, `identity`, `role`, `ticket` |
| proof and receipt | `receipt`, `proof`, `proven`, `witness`, `verified` |
| handle and residency | `handle`, `resident`, `residency`, `open`, `opened`, `closed`, `released`, `connected`, `disconnected` |
| lifecycle and mechanism | `lifecycle`, `phase`, `transition`, `draft`, `submitted`, `sealed`, `ready`, `live`, `initialized`, `initialize`, `load`, `loaded` |

A marker in the exported declaration name applies to the declaration as a whole. A marker in an exported producer, ordinary
parameter, public field, payload field, variant, reason variant, or phase position applies to the declaration or value that
the marked name exposes. Primitive and sequence fields or parameters are claim-participating when they are public fields or
ordinary parameters of a declaration whose exported declaration or function name contains a marker, or when their own name
contains a marker. A conforming implementation MUST NOT infer additional semantic-claim triggers from unstated naming
intent.

Semantic marker coverage is computed by marker family, not by claim category. Groups not listed here are exact normalized
marker words. The non-exact marker families are: `valid` / `validated` / `validate` / `validation` / `validity` /
`validator`; `verify` / `verification` / `verified`; `curated` / `curate` / `curation`; `accepted` / `acceptance`;
`canonical` / `canonicalized` / `canonicalize` / `canonicalization`; `syntax` / `syntactic`; `safe` / `safety`; `secure`
/ `security`; `trusted` / `trust`; `sanitized` / `sanitize`; `escaped` / `escape`; `encoded` / `encode`; `decoded` /
`decode`; `normalized` / `normalize`; `authenticated` / `authenticate`; `attested` / `attest`; `signed` / `signature`;
`authorized` / `authorize` / `authorization`; `grant` / `granted`; `permit` / `permitted` / `permission`; `id` /
`identifier` / `identity`; `proof` / `proven`; `resident` / `residency`; `open` / `opened`; `lifecycle` / `phase` /
`transition`; `submitted` / `sealed` / `ready` / `live`; `initialized` / `initialize`; and `load` / `loaded`.

A visible consequence path is claim-specific only when the consequence surface covers every semantic-claim marker in the
produced claim. A consequence surface covers a semantic-claim marker when at least one of these source-visible conditions
holds:

* the consequence surface contains a semantic-claim marker from the same marker family as that produced claim marker; or
* the consequence surface names the produced nominal declaration by containing that declaration's complete normalized word
  sequence from Section 10.7 as a contiguous word sequence.

If the produced claim surface contains multiple semantic-claim markers, every produced marker MUST be covered. The broader
claim category does not create coverage.

A produced claim with zero semantic-claim markers MUST NOT satisfy claim-specific coverage by vacuity. A markerless
primitive/sequence-backed nominal whose direct external construction is blocked has a synthetic construction-control claim.
That synthetic claim is claim-specific only when the consequence surface names the produced nominal declaration by
containing that declaration's complete normalized word sequence from Section 10.7, or when the returned value is derived
from an already inhabited ordinary parameter whose type is consequence-carrying and whose public semantic surface or
required field path already names or definitely contains the produced nominal.

The consequence surface for a `reason` is the reason declaration name, reason variant names, and reason payload-field
names. The consequence surface for a `state` is the state declaration name, variant names, and payload-field names. The
consequence surface for a `proof` is the proof declaration name and proof field names. The consequence surface for a
concrete `phase` is the phase family name, position name, and phase field names. The consequence surface for a `permit` is
the permit declaration name. A permit parameter name or grant binding name is not a permit consequence surface and MUST
NOT provide semantic-marker coverage. The consequence surface for an already inhabited input is the public semantic
surface of the input type and any required field path used to derive the returned value.

A type expression definitely contains a consequence-carrying data type when the type expression is itself
consequence-carrying, or when a required structural path reaches one. The path follows required public or private record
fields, proof fields, concrete phase fields, always-present compiler-owned companion fields, explicitly instantiated
generic record fields after substitution when those fields are required, `NonEmptyList` element paths, and `NonEmptyMap`
key or value paths. It follows a `state` payload only when every variant contains at least one definite consequence path.
It never follows payloads behind empty-capable `List` or `Map` values. Maybe-present evidence does not satisfy this rule.

A consequence-carrying type is mechanically one of:

* a `reason` type, when it appears in a `fails` clause or failure arm,
* a `state` type,
* a `proof` type,
* a concrete `phase` type,
* a `permit` type, when it appears in a `grants` clause or permit parameter for an authority or authorized-operation claim,
* an exported semantic-claim nominal declaration that satisfies the construction-control and visible-consequence rules in
  this section,
* a record, proof, concrete phase, or compiler-owned companion record with at least one required field whose field type
  definitely contains a consequence-carrying data type,
* `NonEmptyList<T>` when `T` definitely contains a consequence-carrying data type, or
* `NonEmptyMap<K, V>` when `K` or `V` definitely contains a consequence-carrying data type.

A consequence-carrying data type is a consequence-carrying type that is a data type. A transparent primitive/sequence
record is not consequence-carrying except through fields whose own type is consequence-carrying. Empty-capable `List<T>`
and `Map<K, V>` are not consequence-carrying by containment.

For Section 10.6, an exported producer for a nominal declaration `N` is an exported function declared in the same module as
`N` whose body directly constructs `N` or calls same-module helper code that constructs `N`, and whose exported result type
is `N` or a type that definitely contains `N`.

An exported producer exposes a claim-specific consequence surface for a produced claim only when its exported signature,
contract clauses, or direct returned shape exposes one of the eligible surfaces below and that surface is claim-specific
under the marker-family coverage rule above:

* it can fail with a same-module `reason`;
* it returns a `state`;
* it declares `proves` for a same-module `proof` and returns that proof or a value that definitely contains it;
* it returns or transitions a concrete `phase` type;
* for an authority or authorized-operation claim, it requires a same-module permit type; or
* it derives the nominal value from an already inhabited ordinary parameter whose type is not raw-adapter-exposing and
  whose value definitely contains a consequence-carrying data type, or from a permit-authorized operation result whose type
  is itself consequence-carrying.

A producer satisfies a visible-consequence requirement only when the returned claimed value is constructed, copied, moved,
or derived through a claim-specific consequence surface. An unrelated `fails` reason, unrelated `state`, unrelated proof,
unrelated phase, unrelated permit, or unrelated preexisting consequence-carrying input MUST NOT satisfy the requirement.

An exported function has a function-level semantic claim when its exported function name or any ordinary parameter name in
its public semantic surface contains a semantic-claim marker. Each claim-participating ordinary parameter MUST have a
consequence-carrying data type whose consequence surface covers every semantic-claim marker in that parameter name. A
claim-participating ordinary parameter whose type is a core primitive or sequence type, a transparent primitive/sequence
record, `Unit`, or any non-consequence-carrying type is invalid.

An exported function whose own name has a function-level semantic claim MUST expose the successful claimed fact through one
of these source-visible success surfaces:

* a return type that is a consequence-carrying data type and whose public semantic surface covers every semantic-claim
  marker in the function name;
* a returned or definitely contained same-module `proof` declared by the function's `proves` clause whose consequence
  surface covers every semantic-claim marker in the function name;
* a returned `state` or concrete `phase` whose consequence surface covers every semantic-claim marker in the function name;
  or
* for an authority, authorization, grant, permit, permission, capability, credential, access, token, role, or ticket claim,
  a `grants` clause. The presence of the `grants` clause covers only `grant`, `granted`, `authorize`, `authorized`,
  `authorization`, `permit`, `permitted`, and `permission` markers in the function name; the permit consequence surface
  MUST cover every other semantic-claim marker in the function name.

The successful output of a function-level semantic claim MUST NOT be a direct core primitive or sequence type, transparent
primitive/sequence record, or bare `Unit`. `Unit` success is permitted for a function-level authority claim only when the
function declares a `grants` clause that satisfies the rule above. A `fails` clause may expose the rejection path for an
operation, but failure alone does not satisfy a successful function-level semantic claim.

An exported declaration in a `domain module` or `foundation module` MUST NOT expose a raw-adapter-exposing type expression
in its exported API surface. Every `foreign fn`, exported or private, is a raw-adapter declaration. A raw-adapter export is
an exported declaration in a `boundary module` or `hazard module` when any type expression in its exported API surface is
raw-adapter-exposing. An exported `foreign fn` is also a raw-adapter export. Raw-adapter exports are permitted only in
`boundary` and `hazard` modules. This classification is computed to a least fixed point over exported boundary and hazard
declarations, so a function that returns an exported raw-adapter record is itself a raw-adapter export. A private
`foreign fn` is not an export, but Section 10.5 still applies to the first body-bearing declaration that uses it. A
raw-adapter export MUST NOT be referenced from a `domain module` or a `foundation module`. A boundary or hazard declaration
intended for domain or foundation callers MUST have a non-raw-adapter-exposing exported API surface and MUST NOT be a
`foreign fn`.

A core primitive or sequence type expression is exactly `Int`, `Nat`, `Float`, `Char`, `Byte`, `Text`, `Bytes`, `NonEmptyText`, or
`NonEmptyBytes`, or a compiler-owned collection, compiler-owned companion record, or explicitly instantiated generic record
whose explicit type arguments or concrete fields directly or transitively contain one of those type expressions.

An exported nominal declaration is primitive/sequence-backed when its concrete representation directly or transitively
contains a core primitive or sequence type expression through public or private record fields, state payloads, proof fields,
phase fields, collection elements, map keys or values, compiler-owned companion records, or explicitly instantiated generic
record fields after substitution. A primitive/sequence-backed exported `record` whose direct external construction is
allowed, whose primitive/sequence-backed representation edges are all public fields, and whose public semantic surface has
no marker from the semantic-claim marker table is a transparent primitive/sequence record.

Any primitive/sequence-backed exported nominal declaration that is not a transparent primitive/sequence record is a
semantic-claim nominal declaration by construction, even when its public semantic surface contains no marker from the
semantic-claim marker table. Private construction over primitive or sequence representation is therefore treated as a
source-visible construction-control claim by default.

A semantic-claim nominal declaration backed by primitive or sequence values counts as a consequence-carrying semantic fact
only when all of the following hold:

* its exported API surface is not raw-adapter-exposing;
* direct external construction is blocked, unless every claim-participating public field is itself consequence-carrying;
* every exported producer for the declaration exposes a claim-specific consequence surface for the claim before returning
  the nominal value; and
* no exported producer constructs the nominal solely from a hidden literal, parser-invented default, private constant,
  private raw value, zero-argument helper result, bare primitive or sequence parameter, or primitive/sequence result from a
  permit-authorized operation whose result type is not itself consequence-carrying.

If those conditions do not hold, the declaration is invalid in a `domain module` or `foundation module`. In a
`boundary module` or `hazard module`, such a declaration is adapter-local and is a raw-adapter export for reference-classification
purposes; a `domain module` or `foundation module` MUST NOT reference it.

Foundation declarations are checked at every visibility, not only on exported surfaces. A `foundation` declaration MUST NOT
contain a semantic-claim marker in any user-authored declaration-surface name, and a `foundation` nominal declaration MUST
NOT be a semantic-claim nominal declaration. Private `foundation` records may carry raw, text, bytes, or generic structure
only as semantically blind plumbing.

Domain text or bytes that require content MUST be represented as `NonEmptyText`, `NonEmptyBytes`, or a
consequence-carrying domain type. Syntax validity, protocol membership, schema status, canonicalization, policy acceptance,
and other content predicates MUST be expressed through a claim-specific `reason`, `state`, `proof`, `phase`, `permit`, or
already-inhabited input under the rules above, or remain outside this draft under the deferred predicate-proof surface.

Private helper functions and private fields of non-exported declarations MAY use raw `Text`, raw `Bytes`, or raw byte or
character sequence surfaces as semantically blind plumbing. Private fields and private helper functions MAY use `CString`,
`CInt`, or `CSize` only inside `boundary` and `hazard` modules. A private raw field, private constant, fallback value, or
helper body MUST NOT satisfy a domain fact.

If an empty textual or byte sequence has genuine domain meaning, that meaning MUST be named by a domain type or a
consequence-first `state` variant. The raw empty sequence itself MUST NOT be the domain fact.

### 10.7 Reserved modeled names

The naming rules in Sections 10.1 through 10.4 are enforced in part by a closed set of reserved modeled names. A
conforming implementation MUST reject any user-authored declaration-surface name drawn from the reserved set below, and
MUST reject any user-authored declaration-surface name that is a single character.

These reservations apply regardless of visibility to user-authored declaration-surface names:

* a declaration name (`record`, `state`, `reason`, `proof`, `permit`, `phase`, `fn`, `foreign fn`, or `module`)
* a public or private field of a `record`, `proof`, or `phase`
* a variant name of a `state` or `reason`
* a payload field name of a `state` or `reason` variant
* a position name of a `phase`
* an ordinary function parameter name
* a permit parameter name
* a generic parameter name

Compiler-owned built-in names, compiler-owned companion names, and compiler-owned operation names are specified by Section
9 and are not user-authored declaration-surface names.

The reserved set is closed:

* boolean encodings (Section 10.1): `Yes`, `No`, `True`, `False`
* presence-or-absence wrappers (Section 10.2): `Some`, `None`, `Present`, `Absent`, `Missing`, `Unknown`, `Known`
* generic lifecycle or mechanism labels (Section 10.1): `Ready`, `Unavailable`, `Connected`, `Disconnected`, `Reported`, `NotReported`
* placeholder or sentinel labels (Section 10.4): `Default`, `Other`, `Invalid`, `Unset`
* wildcard bypass variants (Section 10.3): `Any`, `All`, `Unrestricted`, `AllowAll`

To test whether a user-authored declaration-surface name is drawn from the reserved set, normalize both the candidate name
and each reserved entry by splitting on underscores and ASCII case transitions, comparing word sequences
case-insensitively, and ignoring digits.

The boolean and generic lifecycle or mechanism reserved entries are exact reserved entries. A candidate matches one of
those entries only when its complete normalized word sequence equals the reserved entry's complete normalized word
sequence.

The presence-or-absence, placeholder, sentinel, and wildcard bypass reserved entries are forbidden token sequences. A
candidate matches one of those entries when the reserved entry's complete normalized word sequence appears anywhere as a
contiguous word sequence in the candidate.

This reserved set is the complete compiler-enforced declaration-surface-name rejection set in this draft. Sections 10.1
through 10.4 do not authorize hidden compiler rejection of names outside the reserved set.

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
    public endpoint: NonEmptyText,
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

Pseudo-optional shapes are forbidden. A `state` MUST NOT use the Section 10.7 presence-or-absence reserved names.

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
    RequiredKeyNotProvided { key: NonEmptyText },
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
    public config_id: NonEmptyText,
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
        public id: NonEmptyText,
        public payload: NonEmptyText,
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

The phase family name `AppConfig` is not a concrete type and MUST NOT be used as a value type or type annotation.

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
* A phase family name is not a concrete type.

Inline by-value representation containment MUST be acyclic. The inline containment graph has an edge from a nominal data
type to every data type stored directly in its concrete representation, including record fields, state payload fields,
proof fields, phase fields, compiler-owned companion record fields, and explicitly instantiated generic record fields
after substitution. Compiler-owned collection element types, map key types, and map value types are not inline
containment edges for this rule; finite collection values are representation boundaries rather than inline expansion of
each element. A conforming implementation MUST reject any non-trivial strongly connected component or self-loop in the
inline containment graph. This rule applies even when the cycle passes through private fields, compiler-owned companion
records, or instantiated generic records. Recursive value shapes through compiler-owned collections are permitted by this
inline-layout rule only when all other category, raw-substrate, map-key, affine, and exported-surface rules are satisfied.

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

A failing expression MUST be handled with `try` or `match`.

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

### 14.4 Stable authority and minting contract

The current core language treats proof creation and permit use as explicit authority events. A conforming implementation MUST enforce all of the following:

* A permit type is not data. It MUST NOT be stored, returned, matched, nested in collections or generic arguments, constructed directly, or bound with ordinary `let`.
* A permit parameter MUST be written as `as name: PermitType`.
* A permit argument MUST be written as `as name` in the direct argument position where the callee expects that permit type.
* A permit binding MUST originate only from a permit parameter or from `grant grantor(...) as name { ... }`.
* A grantor function MUST declare `grants PermitType`, MUST be declared in the same module as that permit type, and MUST return `Unit`.
* A grantor function MUST be invoked through `grant`; an ordinary direct call to a function with `grants` is invalid.
* A permit binding introduced by `grant` is lexical. It exists only inside the grant body and is not consumed by being passed to authorized calls.
* `grant` failure typing is exact: the grantor failure reason and grant body failure reason MUST either be absent or be the same `reason` type.
* A proof value MUST be created with `prove ProofType { ... }`. Direct proof construction is invalid.
* A function MAY use `prove ProofType { ... }` only when its own signature declares `proves ProofType`.
* A function that declares `proves ProofType` MUST be declared in the same module as that proof type.
* Duplicate `proves` clauses for the same proof type are invalid.
* A `prove` initializer MUST name exactly the proof fields, MUST NOT duplicate fields, and each initializer expression MUST have the declared field type without an unhandled failure.
* Proof values are affine. Moving a proof consumes that binding; reusing it after move is invalid.

These rules are the stable subset contract for authority and proof minting. Broader typestate transition syntax may extend the language later, but it MUST NOT weaken these rules.

### 14.5 `foreign fn`

A `foreign fn` declares an external function without a body.

A conforming toolchain MUST statically enforce the source-visible `foreign fn` rules:

* a `foreign fn` may appear only in `boundary` or `hazard` modules,
* a `foreign fn` parameter list may contain only ordinary value parameters,
* a `foreign fn` parameter type MUST be exactly `CString`, `CInt`, or `CSize`,
* a `foreign fn` return type MUST be exactly `CString`, `CInt`, `CSize`, or `Unit`,
* no other built-in type, user-defined type, compiler-owned collection type, compiler-owned companion type, `reason`,
  `permit`, `proof`, or concrete `phase` type is foreign-ABI-admissible, and
* a `foreign fn` is a raw-adapter declaration under Section 10.6, and is a raw-adapter export when exported.

For the supported `x86_64-pc-windows-msvc` native target, the foreign ABI representation table is:

| Evident type | C prototype spelling | ABI representation |
|--------------|----------------------|--------------------|
| `CInt` | `int` | 32-bit signed MSVC C `int`, passed and returned by value |
| `CSize` | `size_t` | 64-bit unsigned MSVC C `size_t`, passed and returned by value |
| `CString` | `char const *` | 64-bit non-null pointer passed and returned by value; points to immutable NUL-terminated `char` byte storage satisfying Section 10.6 |
| `Unit` return | `void` | no return value |

A `foreign fn` declaration for this target is equivalent to the C prototype obtained by replacing each Evident foreign ABI
type with the table entry above, preserving parameter count and order, and using the Windows x64/MSVC external C function
calling convention. A conforming implementation MUST reject a `foreign fn` declaration whose source-visible signature
cannot be represented exactly by this table. A binding assertion whose external symbol has a different C prototype,
different signedness or width, nullable or mutable C-string contract, different parameter count or order, different return
type, or different calling convention is outside Evident semantics and is non-conforming.

For the supported `x86_64-pc-windows-msvc` native target, a `foreign fn` declaration binds to an unmangled external C ABI
symbol whose link name is exactly the Evident function identifier. Evident does not mangle `foreign fn` names, does not
overload `foreign fn` declarations by signature, and does not infer alternate symbol names from module paths. The call uses
the target's external C function ABI and Windows x64/MSVC calling convention. A `foreign fn` MUST NOT rely on a C++ mangled
name, overload set, method receiver, variadic argument list, ordinal import, import-library alias, alternate link name,
alternate calling convention, or target ABI that is not explicitly specified by this draft. Alternate foreign binding
syntax and non-Windows target ABI bindings are **[Deferred]** until their symbol names, calling conventions, value
representations, and rejection rules are specified.

The `foreign fn` external link-name namespace is package-wide. A package MUST NOT contain two `foreign fn` declarations
with the same link name, even when their Evident signatures are identical. A single same-module raw adapter declaration
owns each external symbol assertion for the package.

A `foreign fn` declaration asserts that the external symbol communicates all source-visible success, failure, status, and
result information only through its admitted return value and the direct effects that the surrounding hazard or boundary
adapter explicitly models. Because `foreign fn` declarations cannot declare `fails`, an external symbol whose success,
failure, status, or result must be read from `errno`, `GetLastError`, thread-local status, hidden global state,
callback-order state, an output parameter, a mutable buffer, a stored C string pointer, or another out-of-band channel is
outside the current strict foreign ABI surface. Such a symbol MUST NOT be declared directly as a `foreign fn`; it must be
wrapped outside Evident so the wrapper returns an admitted explicit value, or wait for a future hazard primitive whose
source surface names that out-of-band channel.

Declaring a `foreign fn` with a `CString` parameter or return type is an external contract assertion about the external
symbol. The declaration asserts that each such `CString` obeys the validity rules in Section 10.6 for the full dynamic
extent in which Evident reads the value or passes it to the external symbol. This assertion is trusted raw-adapter input to
the language. If the assertion is false for any dynamic call, the program is non-conforming and the resulting behavior is
outside Evident semantics. If an external symbol may receive or produce a null, unterminated, dangling, mutable,
shared-unstable, manually released, or otherwise invalid C string, that symbol is outside the current strict Evident foreign
ABI surface and MUST NOT be declared with `CString`. It must be wrapped outside the strict Evident surface or exposed later
through a separately specified hazard primitive. Nullable raw pointers, mutable buffers, length-coupled buffers,
out-parameters, caller-owned lifetime protocols, caller-managed release protocols, and other unchecked ABI shapes are
**[Deferred]** as source-level foreign substrate types.

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

AppConfig::Draft { id: "cfg-1", payload: "payload" }
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
    failed(ParseFailure::RequiredKeyNotProvided { key }) => recover(key),
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
        public id: NonEmptyText,
        public payload: NonEmptyText,
    }

    positions {
        Draft,
        Validated,
    }
}

public proof ConfigValidated {
    public config_id: NonEmptyText,
}

public record ValidationResult {
    public config: AppConfig::Validated,
    public receipt: ConfigValidated,
}

public reason ValidationFailure {
    PayloadRejectedByPolicy,
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

Generic function calls MUST write all type arguments explicitly, for example `identity<Int>(value)`.
The language does not infer user-defined generic function arguments.

Generic record construction MUST write all type arguments explicitly, for example `Box<Int> { value: value }`.
The language does not infer user-defined generic record constructor arguments.
Generic type arguments MAY themselves be explicitly instantiated generic records, for example `Pair<Box<Int>, Box<Int>>`.
Different explicit type-argument tuples MUST be treated as distinct concrete instantiations.

### 16.2 Structural blindness

A user-defined generic abstraction MUST remain structurally blind.

A generic abstraction MUST NOT assign domain meaning to its type parameters.

Generic parameter identifiers are user-authored declaration-surface names and MUST satisfy Section 10.7. In addition, a
generic parameter identifier in a `foundation module` MUST NOT contain any semantic-claim marker from Section 10.6 under
the same underscore, ASCII case-transition, case-insensitive, digit-ignoring splitting rule used for semantic-claim
detection. A `foundation` generic parameter names a structural slot, not a validation state, authority, identity, status,
lifecycle, protocol, proof, handle, or content predicate.

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

A conforming Evident toolchain MUST reject every program that violates a `MUST` or `MUST NOT` requirement in this
document. This section is an index of rejection families, not a duplicate checklist of every atom in the normative text.
When this section and a body section appear to differ in detail, the body section is authoritative.

At minimum, a conforming implementation MUST reject violations in these families:

* source text, token, literal, grammar, declaration-shape, duplicate-name, unknown-reference, package/import, and explicit
  module-kind rules (Sections 5, 8, and 8.1)
* visibility, private-type leakage, construction-control, direct construction, and private-field access rules (Sections 6
  and 15)
* built-in type, literal typing, admitted target, map-key, compiler-owned collection, text/bytes, foreign ABI conversion,
  compiler-owned name, and substrate-reason rules (Section 9)
* exact boundary and hazard collapse rules, including raw-adapter relay, ingress-derived primitive/sequence escape, private
  ingress helper, foreign-egress source-carrier, same-body mapping, and immediate foreign-call preparation rules (Section
  10.5)
* raw-substrate, raw-adapter export, raw-adapter reference, exported domain/foundation raw exposure,
  consequence-carrying containment, primitive/sequence-backed nominal anti-laundering, and semantic-claim nominal rules
  (Section 10.6)
* semantic-claim marker, marker-family coverage, claim-specific consequence surface, markerless construction-control,
  permit type-only coverage, foundation semantic-blindness, and exported function-level semantic-claim rules (Section 10.6)
* consequence-first modeled-name, pseudo-optional, wildcard bypass, sentinel, reserved declaration-surface-name,
  single-character name, and silent default-injection rules (Sections 10.1 through 10.7)
* copyability, affine move, permit binding, direct affine-field extraction, and reuse-after-move rules (Section 11)
* `record`, `state`, `reason`, `proof`, `permit`, concrete `phase`, and type-position category rules (Sections 12 and 13)
* inline by-value representation containment cycle rules (Section 13)
* `fails`, `grants`, `proves`, stable authority, proof minting, grant failure typing, and `foreign fn` rules (Section 14)
* expression rules for calls, permit passing, construction, `match`, `try`, `fail`, `grant`, and `prove` (Section 15)
* generic quarantine, allowed generic forms, structural blindness, generic parameter naming, and affine propagation through
  generic instantiation (Section 16)

## 18. Deferred Areas

The following areas are intentionally outside this draft:

* external package identity, dependency metadata, build profiles, version constraints, and cross-package linking
* mutation and assignment semantics beyond the immutable core defined here
* trait declarations, trait bounds, and open polymorphism
* explicit phase-family helper syntax for “any position of a family”
* the `Float` arithmetic, comparison, and transcendental operation surface, alternate floating-point rounding-mode
  selection, foreign ABI floating-point binding, and `Float`-to-`Text`/`Bytes` conversion (the `Float` type, its finite
  IEEE 754 binary64 value set, float literals, roundTiesToEven rounding, and exceptional-result collapse are normative
  under Section 9)
* collection literals, comprehensions, higher-order traversal, borrowing iterators, index-based access, slicing, sorting,
  and specialized representation controls for the compiler-owned collection families (`Text` and `Bytes` length,
  element access, and slicing are defined in Section 9.8 and are not deferred)
* source-level foreign substrate types for nullable raw pointers, mutable buffers, unchecked C string pointers,
  length-coupled buffers, out-parameters, caller-owned lifetime protocols, caller-managed release protocols, and other raw
  ABI shapes not admitted by Section 14.5
* alternate foreign binding syntax, C++ mangled names, foreign overload sets, ordinal imports, import-library aliases,
  alternate link names, variadic foreign calls, alternate calling conventions, and non-Windows target ABI bindings
* source-level modeling of foreign `errno`, `GetLastError`, thread-local status, hidden global status, callback-order
  status, output-parameter result channels, mutable-buffer result channels, and stored-pointer result channels
* explicit semantic-claim syntax, user-defined semantic-claim vocabularies, and compiler interpretation of semantic claims
  outside the closed marker table in Section 10.6
* static proof of arbitrary semantic predicates beyond the structural anti-laundering lint rules in Section 10.6
* concurrency and runtime coordination primitives beyond module classification
* macros and compile-time metaprogramming
* custom constructor syntax beyond field visibility, `grant`, and `prove`

## 19. Maintenance Rule

When a language decision changes syntax, typing, visibility, construction control, value discipline, proof semantics, permit semantics, phase semantics, module classification, or genericity, this document MUST be updated in the same change.
