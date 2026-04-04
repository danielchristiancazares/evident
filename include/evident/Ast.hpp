#pragma once

#include "evident/Source.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evident::ast {

enum class Visibility {
    Private,
    Public,
};

enum class ModuleKind {
    Domain,
    Boundary,
    Foundation,
    Hazard,
};

enum class DeclKind {
    Module,
    Record,
    State,
    Reason,
    Proof,
    Permit,
    Phase,
    Function,
    ForeignFunction,
};

enum class ExprKind {
    NumberLiteral,
    StringLiteral,
    Path,
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

enum class StmtKind {
    Let,
    Expr,
};

enum class PatternKind {
    Variant,
    Succeeded,
    Failed,
};

struct Node {
    SourceSpan span{};
    virtual ~Node() = default;
};

struct TypeRef {
    std::vector<std::string> path;
    std::vector<TypeRef> args;
    SourceSpan span{};
};

struct GenericParam {
    std::string name;
    SourceSpan span{};
};

struct Field {
    Visibility visibility = Visibility::Private;
    std::string name;
    TypeRef type;
    SourceSpan span{};
};

struct Variant {
    std::string name;
    std::vector<Field> fields;
    SourceSpan span{};
};

struct Parameter {
    std::string name;
    TypeRef type;
    bool is_permit_param = false;
    SourceSpan span{};
};

struct FunctionSignature {
    std::string name;
    std::vector<GenericParam> generic_params;
    std::vector<Parameter> params;
    TypeRef return_type;
    std::optional<TypeRef> fails_type;
    std::optional<TypeRef> grants_type;
    std::vector<TypeRef> proves_types;
    SourceSpan span{};
};

struct Expr;
struct Pattern;
struct Stmt;

struct RecordFieldInit {
    std::string name;
    std::unique_ptr<Expr> value;
    bool shorthand = false;
    SourceSpan span{};
};

struct Expr : Node {
    ExprKind kind;

    explicit Expr(ExprKind expr_kind)
        : kind(expr_kind) {}
    ~Expr() override = default;
};

struct NumberLiteralExpr final : Expr {
    std::string lexeme;

    explicit NumberLiteralExpr(std::string value)
        : Expr(ExprKind::NumberLiteral), lexeme(std::move(value)) {}
};

struct StringLiteralExpr final : Expr {
    std::string lexeme;

    explicit StringLiteralExpr(std::string value)
        : Expr(ExprKind::StringLiteral), lexeme(std::move(value)) {}
};

struct PathExpr final : Expr {
    std::vector<std::string> path;

    PathExpr()
        : Expr(ExprKind::Path) {}
};

struct CallExpr final : Expr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;

    CallExpr()
        : Expr(ExprKind::Call) {}
};

struct ConstructExpr final : Expr {
    std::vector<std::string> path;
    std::vector<RecordFieldInit> fields;

    ConstructExpr()
        : Expr(ExprKind::Construct) {}
};

struct TryExpr final : Expr {
    std::unique_ptr<Expr> operand;

    TryExpr()
        : Expr(ExprKind::Try) {}
};

struct FieldAccessExpr final : Expr {
    std::unique_ptr<Expr> object;
    std::string field_name;

    FieldAccessExpr()
        : Expr(ExprKind::FieldAccess) {}
};

struct Pattern : Node {
    PatternKind kind;

    explicit Pattern(PatternKind pattern_kind)
        : kind(pattern_kind) {}
    ~Pattern() override = default;
};

struct VariantPattern final : Pattern {
    enum class PayloadMode {
        None,
        Bindings,
        Ignore,
    };

    struct Binding {
        std::string field_name;
        std::string binding_name;
        SourceSpan span{};
    };

    std::vector<std::string> path;
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
    SourceSpan span{};
};

struct MatchExpr final : Expr {
    std::unique_ptr<Expr> scrutinee;
    std::vector<MatchArm> arms;

    MatchExpr()
        : Expr(ExprKind::Match) {}
};

struct Stmt : Node {
    StmtKind kind;

    explicit Stmt(StmtKind stmt_kind)
        : kind(stmt_kind) {}
    ~Stmt() override = default;
};

struct LetStmt final : Stmt {
    std::string name;
    std::unique_ptr<Expr> initializer;

    LetStmt()
        : Stmt(StmtKind::Let) {}
};

struct ExprStmt final : Stmt {
    std::unique_ptr<Expr> expr;

    ExprStmt()
        : Stmt(StmtKind::Expr) {}
};

struct BlockExpr final : Expr {
    std::vector<std::unique_ptr<Stmt>> statements;
    std::unique_ptr<Expr> result;

    BlockExpr()
        : Expr(ExprKind::Block) {}
};

struct FailExpr final : Expr {
    std::vector<std::string> path;
    std::vector<RecordFieldInit> fields;

    FailExpr()
        : Expr(ExprKind::Fail) {}
};

struct GrantExpr final : Expr {
    std::unique_ptr<Expr> grant_call;
    std::string binder_name;
    std::unique_ptr<BlockExpr> body;

    GrantExpr()
        : Expr(ExprKind::Grant) {}
};

struct ProveExpr final : Expr {
    std::vector<std::string> path;
    std::vector<RecordFieldInit> fields;

    ProveExpr()
        : Expr(ExprKind::Prove) {}
};

struct Decl : Node {
    DeclKind kind = DeclKind::Record;
    Visibility visibility = Visibility::Private;
    std::string name;

    Decl(DeclKind decl_kind, Visibility decl_visibility, std::string decl_name)
        : kind(decl_kind), visibility(decl_visibility), name(std::move(decl_name)) {}
};

struct ModuleDecl final : Decl {
    ModuleKind module_kind = ModuleKind::Domain;
    std::vector<std::unique_ptr<Decl>> members;

    explicit ModuleDecl(Visibility visibility, std::string name, ModuleKind mk)
        : Decl(DeclKind::Module, visibility, std::move(name)), module_kind(mk) {}
};

struct RecordDecl final : Decl {
    std::vector<GenericParam> generic_params;
    std::vector<Field> fields;

    explicit RecordDecl(Visibility visibility, std::string name)
        : Decl(DeclKind::Record, visibility, std::move(name)) {}
};

struct StateDecl final : Decl {
    std::vector<Variant> variants;

    explicit StateDecl(Visibility visibility, std::string name)
        : Decl(DeclKind::State, visibility, std::move(name)) {}
};

struct ReasonDecl final : Decl {
    std::vector<Variant> variants;

    explicit ReasonDecl(Visibility visibility, std::string name)
        : Decl(DeclKind::Reason, visibility, std::move(name)) {}
};

struct ProofDecl final : Decl {
    std::vector<Field> fields;

    explicit ProofDecl(Visibility visibility, std::string name)
        : Decl(DeclKind::Proof, visibility, std::move(name)) {}
};

struct PermitDecl final : Decl {
    explicit PermitDecl(Visibility visibility, std::string name)
        : Decl(DeclKind::Permit, visibility, std::move(name)) {}
};

struct PhaseDecl final : Decl {
    std::vector<Field> fields;
    std::vector<std::string> positions;
    std::vector<SourceSpan> position_spans;

    explicit PhaseDecl(Visibility visibility, std::string name)
        : Decl(DeclKind::Phase, visibility, std::move(name)) {}
};

struct FunctionDecl final : Decl {
    FunctionSignature signature;
    std::unique_ptr<BlockExpr> body;
    std::optional<SourceSpan> body_span;

    explicit FunctionDecl(Visibility visibility, std::string name, bool is_foreign)
        : Decl(is_foreign ? DeclKind::ForeignFunction : DeclKind::Function,
               visibility,
               std::move(name)) {}

    [[nodiscard]] bool is_foreign() const noexcept { return kind == DeclKind::ForeignFunction; }
};

struct TranslationUnit {
    std::vector<std::unique_ptr<Decl>> decls;
};

[[nodiscard]] std::string_view decl_kind_name(DeclKind kind);
[[nodiscard]] std::string_view module_kind_name(ModuleKind kind);
[[nodiscard]] std::string visibility_name(Visibility visibility);
[[nodiscard]] std::string format_type(const TypeRef& type);
std::string dump(const TranslationUnit& unit, std::string_view source_text = {});

} // namespace evident::ast
