# Type-Driven Design in C++

## 0. Reading Rule

Read for invariants, not syntax. Examples illustrate the rule; they are not templates to copy mechanically.

This contract is stricter than raw C++ semantics. If C++ permits a construct that this document forbids, this document wins. Enforce the contract mechanically with CI linting, architectural tests, strict compiler flags (`-Wall -Wextra -Werror`), and static analysis (`clang-tidy`, use-after-move checks, architectural linting).

If core logic requires `.value()`, `.value_or()`, `assert`, `std::terminate`, `dynamic_cast`, `typeid`, raw ownership pointers, `reinterpret_cast`, `const_cast`, C-style casts, `std::any`, `std::variant`, or `std::shared_ptr<std::mutex>` to make the model workable, the invariant or ownership model is wrong.

## I. Core Contract

Invalid states MUST be unrepresentable. Types MUST make invalid states impossible to construct or impossible to pass across internal boundaries. Core logic MUST NOT rely on discipline, runtime checks, comments, or documentation to preserve invariants.

If an invalid domain state can exist after a successful compilation, the type design is wrong. Successful compilation MUST be evidence of structural integrity, not mere syntax validity.

## II. Definitions

For this document:

- **Repo-defined surface** means any type or signature declared in this repository, including class fields, scoped enum payloads, type aliases, config DTOs, persisted-state models, function parameters, and return types, except a single foreign-schema adapter signature whose shape is mechanically dictated by an external ABI, parser, callback, or generated client.
- **Foreign-schema adapter signature** means the exact boundary entrypoint forced by foreign data. It MAY mention external `std::optional`, `bool`, nullable pointers, loosely typed objects, integerly typed enums, or equivalent ambiguity only long enough to collapse them before any repo-defined value is constructed.
- **Boundary** means the immediate deserialization layer, FFI adapter, CLI/environment decoder, or external API handler that first receives foreign data.
- **Core logic** means internal business or domain logic after boundary translation.
- **Strict-surface API** means an API outside explicitly named runtime-shell, compatibility-quarantine, or low-level substrate modules.
- **Runtime-shell / compatibility-quarantine module** means a module explicitly designated to isolate runtime coordination, legacy APIs, OS integration, or foreign-library constraints. It is not allowed to define domain truth.
- **Low-level substrate module** means an explicitly named internal module whose sole purpose is to implement primitives such as `Closed<Ts...>`, storage utilities, or ABI glue. It MUST NOT leak low-level escape hatches into strict-surface APIs.

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**, and **MAY** are to be interpreted as defined by BCP 14 (RFC 2119 and RFC 8174) when, and only when, shown in all caps.

## III. Absolute Modeling Rules

### A. No Optionality in Repo-Defined Types

Repo-defined surfaces MUST NOT contain `std::optional<T>`, nullable raw pointers (`T*`), `std::nullptr_t` branches, or `std::shared_ptr<T>` / `std::weak_ptr<T>` used to indicate absence. This includes nested forms such as `std::vector<std::optional<T>>`.

Repo-defined types MUST NOT replace optionality with pseudo-optional wrappers such as `Absent | Present(T)`, `Missing | Value(T)`, `Unknown | Known(T)`, `Closed<std::monostate, T>`, `Closed<None, T>`, or any equivalent pair where one branch means only "no value".

If data can be absent, the model is wrong. Fix it by one of the following:

1. split the model into distinct inhabited types,
2. use typestate, or
3. introduce a genuine domain mode whose variants name behavior, not absence.

Mere absence MUST NOT be modeled as a variant.

Strict-surface APIs MUST NOT accept false optionality. A parameter MUST NOT be `std::optional<T>` when every valid call path requires a `T`.

Use explicit operational modes instead of presence wrappers:

```cpp
struct FeatureConfig final {};

using FeatureMode = Closed<
    struct OfflineOnly,
    struct ProviderBacked
>;
```

`OfflineOnly` is allowed because it names behavior. `Absent` is forbidden because it names missing data.

### B. No Bare Booleans

Repo-defined surfaces MUST NOT contain `bool`, including nested forms such as `std::vector<bool>` or `std::unordered_map<K, bool>`.

Every boolean distinction stored or passed MUST be named with a scoped enum, typestate, capability token, or split inhabited types. `true` and `false` are storage encodings, not domain language.

### C. No Wildcard Bypass Variants

Repo-defined config and permission ADTs MUST NOT define wildcard variants such as `Any`, `All`, `Unrestricted`, or `AllowAll`.

Use finite policy lattices with explicit merge functions instead.

Boundary absence is permitted only transiently, but it MUST collapse into an inhabited requirement value before any repo-defined value is constructed.

### D. `std::expected` Is Control Flow, Not Domain State

Returning `std::expected<T, E>` from fallible boundary operations is permitted. It MUST NOT be stored in domain state or other repo-defined data types.

`std::expected<T, std::monostate>`, `std::expected<T, NotFound>`, `std::expected<T, Missing>`, `std::expected<void, E>`, and any equivalent carrier where either branch carries no information MUST NOT be used. `void` as a success type is `std::monostate` by another name. If an operation succeeds, the success MUST be named: return a proof type, a receipt, a handle, or a capability token.

When the only purpose of an operation is to branch immediately among mutually exclusive outcomes, the implementation MUST use CPS / Church Encoding or immediate exhaustive dispatch. It MUST NOT materialize an unresolved control-state object just to be inspected later.

If the alternatives are themselves stable domain facts with downstream meaning, use distinct inhabited types or a sealed `Closed<Ts...>`.

### E. External Primitives Are Boundary-Local Only

External `std::optional`, `bool`, nullable pointers, stringly typed discriminators, integerly typed enums, parse failures, and loosely typed objects MUST NOT be stored, forwarded, or bound to named locals whose scope outlives the immediate collapse logic.

Collapse them immediately in the exact boundary function that first receives them, either:

- inline in a `switch` / `if`,
- inside `from_json`, `TryFrom`-style adapters, parser callbacks, or equivalent boundary conversion code, or
- inside a smart constructor invoked directly by the boundary.

Do not assign foreign ambiguity to a local and juggle it across helper layers. Do not forward raw optionality or booleans into core logic.

### F. No Invalid Enum States

Scoped enums are naming tools, not proofs. Integer casts, value-initialization, zero-initialization, and unchecked deserialization can manufacture invalid enumerators.

Repo-defined scoped enums MUST NOT be constructed from integers outside a validating boundary. If zero is not a semantic enumerator, the enum MUST be wrapped in a private-construction type or replaced with split types, typestate, or capability tokens.

Sentinel enumerators such as `Invalid`, `Unknown`, `None`, or `Unset` are forbidden. If the domain has a state that other frameworks would call "unknown" or "none," that state MUST be given a consequence-first name describing what the system does when it encounters it.

### G. No Sentinel Values

Empty strings, empty vectors, zero IDs, `UINT32_MAX`-style placeholders, null handles, all-zero structs, and equivalent in-band markers MUST NOT encode absence, failure, "not yet initialized", or "not found" in repo-defined surfaces. If a zero or empty value has domain meaning, it MUST be wrapped in a named type that makes that meaning explicit. The raw sentinel is never the domain fact; the named wrapper is.

## IV. Naming Rules

Repo-defined enums and types MUST use consequence-first names. A variant name MUST describe either:

- the downstream operational consequence, or
- the definitive domain fact established by that variant.

Repo-defined enums and types MUST NOT use generic lifecycle or container labels such as `Ready`, `Unavailable`, `Present`, `Missing`, `Connected`, `Disconnected`, `Reported`, or `NotReported`.

Variant names MUST NOT describe the upstream process that produced the state. They MUST describe what the system now knows or what the system must now do.

If a downstream consumer must inspect the constructor, parsing path, or origin function to understand how to behave, the name has failed. The compiler enforces structure; naming MUST enforce semantics.

When a variant encodes a restriction, fallback, or denial, the name MUST describe the exact resulting perimeter or behavior. Use `HostIsolated` rather than `Sandboxed`. Use `FollowPlatformDefault` rather than `Absent`.

## V. Typestate, Closed Sum Types, and State as Location

### A. Typestate

Use typestate to model phase transitions. Distinct phases MUST be distinct types to the compiler. Transitions MUST consume the prior state via an `&&`-qualified member function or an equivalent function taking `T&&`, and MUST return the next state.

```cpp
struct CardDrawn final {
  Card card;
  class ShuffledDeck remaining;
};

struct FinalCardDrawn final {
  Card card;
};

using DrawOutcome = Closed<CardDrawn, FinalCardDrawn>;

class ShuffledDeck final {
public:
  ShuffledDeck(ShuffledDeck const&) = delete;
  ShuffledDeck& operator=(ShuffledDeck const&) = delete;

  [[nodiscard]] DrawOutcome draw() &&;
};
```

`ShuffledDeck` exists; an unshuffled game deck does not. The draw transition consumes the old state and yields a new inhabited state.

Typestate carriers and capability tokens MUST delete copy construction and copy assignment. Because moved-from C++ objects remain accessible, static analysis MUST be used to prevent use-after-move. Transition-returning APIs SHOULD be marked `[[nodiscard]]`.

### B. Parametric Typestate

When multiple states share one data layout, a sealed marker parameter MUST be used.

```cpp
namespace phase {
  struct Draft final {
  private:
    Draft() = default;
    friend AppConfig<Draft> createDraftConfig(NonEmptyString, NonEmptyString);
  };
  struct Validated final {
  private:
    Validated() = default;
    friend AppConfig<Validated> validateConfig(AppConfig<Draft>&&);
  };

  template <class T>
  concept ConfigPhase =
      std::same_as<T, Draft> ||
      std::same_as<T, Validated>;
}

template <phase::ConfigPhase S>
class AppConfig final {
public:
  AppConfig(AppConfig const&) = delete;
  AppConfig& operator=(AppConfig const&) = delete;
  AppConfig(AppConfig&&) = default;
  AppConfig& operator=(AppConfig&&) = default;

private:
  friend AppConfig<phase::Draft> createDraftConfig(NonEmptyString, NonEmptyString);
  friend AppConfig<phase::Validated> validateConfig(AppConfig<phase::Draft>&&);

  AppConfig(NonEmptyString id, NonEmptyString payload, S phase)
      : id_(std::move(id)), payload_(std::move(payload)), phase_(std::move(phase)) {}

  NonEmptyString id_;
  NonEmptyString payload_;
  [[no_unique_address]] S phase_;
};
```

When states share layout, this pattern MUST be used. When states require different fields, separate classes MUST be used. Do not reintroduce `std::optional`, nullable members, or partial initialization to fake a shared layout.

The set of legal phase markers MUST be sealed by the defining module. Downstream code MUST NOT be able to invent new phases. Phase tag constructors MUST be private. Construction authority MUST be granted to specific transition functions via `friend` function declarations, not to classes.

### C. Use Closed Sum Types for Exclusive States

Product types multiply state space. Closed sum types add it. Multiplication MUST NOT be used when the domain requires exclusive alternatives.

Repo-defined surfaces MUST NOT model exclusive alternatives with:

- a tag enum plus partially relevant fields,
- a `bool` plus a sometimes-meaningful payload,
- nullable members,
- sentinel values,
- base-class hierarchies or `std::unique_ptr<Base>` for a closed world, or
- `void*` / type erasure in place of a finite alternative set.

Invalid:

```cpp
struct Connection {
  ConnectionState state;
  Socket* socket; // null in some states
};
```

Valid:

```cpp
struct HostUnreachable final {};
struct EstablishedStream final { Socket socket; };

using TransportSession = Closed<HostUnreachable, EstablishedStream>;
```

If a single topological slot is not required, prefer separate inhabited types and separate locations.

### D. State as Location

Collections MUST NOT carry lifecycle state through status flags, nullable members, or colocated sum wrappers. Location is the state.

Use distinct physical containers such as:

```cpp
std::vector<PendingAsset> pending_uploads;
std::vector<ResidentAsset> resident_textures;
```

Transitions MUST consume from one location and produce into another. Presence in the destination location is the proof.

### E. Algorithm and Range Style

Pure collection transformations SHOULD be expressed with standard algorithms or ranges when doing so keeps end-of-sequence state, empty-subrange state, and filtering mechanics inside iterator machinery.

Imperative loops MAY be used for side effects, tight performance constraints, or algorithmic clarity, but they MUST NOT materialize placeholder domain states, pseudo-optionals, or two-phase accumulation objects in repo-defined surfaces.

End sentinels, exhaustion state, and temporary emptiness belong inside the algorithm machinery, not in the domain model.

### F. The `Closed<Ts...>` Mandate

Repo-defined surfaces MUST NOT use `std::variant<Ts...>`.

If a domain model strictly requires a heterogeneous collection or exclusive state in one topological slot, it MUST use a sealed, repo-owned sum type such as `Closed<Ts...>` that enforces all of the following:

1. **No valueless state.** The active alternative MUST exist for the entire lifetime of the object.
2. **No default construction.** `Closed<Ts...>` MUST be non-default-constructible.
3. **Named construction only.** Construction MUST occur through named factories.
4. **Private discriminator and storage.** The active tag and raw storage MUST be private and MUST NOT be user-specializable.
5. **No guessing.** Public APIs equivalent to `index()`, `tag()`, `holds<T>()`, `get<T>()`, `get_if<T>()`, `reset()`, `emplace()`, `swap()`, or raw-storage access MUST NOT exist.
6. **Exhaustive observation only.** The only observation mechanism MUST be `match()`, and `match()` MUST require an exact overload set covering all `Ts...`.
7. **No generic catch-alls.** Generic lambdas, templated call operators, ellipsis overloads, base-class visitors, RTTI-based inspection, and fallback overloads are forbidden.
8. **Closed alternatives only.** Alternatives MUST be pairwise distinct. Closed sets MUST NOT be modeled as open polymorphic hierarchies.
9. **No in-place retagging.** Replacing one active alternative with another inside the same object is forbidden. Construct a new `Closed<Ts...>` instead.
10. **Nothrow movement requirement.** Move support MAY exist only when all alternatives are `std::is_nothrow_move_constructible_v` and the implementation never creates a disengaged state during movement.
11. **Copy is deleted.** Copy construction and copy assignment MUST be deleted.

A valid observation looks like this:

```cpp
session.match(
    [](HostUnreachable const&) -> void { /* ... */ },
    [](EstablishedStream const& s) -> void { /* ... */ }
);
```

This is invalid:

```cpp
session.match([](auto const&) { /* forbidden generic catch-all */ });
```

## VI. Parametricity and Concept Bounds

Use unconstrained templates when a function MUST remain blind to the element type.

```cpp
template <class T>
T const& first(NonEmptyList<T> const& list) {
  return list.first();
}
```

With no concept bounds, the implementation cannot branch on `T`, inspect `T`, or call type-specific behavior on `T`. It is forced to operate only on container structure.

Add concepts only for capabilities the function actually requires.

```cpp
template <class T>
void render_blind(T const&) {}

template <class T>
requires StreamInsertable<T>
std::string render(T const& value) {
  std::ostringstream os;
  os << value;
  return os.str();
}
```

A function claiming parametric behavior MUST NOT use `if constexpr` on type traits, RTTI, `dynamic_cast`, `typeid`, reflection mechanisms, or detection-based branching to inspect the type it claims to ignore.

Concept bounds are call-site rejection. The function does not exist for types that do not satisfy the bound.

## VII. Ownership Is Coordination

One component MUST own each mutable resource. If two components must "agree" on the state of a resource, the architecture MUST be rewritten.

Ownership hierarchy is the coordination mechanism. Other components borrow, observe, or communicate with the owner; they do not co-own domain truth.

`std::shared_ptr<T>` plus locks, `std::mutex`, `std::shared_mutex`, `std::condition_variable`, global lock registries, and lock-wrapped maps are strong signals that ownership was not drawn correctly. Core domain state MUST be rewritten into one owner plus borrowed views or typed coordination handles.

Retry loops inside core logic indicate a consensus failure: the system is trying to synchronize with itself. Boundary retries adapt to external unreliability. Internal retries indicate broken ownership.

### A. Thread and Task Boundaries

A thread, task, actor, event-loop owner, or callback-owned coordinator is an ownership boundary. Cross-thread mutation MUST be modeled as transferred ownership or typed coordination handles, not shared mutable references.

Core domain state, policy state, authority state, typestate carriers, proof-bearing values, and business invariants MUST NOT be shared across threads or tasks through lock-wrapped shared ownership or equivalent wrappers.

Strict-surface APIs MUST NOT expose shared mutable ownership. They MUST expose typed handles such as command queues, request/reply handles, read-only snapshots, permits, or join rights.

Locks are not state machines. A lock-protected value MUST NOT be the canonical owner of domain truth when an owner thread or coordinator can exist.

### B. Narrow Exception: Runtime Coordination Cells

`std::mutex`, `std::shared_mutex`, `std::condition_variable`, `std::atomic<T>`, and equivalent coordination primitives are permitted only in explicitly named runtime-shell or compatibility-quarantine modules, and only when all of the following hold:

1. The guarded value is coordination metadata, not domain truth.
2. The guarded value does not encode policy, permission, typestate, phase, or business invariants.
3. No user callback, blocking foreign call, or re-entrant subsystem call occurs while the primitive is held.
4. The guard, reference, or pointer to guarded state never escapes the module.
5. The primitive never appears in a strict-surface API.
6. No nested locking or lock-order graph exists.
7. The cell is replaceable in principle by an owner thread, actor, or coordinator and is retained only as a localized runtime convenience.

Permitted examples: pending-response maps, cancellation registries, hot cache shards, metrics accumulators, lazily initialized runtime slots.

Forbidden examples: workflow state, approval policy, capability possession, typestate progression, session authority, proof of residency, or any value whose meaning defines application behavior.

## VIII. Providers Expose Mechanism; Callers Decide Policy

Providers MUST expose mechanisms. Callers MUST enforce policies.

A provider MUST NOT silently return fallback data when the caller asked for a specific resource. Hidden defaults are policy decisions.

If a function can return fallback data or real data, the interface MUST distinguish those outcomes via CPS, distinct inhabited types, or proof-bearing views. Otherwise the function has embedded policy inside data access.

Loading and reading MUST be separated. Separate mutation from proof of residency.

```cpp
class LoadReceipt final {
private:
  friend std::expected<LoadReceipt, LoadError> TextureManager::load(AssetId);
  LoadReceipt() = default;
};

class ResidentTextureView final {
public:
  Texture const& get() const noexcept { return *texture_; }

private:
  friend std::expected<ResidentTextureView, LookupError> TextureManager::get(AssetId) const;
  explicit ResidentTextureView(Texture const& texture) noexcept
      : texture_(&texture) {}

  Texture const* texture_;
};

class TextureManager final {
public:
  [[nodiscard]] std::expected<LoadReceipt, LoadError> load(AssetId id);
  [[nodiscard]] std::expected<ResidentTextureView, LookupError> get(AssetId id) const;
};
```

`load()` mutates and returns a proof type on success — not `void`. A `void` success branch carries no information and is `std::monostate` by another name. `get()` proves residency through a read-only view. The provider reports reality; the caller decides what to do with failure.

`std::expected<void, E>` is banned. If an operation succeeds, the success MUST be named.

## IX. Capability and Proof Tokens

Tokens encode system invariants into the type system, but they must be strictly categorized by lifecycle to prevent categorical errors in storage and movement.

The spec distinguishes two forms of tokens: **Authority Tokens** and **Proof-of-Fact Tokens**.

### A. Authority Tokens (Non-Movable)

Operations valid only within a phase or under a specific authority MUST require a phase-bound or authority-bound Authority Token. Use an empty witness type with a private constructor and a scoped `friend` function — not a `friend` class.

```cpp
class RenderPassPermit final {
public:
  RenderPassPermit(RenderPassPermit const&) = delete;
  RenderPassPermit& operator=(RenderPassPermit const&) = delete;
  RenderPassPermit(RenderPassPermit&&) = delete;
  RenderPassPermit& operator=(RenderPassPermit&&) = delete;

private:
  friend void RenderPass::withActivePass(std::invocable<RenderPassPermit const&> auto&&);
  RenderPassPermit() = default;
};

void submit_draw_call(RenderPassPermit const&, Mesh const&);
```

Authority Tokens are non-copyable AND non-movable. They exist only as stack locals within the scoped guard that constructs them. Construction authority MUST be granted to a specific guard function, not to a class. Storing an Authority Token in a member variable, capturing it in a closure that outlives the scope, wrapping it in a `Closed<Ts...>`, or moving it to the heap is forbidden. Calls MUST require possession of the token, and scope MUST determine which operations are legal.

### B. Proof-of-Fact Tokens (Move-Only)

When a token represents a completed event, a fulfilled prerequisite, or a consumed resource that must be stored or transported through asynchronous core logic, it MUST be a Proof-of-Fact Token. 

Proof-of-Fact Tokens are move-only. Copy construction and copy assignment MUST be deleted. Unlike Authority Tokens, they MAY be stored in member variables, returned by value, or placed inside `Closed<Ts...>` as long as static analysis prevents use-after-move. 

### C. Token Naming

Tokens MUST be named for the exact authority they grant or fact they prove, not for the subsystem they happen to touch. Use `NetworkEgressGrant` (Authority) or `ConfigValidationReceipt` (Proof) rather than generic nouns such as `NetworkClient` or `ValidatorResult`.

When modeling policies that eventually yield authority, do not store the authority token itself. Store the policy configuration, and evaluate the policy dynamically to mint the stack-local authority.

Valid:

```cpp
struct EgressDenied final {};
struct EgressPermitted final { std::string remote_host; }; // Stores policy intent, not the token

using NetworkEgressPolicy = Closed<EgressDenied, EgressPermitted>;
```

Invalid:

```cpp
// Category Error: Storing stack-local authority inside a variant wrapper
struct EgressPermitted final { NetworkEgressGrant grant; };
using NetworkEgressPolicy = Closed<EgressDenied, EgressPermitted>;
```

### D. Token Consumption and Recovery on Retryable Failure

Tokens enforce different consumption rules based on their category:

1. **Authority Tokens are Borrowed:** Because Authority Tokens are non-movable stack locals, they are passed by `const&`. They act as ambient authority within their scope and are mechanically immune to consumption. If an operation fails, the caller retains the token on the stack. No "recovery" mechanism is necessary or conceptually possible.
2. **Proof-of-Fact Tokens are Affine:** Because Proof Tokens represent consumed resources or unique runtime facts, they are move-only. They are passed by `&&` and are definitively consumed by the callee. Static analysis MUST flag any use-after-move by the caller.

If an operation consumes an affine Proof Token and fails in a retryable way, the failure path MUST return the recoverable token only when absolutely zero side effects occurred.

If any side effect occurred, even partial, the consumed token MUST NOT be returned.

Recovery outcome APIs MUST be `[[nodiscard]]`.

Do not return a raw outcome struct representing suspension. Return a suspended continuation object that encapsulates the state of the failed operation. Because Authority Tokens are held by the scoped guard and passed by reference, the continuation does not hold the token itself.

A suspended continuation:

- MUST NOT be copyable,
- MUST retain the original target and intent (the state of the operation),
- MUST allow `retry(Token const&)` to be called by the guard, requiring the guard to pass its stack-local authority again, and
- MAY allow `abort() &&` or destruction to safely discard the operation state when no irreversible side effects occurred.

On success, permanent failure, or any partial side effect, the affine token is permanently consumed.

## X. Boundary Discipline

Boundaries are the immediate deserialization layer, FFI adapter, CLI/environment decoder, and external API handler. Boundaries translate foreign ambiguity into strict internal types.

`std::optional`, `bool`, parse failures, nullable pointers, foreign schemas, stringly typed discriminators, integerly typed enums, and loosely typed containers MUST be collapsed in the exact boundary function that first receives them.

Boundary and core types MUST NOT contain pseudo-optionals or bare booleans. Constructors MUST yield fully valid values.

Passing raw optionality or booleans through multiple boundary functions spreads ambiguity. They MUST be eliminated in the first boundary function that touches them.

For configuration, explicit operational modes such as `OfflineOnly | ProviderBacked(T)` MUST be used instead of `std::optional<T>` fields or `enabled: bool`.

For policy and authority, finite ordered variants plus total merge rules MUST be used. Wildcard bypass variants and `allow_all`-style flags are forbidden.

### A. Deserialization Mandate

External formats such as JSON, TOML, YAML, protobuf messages, CLI flags, environment variables, and FFI payloads are inherently loose. That looseness MUST NOT survive parsing.

`from_json`, parser callbacks, generated-client adapters, custom deserializers, explicit conversion functions, and smart constructors MUST be used as needed so that parsing yields fully inhabited internal values immediately.

If a field is missing, parsing MUST fail. If a domain requires a starting state when external input provides nothing, that starting state MUST be a named variant or a named factory with no parameters — structurally visible in the type system, not silently injected by the parser. The parser does not make policy decisions. The parser rejects or translates. It does not invent.

The domain type is the schema.

## XI. Module Privacy and Construction Control

If an invariant cannot be expressed in the public type signature alone, construction MUST be restricted by module privacy.

Private constructors, friend factories, sealed concepts, and smart constructors such as `static std::expected<Self, Error> create(...)` MUST guarantee that downstream code cannot construct invalid values.

Private proof fields, hidden tags, and friend-only minting are all acceptable. Public construction of invalid states is not.

### A. Construction Escape Hatches

Invariant-bearing repo-defined types MUST NOT be aggregates. They MUST NOT have public data members that permit brace initialization or designated initialization to bypass validation.

Default constructors MUST be deleted. If a type needs a starting state, that state MUST be constructed through a named factory that makes the initial condition explicit. A default constructor hides provenance.

Single-argument constructors MUST be `explicit`.

Conversion operators MUST be deleted. `operator bool` reintroduces truthiness testing into a type system that bans booleans. If a type needs to expose a binary distinction, that distinction MUST be a named method returning a scoped enum, not an implicit conversion.

Default member initializers MUST NOT manufacture placeholders, sentinels, or partially initialized objects.

Default-inserting container APIs such as `map::operator[]`, `unordered_map::operator[]`, `resize`, bulk value-initialization, and equivalent facilities MUST NOT be used to create invariant-bearing objects. Use `emplace`, `try_emplace`, or explicit factory construction instead.

Bitwise initialization and representation tricks are forbidden in core logic. `memset`, `calloc`, byte-wise zeroing, union punning, `reinterpret_cast`, `const_cast`, C-style casts, placement `new` outside explicitly named low-level substrate modules, `void*`, `std::any`, and open-ended type erasure in core logic are forbidden.

## XII. Assertions and Panic-Based Design

`assert()`, `std::terminate()`, `.value()`, `.value_or()`, unchecked downcasts, and panic-style crash points do not prevent invalid states. They detect invalid states that the type design already permitted.

In core logic, these are signs that the representation is too weak. Strengthen the representation instead.

## XIII. C++ IFA Mechanisms

Because C++ lacks dependent types and true linear ownership, the architecture dictates explicit mechanisms to replace ambiguous constructs:

| Architectural need | Standard C++ escape hatch | Strict replacement |
|---|---|---|
| Control flow | Stored `std::expected`, exception-driven control flow, or deferred result inspection | Boundary-local `std::expected`, CPS / Church Encoding, or immediate exhaustive dispatch |
| Exclusive states | Tag enums plus partially meaningful fields, nullable members, or base-class polymorphism | Distinct inhabited structs or sealed `Closed<Ts...>` |
| Heterogeneous collections | `std::variant`, `std::any`, RTTI, `void*` | Sealed repo-owned `Closed<Ts...>` with exhaustive `match()` only |
| Lifecycle transitions | In-place retagging, partially initialized objects, zombie moved-from values | Typestate with consuming `&&` transitions, deleted copy, `[[nodiscard]]`, and use-after-move analysis |
| Shared coordination | Shared ownership plus locks or lock registries | One owner plus typed handles; narrow runtime-shell coordination exception only |
| Construction | Aggregates, implicit conversions, default insertion, sentinel defaults | Private constructors, explicit factories, proof-bearing types |
| Enum validity | `static_cast` from integers, zero-init, unchecked deserialization | Validating boundary conversion, wrapped enums, or split inhabited types |
| Storage | Flags and colocated wrappers for state | State as location with distinct containers per state |

If C++ cannot forbid an invalid state completely, the interface MUST still make that state difficult to construct and impossible to use accidentally. Imperfect enforcement requires a harder interface, not a weaker one.

## XIV. Quick Review Test

During review, do not ask, "What happens if someone passes the wrong data?"

Ask, "How do I make passing the wrong data a compile error?"

Compile-time friction is cheaper than runtime desynchronization. If the type system resists the design, redesign the architecture.

## XV. Disallowed Patterns

| Pattern | Why it is forbidden |
|---|---|
| `std::optional<T>` fields, pseudo-optionals, or `Closed<std::monostate, T>` | Encodes absence instead of a domain fact |
| False optionality (`std::optional<T>` or `std::expected<T, Missing>` when `T` is required) | Lies about the contract |
| Bare `bool` fields or parameters | Compresses meaning into unlabeled bits |
| Wildcard policy variants (`Any`, `All`, `Unrestricted`, `AllowAll`) | Creates a universal escape hatch |
| `std::variant<Ts...>` | Contains architectural escape hatches and bypass observation APIs |
| Base-class hierarchies or `std::unique_ptr<Base>` for a closed alternative set | Reopens a closed world and hides exhaustiveness |
| Sentinel values and placeholder defaults | Uses in-band signaling instead of types |
| Tag enums plus partially relevant fields | Multiplies invalid states |
| Two-phase initialization | Exposes partially initialized state |
| Aggregate construction of invariant-bearing types | Bypasses validation |
| `.value()`, `.value_or()`, `assert()`, or crash points in core logic | Replaces type obligations with runtime panic |
| `std::shared_ptr<std::mutex>`, `std::shared_mutex`, or global lock registries as domain architecture | Fragments ownership and creates consensus problems |
| `dynamic_cast`, `typeid`, `std::any`, `void*`, or RTTI-driven branching in core logic | Replaces closed modeling with runtime guessing |
| Default insertion of invariant-bearing objects via container APIs | Manufactures placeholder state |
| `std::expected<void, E>` | `void` success carries no information; name the success with a proof type or receipt |
| `friend class` on typestate or token types | Grants unbounded construction authority; use `friend` functions scoped to specific transitions |
| Copyable `Closed<Ts...>` | Copy permits forked state; copy MUST be deleted |
| Movable capability tokens | Permits storage beyond phase scope; tokens MUST be non-copyable and non-movable |
| Public constructors on phase marker tags | Permits downstream code to forge phases; tag constructors MUST be private |
| Silent default injection by parsers | Parser invents values instead of failing; the parser rejects or translates, it does not invent |

## XVI. Final Standard

The standard is a codebase where successful compilation is evidence that invalid states, illegal transitions, and unauthorized operations have been designed out of the reachable program.