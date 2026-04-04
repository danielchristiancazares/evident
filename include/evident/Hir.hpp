#pragma once

#include "evident/Ast.hpp"
#include "evident/ResolvedType.hpp"

#include <cstddef>
#include <memory>
#include <optional>
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

struct TypeRef {
    std::string text;
    std::optional<TypeId> type_id;
    bool is_builtin = false;
    bool is_generic = false;
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

struct TypeDecl {
    TypeId id = 0;
    TypeKind kind = TypeKind::Record;
    ast::Visibility visibility = ast::Visibility::Private;
    std::string qualified_name;
    std::optional<typesys::UseDiscipline> concrete_discipline;
    std::vector<std::string> generics;
    std::vector<FieldDecl> fields;
    std::vector<VariantId> variants;
};

struct Parameter {
    std::string name;
    TypeRef type;
    bool is_compile_time_only = false;
    bool is_permit_param = false;
};

struct Binding {
    std::string field_name;
    std::string binding_name;
};

struct Expr;

struct FieldInit {
    std::string name;
    std::unique_ptr<Expr> value;
    bool shorthand = false;
};

struct Expr {
    ExprKind kind = ExprKind::Unit;
    TypeRef result_type;
    std::optional<TypeId> fails_reason_type_id;

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
    std::vector<std::unique_ptr<Expr>> args;

    explicit CallExpr(TypeRef type = {})
        : Expr(ExprKind::Call, std::move(type)) {}
};

struct ConstructExpr final : Expr {
    ConstructKind construct_kind = ConstructKind::Record;
    TypeId owner_type_id = 0;
    std::optional<VariantId> variant_id;
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
    std::optional<std::string> binding_name;
    bool ignore = false;

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

struct FunctionDecl {
    FunctionId id = 0;
    ast::Visibility visibility = ast::Visibility::Private;
    std::string qualified_name;
    std::vector<std::string> generics;
    std::vector<Parameter> params;
    TypeRef return_type;
    std::optional<TypeId> fails_reason_type_id;
    std::optional<TypeId> grants_permit_type_id;
    std::vector<TypeId> proves_proof_type_ids;
    bool is_foreign = false;
    std::unique_ptr<BlockExpr> body;
};

struct Package {
    std::vector<TypeDecl> types;
    std::vector<VariantDecl> variants;
    std::vector<FunctionDecl> functions;
};

[[nodiscard]] Package lower(const ast::TranslationUnit& unit);
[[nodiscard]] std::string dump(const Package& package);
[[nodiscard]] std::string emit_stub_backend(const Package& package);
[[nodiscard]] const TypeDecl& lookup_type(const Package& package, TypeId id);
[[nodiscard]] const VariantDecl& lookup_variant(const Package& package, VariantId id);
[[nodiscard]] const FunctionDecl& lookup_function(const Package& package, FunctionId id);
[[nodiscard]] std::string type_kind_name(TypeKind kind);

} // namespace evident::hir
