#pragma once

#include "evident/Ast.hpp"
#include "evident/ResolvedType.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace evident::hir {

using TypeId = std::size_t;
using VariantId = std::size_t;
using FunctionId = std::size_t;

enum class TypeKind {
    Record,
    State,
    Reason,
    Proof,
    Permit,
    Phase,
};

enum class ExprKind {
    NumberLiteral,
    StringLiteral,
    Unit,
    LocalRef,
    Call,
    Construct,
    Try,
    Match,
    Block,
    Fail,
    Grant,
    Prove,
    FieldAccess,
    Traverse,
};

enum class StatementKind {
    Let,
    Expr,
};

enum class PatternKind {
    Variant,
    Succeeded,
    Failed,
};

enum class ConstructKind {
    Record,
    Proof,
    StateVariant,
    ReasonVariant,
    Unit,
};

enum class TypeIdentitySource {
    TypeFlavor,
    PackageTypeDeclaration,
};

class TypeIdentity final {
public:
    [[nodiscard]] static TypeIdentity from_type_flavor();
    [[nodiscard]] static TypeIdentity from_package_type(TypeId type_id);

    [[nodiscard]] TypeIdentitySource source() const noexcept { return source_; }
    [[nodiscard]] TypeId package_type_id() const noexcept { return package_type_id_; }

private:
    TypeIdentitySource source_;
    TypeId package_type_id_;

    TypeIdentity(TypeIdentitySource source, TypeId package_type_id);
};

struct TypeRef {
    std::string text = "<error>";
    TypeIdentity identity = TypeIdentity::from_type_flavor();
    typesys::TypeFlavor flavor = typesys::TypeFlavor::Error;
    typesys::UseDiscipline discipline = typesys::UseDiscipline::Copyable;
    std::vector<TypeRef> args;
};

struct FieldDecl {
    std::string name;
    TypeRef type;
};

struct VariantDecl {
    VariantId id = 0;
    TypeId owner_type = 0;
    std::string qualified_name;
    std::string name;
    std::vector<FieldDecl> fields;
};

enum class TypeDisciplineResolutionState {
    ConcreteUseDisciplineKnown,
    RequiresConcreteInstantiation,
};

class TypeDisciplineResolution final {
public:
    [[nodiscard]] static TypeDisciplineResolution requires_concrete_instantiation();
    [[nodiscard]] static TypeDisciplineResolution concrete_use_discipline(typesys::UseDiscipline discipline);

    [[nodiscard]] TypeDisciplineResolutionState state() const noexcept { return state_; }
    [[nodiscard]] typesys::UseDiscipline discipline() const noexcept { return discipline_; }

private:
    TypeDisciplineResolutionState state_;
    typesys::UseDiscipline discipline_;

    TypeDisciplineResolution(TypeDisciplineResolutionState state, typesys::UseDiscipline discipline);
};

struct TypeDecl {
    TypeId id = 0;
    TypeKind kind = TypeKind::Record;
    ast::Visibility visibility = ast::Visibility::Private;
    std::string qualified_name;
    TypeDisciplineResolution discipline = TypeDisciplineResolution::requires_concrete_instantiation();
    std::vector<std::string> generics;
    std::vector<FieldDecl> fields;
    std::vector<VariantId> variants;
};

struct Parameter {
    std::string name;
    TypeRef type;
    ast::ParameterAuthority authority = ast::ParameterAuthority::OrdinaryValue;
};

enum class FunctionFailureBehavior {
    ReturnsDeclaredValue,
    YieldsReason,
};

enum class FunctionAuthorityEffect {
    OrdinaryCall,
    GrantsScopedPermit,
};

class FunctionFailureContract final {
public:
    [[nodiscard]] static FunctionFailureContract returns_declared_value();
    [[nodiscard]] static FunctionFailureContract yields_reason(TypeId reason_type_id);

    [[nodiscard]] FunctionFailureBehavior behavior() const noexcept { return behavior_; }
    [[nodiscard]] TypeId reason_type_id() const noexcept { return reason_type_id_; }

private:
    FunctionFailureBehavior behavior_;
    TypeId reason_type_id_;

    FunctionFailureContract(FunctionFailureBehavior behavior, TypeId reason_type_id);
};

class FunctionAuthorityContract final {
public:
    [[nodiscard]] static FunctionAuthorityContract ordinary_call();
    [[nodiscard]] static FunctionAuthorityContract grants_scoped_permit(TypeId permit_type_id);

    [[nodiscard]] FunctionAuthorityEffect effect() const noexcept { return effect_; }
    [[nodiscard]] TypeId permit_type_id() const noexcept { return permit_type_id_; }

private:
    FunctionAuthorityEffect effect_;
    TypeId permit_type_id_;

    FunctionAuthorityContract(FunctionAuthorityEffect effect, TypeId permit_type_id);
};

enum class ExprFailureEffect {
    ReturnsValueOnly,
    YieldsReason,
};

class ExprFailureContract final {
public:
    [[nodiscard]] static ExprFailureContract returns_value_only();
    [[nodiscard]] static ExprFailureContract yields_reason(TypeId reason_type_id);

    [[nodiscard]] ExprFailureEffect effect() const noexcept { return effect_; }
    [[nodiscard]] TypeId reason_type_id() const noexcept { return reason_type_id_; }

private:
    ExprFailureEffect effect_;
    TypeId reason_type_id_;

    ExprFailureContract(ExprFailureEffect effect, TypeId reason_type_id);
};

struct Binding {
    std::string field_name;
    std::string binding_name;
};

struct Expr;

struct FieldInit {
    std::string name;
    std::unique_ptr<Expr> value;
    ast::FieldInitSpelling spelling = ast::FieldInitSpelling::ExplicitValue;
};

struct Expr {
    ExprKind kind = ExprKind::Unit;
    TypeRef result_type;
    ExprFailureContract failure = ExprFailureContract::returns_value_only();

    explicit Expr(ExprKind expr_kind, TypeRef type = {})
        : kind(expr_kind), result_type(std::move(type)) {}
    virtual ~Expr() = default;
};

struct NumberLiteralExpr final : Expr {
    std::string lexeme;

    explicit NumberLiteralExpr(std::string value, TypeRef type = {})
        : Expr(ExprKind::NumberLiteral, std::move(type)), lexeme(std::move(value)) {}
};

struct StringLiteralExpr final : Expr {
    std::string lexeme;

    explicit StringLiteralExpr(std::string value, TypeRef type = {})
        : Expr(ExprKind::StringLiteral, std::move(type)), lexeme(std::move(value)) {}
};

struct UnitExpr final : Expr {
    explicit UnitExpr(TypeRef type = {})
        : Expr(ExprKind::Unit, std::move(type)) {}
};

struct LocalRefExpr final : Expr {
    std::string name;

    explicit LocalRefExpr(std::string local_name, TypeRef type = {})
        : Expr(ExprKind::LocalRef, std::move(type)), name(std::move(local_name)) {}
};

struct CallExpr final : Expr {
    FunctionId function_id = 0;
    std::string callee_name;
    std::vector<TypeRef> type_args;
    std::vector<std::unique_ptr<Expr>> args;

    explicit CallExpr(TypeRef type = {})
        : Expr(ExprKind::Call, std::move(type)) {}
};

enum class ConstructVariantTargetState {
    ConstructsNamedType,
    ConstructsNamedVariant,
};

class ConstructVariantTarget final {
public:
    [[nodiscard]] static ConstructVariantTarget constructs_named_type();
    [[nodiscard]] static ConstructVariantTarget constructs_named_variant(VariantId variant_id);

    [[nodiscard]] ConstructVariantTargetState state() const noexcept { return state_; }
    [[nodiscard]] VariantId variant_id() const noexcept { return variant_id_; }

private:
    ConstructVariantTargetState state_;
    VariantId variant_id_;

    ConstructVariantTarget(ConstructVariantTargetState state, VariantId variant_id);
};

struct ConstructExpr final : Expr {
    ConstructKind construct_kind = ConstructKind::Record;
    TypeId owner_type_id = 0;
    ConstructVariantTarget variant_target = ConstructVariantTarget::constructs_named_type();
    std::string qualified_name;
    std::vector<FieldInit> fields;

    explicit ConstructExpr(TypeRef type = {})
        : Expr(ExprKind::Construct, std::move(type)) {}
};

struct TryExpr final : Expr {
    std::unique_ptr<Expr> operand;

    explicit TryExpr(TypeRef type = {})
        : Expr(ExprKind::Try, std::move(type)) {}
};

struct Pattern {
    PatternKind kind = PatternKind::Variant;

    explicit Pattern(PatternKind pattern_kind)
        : kind(pattern_kind) {}
    virtual ~Pattern() = default;
};

struct VariantPattern final : Pattern {
    enum class PayloadMode {
        None,
        Bindings,
        Ignore,
    };

    TypeId owner_type_id = 0;
    VariantId variant_id = 0;
    std::string owner_name;
    std::string variant_name;
    PayloadMode payload_mode = PayloadMode::None;
    std::vector<Binding> bindings;

    VariantPattern()
        : Pattern(PatternKind::Variant) {}
};

struct SucceededPattern final : Pattern {
    ast::SuccessPatternBinding binding = ast::SuccessPatternBinding::DiscardedValue;
    std::string binding_name;

    SucceededPattern()
        : Pattern(PatternKind::Succeeded) {}
};

struct FailedPattern final : Pattern {
    std::unique_ptr<VariantPattern> variant;

    FailedPattern()
        : Pattern(PatternKind::Failed) {}
};

struct MatchArm {
    std::unique_ptr<Pattern> pattern;
    std::unique_ptr<Expr> body;
};

struct MatchExpr final : Expr {
    std::unique_ptr<Expr> scrutinee;
    std::vector<MatchArm> arms;

    explicit MatchExpr(TypeRef type = {})
        : Expr(ExprKind::Match, std::move(type)) {}
};

struct Stmt {
    StatementKind kind = StatementKind::Expr;

    explicit Stmt(StatementKind stmt_kind)
        : kind(stmt_kind) {}
    virtual ~Stmt() = default;
};

struct LetStmt final : Stmt {
    std::string name;
    TypeRef type;
    std::unique_ptr<Expr> initializer;

    LetStmt()
        : Stmt(StatementKind::Let) {}
};

struct ExprStmt final : Stmt {
    std::unique_ptr<Expr> expr;

    ExprStmt()
        : Stmt(StatementKind::Expr) {}
};

struct BlockExpr final : Expr {
    std::vector<std::unique_ptr<Stmt>> statements;
    std::unique_ptr<Expr> result;

    explicit BlockExpr(TypeRef type = {})
        : Expr(ExprKind::Block, std::move(type)) {}
};

struct FailExpr final : Expr {
    TypeId reason_type_id = 0;
    VariantId variant_id = 0;
    std::string reason_name;
    std::string variant_name;
    std::vector<FieldInit> fields;

    explicit FailExpr(TypeRef type = {})
        : Expr(ExprKind::Fail, std::move(type)) {}
};

struct GrantExpr final : Expr {
    FunctionId grant_function_id = 0;
    TypeId permit_type_id = 0;
    std::string grant_name;
    std::string permit_name;
    std::string binder_name;
    std::vector<std::unique_ptr<Expr>> args;
    std::unique_ptr<BlockExpr> body;

    explicit GrantExpr(TypeRef type = {})
        : Expr(ExprKind::Grant, std::move(type)) {}
};

struct FieldAccessExpr final : Expr {
    std::unique_ptr<Expr> object;
    std::string field_name;

    explicit FieldAccessExpr(TypeRef type = {})
        : Expr(ExprKind::FieldAccess, std::move(type)) {}
};

struct ProveExpr final : Expr {
    TypeId proof_type_id = 0;
    std::string qualified_name;
    std::vector<FieldInit> fields;

    explicit ProveExpr(TypeRef type = {})
        : Expr(ExprKind::Prove, std::move(type)) {}
};

struct TraverseExpr final : Expr {
    ast::TraversalMode mode = ast::TraversalMode::Copying;
    std::unique_ptr<Expr> source;
    std::string element_name;
    TypeRef element_type;
    std::string accumulator_name;
    TypeRef accumulator_type;
    std::unique_ptr<Expr> initial_accumulator;
    std::unique_ptr<BlockExpr> body;

    explicit TraverseExpr(TypeRef type = {})
        : Expr(ExprKind::Traverse, std::move(type)) {}
};

struct FunctionDecl {
    FunctionId id = 0;
    ast::Visibility visibility = ast::Visibility::Private;
    std::string qualified_name;
    std::vector<std::string> generics;
    std::vector<Parameter> params;
    TypeRef return_type;
    FunctionFailureContract failure = FunctionFailureContract::returns_declared_value();
    FunctionAuthorityContract authority = FunctionAuthorityContract::ordinary_call();
    std::vector<TypeId> proves_proof_type_ids;
    ast::FunctionImplementation implementation = ast::FunctionImplementation::EvidentBody;
    std::unique_ptr<BlockExpr> body;
};

struct Package {
    std::vector<TypeDecl> types;
    std::vector<VariantDecl> variants;
    std::vector<FunctionDecl> functions;
};

[[nodiscard]] Package lower(const ast::TranslationUnit& unit);
[[nodiscard]] Package monomorphize_for_backend(const Package& package);
[[nodiscard]] std::string dump(const Package& package);
[[nodiscard]] std::string emit_stub_backend(const Package& package);
[[nodiscard]] const TypeDecl& lookup_type(const Package& package, TypeId id);
[[nodiscard]] const VariantDecl& lookup_variant(const Package& package, VariantId id);
[[nodiscard]] const FunctionDecl& lookup_function(const Package& package, FunctionId id);
[[nodiscard]] std::string type_kind_name(TypeKind kind);

} // namespace evident::hir
