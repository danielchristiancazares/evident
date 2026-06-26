#pragma once

#include "evident/Ast.hpp"
#include "evident/Diagnostic.hpp"
#include "evident/Source.hpp"
#include "evident/Token.hpp"

#include <optional>
#include <vector>

namespace evident {

class Parser {
public:
    Parser(const SourceFile& source, std::vector<Token> tokens, DiagnosticSink& diagnostics);
    [[nodiscard]] ast::TranslationUnit parse();

private:
    [[nodiscard]] const Token& peek(std::size_t lookahead = 0) const;
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] bool check(TokenKind kind) const noexcept;
    [[nodiscard]] bool check_any(std::initializer_list<TokenKind> kinds) const noexcept;
    const Token& advance();
    [[nodiscard]] bool match(TokenKind kind);
    [[nodiscard]] bool match_any(std::initializer_list<TokenKind> kinds);
    const Token& expect(TokenKind kind, std::string_view message);

    void synchronize();
    ast::Visibility parse_visibility();
    std::optional<ast::ImportDecl> parse_import();
    std::unique_ptr<ast::Decl> parse_top_level_decl();
    std::unique_ptr<ast::Decl> parse_decl();
    std::unique_ptr<ast::ModuleDecl> parse_module(ast::Visibility visibility);
    std::unique_ptr<ast::RecordDecl> parse_record(ast::Visibility visibility);
    std::unique_ptr<ast::StateDecl> parse_state(ast::Visibility visibility);
    std::unique_ptr<ast::ReasonDecl> parse_reason(ast::Visibility visibility);
    std::unique_ptr<ast::ProofDecl> parse_proof(ast::Visibility visibility);
    std::unique_ptr<ast::PermitDecl> parse_permit(ast::Visibility visibility);
    std::unique_ptr<ast::PhaseDecl> parse_phase(ast::Visibility visibility);
    std::unique_ptr<ast::FunctionDecl> parse_function(ast::Visibility visibility, bool is_foreign);

    std::vector<ast::GenericParam> parse_generic_params();
    std::vector<ast::Field> parse_field_block();
    std::vector<ast::Variant> parse_variant_block();
    std::vector<ast::Parameter> parse_parameter_list();
    std::vector<ast::Parameter> parse_foreign_parameter_list();
    ast::FunctionSignature parse_function_signature(std::string name);
    ast::FunctionSignature parse_foreign_function_signature(std::string name);
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
    [[nodiscard]] bool begins_expr(TokenKind kind) const noexcept;
    [[nodiscard]] bool looks_like_record_initializer() const noexcept;
    [[nodiscard]] bool is_module_kind_keyword(TokenKind kind) const noexcept;

    const SourceFile& source_;
    std::vector<Token> tokens_;
    DiagnosticSink& diagnostics_;
    std::size_t current_ = 0;
};

} // namespace evident
