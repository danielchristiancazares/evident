#pragma once

#include "evident/Ast.hpp"
#include "evident/Diagnostic.hpp"
#include "evident/Source.hpp"
#include "evident/Token.hpp"

#include <vector>

namespace evident {

enum class ParserCursorState {
    HasMoreTokens,
    ReachedEnd,
};

enum class TokenCheckState {
    DifferentToken,
    Matches,
};

enum class TokenConsumptionState {
    NotConsumed,
    Consumed,
};

enum class ExpressionStartState {
    DoesNotBeginExpression,
    BeginsExpression,
};

enum class RecordInitializerLookahead {
    BlockExpression,
    RecordInitializer,
};

enum class ModuleKindTokenRole {
    OrdinaryToken,
    ModuleKindKeyword,
};

enum class FieldBlockContext {
    DeclarationFields,
    VariantPayloadFields,
};

class Parser {
public:
    Parser(const SourceFile& source, std::vector<Token> tokens, DiagnosticSink& diagnostics);
    [[nodiscard]] ast::TranslationUnit parse();

private:
    [[nodiscard]] const Token& peek(std::size_t lookahead = 0) const;
    [[nodiscard]] ParserCursorState cursor_state() const noexcept;
    [[nodiscard]] TokenCheckState token_check(TokenKind kind) const noexcept;
    [[nodiscard]] TokenCheckState token_check_any(std::initializer_list<TokenKind> kinds) const noexcept;
    const Token& advance();
    [[nodiscard]] TokenConsumptionState consume_if(TokenKind kind);
    [[nodiscard]] TokenConsumptionState consume_any(std::initializer_list<TokenKind> kinds);
    const Token& expect(TokenKind kind, std::string_view message);

    void synchronize();
    ast::Visibility parse_visibility();
    void parse_import_into(ast::TranslationUnit& unit);
    std::unique_ptr<ast::Decl> parse_top_level_decl();
    std::unique_ptr<ast::Decl> parse_decl();
    void reject_kindless_module();
    [[nodiscard]] TokenConsumptionState reject_unsupported_declaration_head();
    std::unique_ptr<ast::ModuleDecl> parse_module(ast::Visibility visibility);
    std::unique_ptr<ast::RecordDecl> parse_record(ast::Visibility visibility);
    std::unique_ptr<ast::StateDecl> parse_state(ast::Visibility visibility);
    std::unique_ptr<ast::ReasonDecl> parse_reason(ast::Visibility visibility);
    std::unique_ptr<ast::ProofDecl> parse_proof(ast::Visibility visibility);
    std::unique_ptr<ast::PermitDecl> parse_permit(ast::Visibility visibility);
    std::unique_ptr<ast::PhaseDecl> parse_phase(ast::Visibility visibility);
    std::unique_ptr<ast::FunctionDecl> parse_function(ast::Visibility visibility,
                                                      ast::FunctionImplementation implementation);

    std::vector<ast::GenericParam> parse_generic_params();
    std::vector<ast::Field> parse_field_block(FieldBlockContext context = FieldBlockContext::DeclarationFields);
    std::vector<ast::Variant> parse_variant_block();
    std::vector<ast::Parameter> parse_parameter_list();
    std::vector<ast::Parameter> parse_foreign_parameter_list();
    ast::FunctionSignature parse_function_signature(std::string name);
    ast::FunctionSignature parse_foreign_function_signature(std::string name);
    ast::TypeRef parse_return_type_after_parameter_list();
    ast::TypeRef parse_type();
    std::vector<ast::TypeRef> parse_type_argument_list();
    std::vector<std::string> parse_path();
    std::vector<ast::RecordFieldInit> parse_record_field_initializers();

    std::unique_ptr<ast::BlockExpr> parse_block_expr();
    std::unique_ptr<ast::Stmt> parse_statement();
    std::unique_ptr<ast::Expr> parse_expr();
    std::unique_ptr<ast::Expr> parse_match_expr();
    std::unique_ptr<ast::Expr> parse_grant_expr();
    std::unique_ptr<ast::Expr> parse_try_expr();
    std::unique_ptr<ast::Expr> parse_fail_expr();
    std::unique_ptr<ast::Expr> parse_prove_expr();
    std::unique_ptr<ast::Expr> parse_postfix_expr();
    std::unique_ptr<ast::Expr> parse_primary_expr();
    std::unique_ptr<ast::Pattern> parse_pattern();
    std::unique_ptr<ast::VariantPattern> parse_variant_pattern_from_path(std::vector<std::string> path, SourceSpan path_span);

    void consume_optional_declaration_terminator();
    void consume_optional_statement_terminator();
    [[nodiscard]] ExpressionStartState expression_start_state(TokenKind kind) const noexcept;
    [[nodiscard]] RecordInitializerLookahead record_initializer_lookahead() const noexcept;
    [[nodiscard]] ModuleKindTokenRole module_kind_token_role(TokenKind kind) const noexcept;

    const SourceFile& source_;
    std::vector<Token> tokens_;
    DiagnosticSink& diagnostics_;
    std::size_t current_ = 0;
};

} // namespace evident
