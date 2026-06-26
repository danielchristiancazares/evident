#include "evident/Parser.hpp"

#include <utility>

namespace evident {

namespace {

bool is_declaration_start(TokenKind kind) {
    switch (kind) {
    case TokenKind::KeywordPublic:
    case TokenKind::KeywordDomain:
    case TokenKind::KeywordBoundary:
    case TokenKind::KeywordFoundation:
    case TokenKind::KeywordHazard:
    case TokenKind::KeywordRecord:
    case TokenKind::KeywordState:
    case TokenKind::KeywordReason:
    case TokenKind::KeywordProof:
    case TokenKind::KeywordPermit:
    case TokenKind::KeywordPhase:
    case TokenKind::KeywordFn:
    case TokenKind::KeywordForeign:
        return true;
    default:
        return false;
    }
}

std::string token_text(const Token& token) {
    return std::string(token.lexeme);
}

std::unique_ptr<ast::PathExpr> make_shorthand_path_expr(const Token& token) {
    auto expr = std::make_unique<ast::PathExpr>();
    expr->path.push_back(token_text(token));
    expr->span = token.span;
    return expr;
}

} // namespace

Parser::Parser(const SourceFile& source, std::vector<Token> tokens, DiagnosticSink& diagnostics)
    : source_(source), tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

ast::TranslationUnit Parser::parse() {
    ast::TranslationUnit unit;
    while (!at_end()) {
        if (check(TokenKind::EndOfFile)) {
            break;
        }

        if (check(TokenKind::KeywordImport)) {
            if (std::optional<ast::ImportDecl> import_decl = parse_import(); import_decl.has_value()) {
                unit.imports.push_back(std::move(*import_decl));
            }
            continue;
        }

        if (std::unique_ptr<ast::Decl> decl = parse_top_level_decl()) {
            unit.decls.push_back(std::move(decl));
        } else {
            synchronize();
        }
    }
    return unit;
}

const Token& Parser::peek(std::size_t lookahead) const {
    const std::size_t index = current_ + lookahead;
    if (index >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[index];
}

bool Parser::at_end() const noexcept {
    return peek().kind == TokenKind::EndOfFile;
}

bool Parser::check(TokenKind kind) const noexcept {
    return peek().kind == kind;
}

bool Parser::check_any(std::initializer_list<TokenKind> kinds) const noexcept {
    for (TokenKind kind : kinds) {
        if (check(kind)) {
            return true;
        }
    }
    return false;
}

const Token& Parser::advance() {
    if (!at_end()) {
        ++current_;
    }
    return tokens_[current_ - 1];
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

bool Parser::match_any(std::initializer_list<TokenKind> kinds) {
    for (TokenKind kind : kinds) {
        if (match(kind)) {
            return true;
        }
    }
    return false;
}

const Token& Parser::expect(TokenKind kind, std::string_view message) {
    if (check(kind)) {
        return advance();
    }
    diagnostics_.error(peek().span, std::string(message));
    return peek();
}

void Parser::synchronize() {
    while (!at_end()) {
        if (peek().kind == TokenKind::Semicolon || peek().kind == TokenKind::RightBrace) {
            advance();
            return;
        }
        if (is_declaration_start(peek().kind)) {
            return;
        }
        advance();
    }
}

bool Parser::is_module_kind_keyword(TokenKind kind) const noexcept {
    switch (kind) {
    case TokenKind::KeywordDomain:
    case TokenKind::KeywordBoundary:
    case TokenKind::KeywordFoundation:
    case TokenKind::KeywordHazard:
        return true;
    default:
        return false;
    }
}

ast::Visibility Parser::parse_visibility() {
    if (match(TokenKind::KeywordPublic)) {
        return ast::Visibility::Public;
    }
    return ast::Visibility::Private;
}

std::optional<ast::ImportDecl> Parser::parse_import() {
    const Token start = expect(TokenKind::KeywordImport, "expected 'import'");
    ast::ImportDecl import_decl;
    import_decl.span.begin = start.span.begin;

    if (!check(TokenKind::Identifier)) {
        diagnostics_.error(peek().span, "expected module path after 'import'");
        if (match(TokenKind::Semicolon)) {
            import_decl.span.end = tokens_[current_ - 1].span.end;
        } else {
            import_decl.span.end = start.span.end;
        }
        return std::nullopt;
    }

    import_decl.path = parse_path();
    const Token end = expect(TokenKind::Semicolon, "expected ';' after import declaration");
    import_decl.span.end = end.span.end;
    return import_decl;
}

std::unique_ptr<ast::Decl> Parser::parse_top_level_decl() {
    const ast::Visibility visibility = parse_visibility();
    if (is_module_kind_keyword(peek().kind)) {
        return parse_module(visibility);
    }
    diagnostics_.error(peek().span,
                       "only module declarations are allowed at the top level; wrap declarations in "
                       "`domain module Name { ... }` (or boundary, foundation, hazard)");
    return nullptr;
}

std::unique_ptr<ast::Decl> Parser::parse_decl() {
    const ast::Visibility visibility = parse_visibility();

    if (is_module_kind_keyword(peek().kind)) {
        return parse_module(visibility);
    }
    if (match(TokenKind::KeywordRecord)) {
        --current_;
        return parse_record(visibility);
    }
    if (match(TokenKind::KeywordState)) {
        --current_;
        return parse_state(visibility);
    }
    if (match(TokenKind::KeywordReason)) {
        --current_;
        return parse_reason(visibility);
    }
    if (match(TokenKind::KeywordProof)) {
        --current_;
        return parse_proof(visibility);
    }
    if (match(TokenKind::KeywordPermit)) {
        --current_;
        return parse_permit(visibility);
    }
    if (match(TokenKind::KeywordPhase)) {
        --current_;
        return parse_phase(visibility);
    }
    if (match(TokenKind::KeywordForeign)) {
        return parse_function(visibility, true);
    }
    if (match(TokenKind::KeywordFn)) {
        --current_;
        return parse_function(visibility, false);
    }

    diagnostics_.error(peek().span, "expected a declaration");
    return nullptr;
}

std::unique_ptr<ast::ModuleDecl> Parser::parse_module(ast::Visibility visibility) {
    const Token kind_token = advance(); // domain/boundary/foundation/hazard
    ast::ModuleKind module_kind = ast::ModuleKind::Domain;
    switch (kind_token.kind) {
    case TokenKind::KeywordDomain:
        module_kind = ast::ModuleKind::Domain;
        break;
    case TokenKind::KeywordBoundary:
        module_kind = ast::ModuleKind::Boundary;
        break;
    case TokenKind::KeywordFoundation:
        module_kind = ast::ModuleKind::Foundation;
        break;
    case TokenKind::KeywordHazard:
        module_kind = ast::ModuleKind::Hazard;
        break;
    default:
        diagnostics_.error(kind_token.span, "expected module kind (domain, boundary, foundation, or hazard)");
        break;
    }
    expect(TokenKind::KeywordModule, "expected 'module' after module kind");
    const Token name = expect(TokenKind::Identifier, "expected module name");
    auto module = std::make_unique<ast::ModuleDecl>(visibility, token_text(name), module_kind);
    expect(TokenKind::LeftBrace, "expected '{' after module name");

    while (!check(TokenKind::RightBrace) && !at_end()) {
        if (std::unique_ptr<ast::Decl> member = parse_decl()) {
            module->members.push_back(std::move(member));
        } else {
            synchronize();
        }
    }

    const Token end = expect(TokenKind::RightBrace, "expected '}' after module body");
    module->span = SourceSpan{kind_token.span.begin, end.span.end};
    return module;
}

std::unique_ptr<ast::RecordDecl> Parser::parse_record(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordRecord, "expected 'record'");
    const Token name = expect(TokenKind::Identifier, "expected record name");
    auto decl = std::make_unique<ast::RecordDecl>(visibility, token_text(name));
    decl->generic_params = parse_generic_params();
    decl->fields = parse_field_block();
    decl->span = SourceSpan{start.span.begin, tokens_[current_ - 1].span.end};
    return decl;
}

std::unique_ptr<ast::StateDecl> Parser::parse_state(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordState, "expected 'state'");
    const Token name = expect(TokenKind::Identifier, "expected state name");
    auto decl = std::make_unique<ast::StateDecl>(visibility, token_text(name));
    if (check(TokenKind::LeftAngle)) {
        diagnostics_.error(peek().span, "state declarations may not be generic");
        parse_generic_params();
    }
    decl->variants = parse_variant_block();
    decl->span = SourceSpan{start.span.begin, tokens_[current_ - 1].span.end};
    return decl;
}

std::unique_ptr<ast::ReasonDecl> Parser::parse_reason(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordReason, "expected 'reason'");
    const Token name = expect(TokenKind::Identifier, "expected reason name");
    auto decl = std::make_unique<ast::ReasonDecl>(visibility, token_text(name));
    if (check(TokenKind::LeftAngle)) {
        diagnostics_.error(peek().span, "reason declarations may not be generic");
        parse_generic_params();
    }
    decl->variants = parse_variant_block();
    decl->span = SourceSpan{start.span.begin, tokens_[current_ - 1].span.end};
    return decl;
}

std::unique_ptr<ast::ProofDecl> Parser::parse_proof(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordProof, "expected 'proof'");
    const Token name = expect(TokenKind::Identifier, "expected proof name");
    auto decl = std::make_unique<ast::ProofDecl>(visibility, token_text(name));
    if (check(TokenKind::LeftAngle)) {
        diagnostics_.error(peek().span, "proof declarations may not be generic");
        parse_generic_params();
    }
    if (check(TokenKind::LeftBrace)) {
        decl->fields = parse_field_block();
        decl->span = SourceSpan{start.span.begin, tokens_[current_ - 1].span.end};
    } else {
        decl->span = SourceSpan{start.span.begin, name.span.end};
        consume_optional_declaration_terminator();
    }
    return decl;
}

std::unique_ptr<ast::PermitDecl> Parser::parse_permit(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordPermit, "expected 'permit'");
    const Token name = expect(TokenKind::Identifier, "expected permit name");
    auto decl = std::make_unique<ast::PermitDecl>(visibility, token_text(name));
    if (check(TokenKind::LeftAngle)) {
        diagnostics_.error(peek().span, "permit declarations may not be generic");
        parse_generic_params();
    }
    decl->span = SourceSpan{start.span.begin, name.span.end};
    consume_optional_declaration_terminator();
    return decl;
}

std::unique_ptr<ast::PhaseDecl> Parser::parse_phase(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordPhase, "expected 'phase'");
    const Token name = expect(TokenKind::Identifier, "expected phase name");
    if (check(TokenKind::LeftAngle)) {
        diagnostics_.error(peek().span, "phase declarations may not be generic");
        parse_generic_params();
    }
    expect(TokenKind::LeftBrace, "expected '{' after phase name");
    expect(TokenKind::KeywordFields, "expected 'fields' in phase declaration");
    std::vector<ast::Field> fields = parse_field_block();
    expect(TokenKind::KeywordPositions, "expected 'positions' after fields");
    expect(TokenKind::LeftBrace, "expected '{' after 'positions'");
    std::vector<std::string> positions;
    std::vector<SourceSpan> position_spans;
    while (!check(TokenKind::RightBrace) && !at_end()) {
        const Token pos = expect(TokenKind::Identifier, "expected position name");
        positions.push_back(token_text(pos));
        position_spans.push_back(pos.span);
        if (match(TokenKind::Comma)) {
            if (check(TokenKind::RightBrace)) {
                break;
            }
            continue;
        }
        break;
    }
    const Token close_positions = expect(TokenKind::RightBrace, "expected '}' after position list");
    const Token close_phase = expect(TokenKind::RightBrace, "expected '}' after phase declaration");
    auto decl = std::make_unique<ast::PhaseDecl>(visibility, token_text(name));
    decl->fields = std::move(fields);
    decl->positions = std::move(positions);
    decl->position_spans = std::move(position_spans);
    decl->span = SourceSpan{start.span.begin, close_phase.span.end};
    (void)close_positions;
    return decl;
}

std::unique_ptr<ast::FunctionDecl> Parser::parse_function(ast::Visibility visibility, bool is_foreign) {
    const Token start = is_foreign ? tokens_[current_ - 1] : expect(TokenKind::KeywordFn, "expected 'fn'");
    if (is_foreign) {
        expect(TokenKind::KeywordFn, "expected 'fn' after 'foreign'");
    }
    const Token name = expect(TokenKind::Identifier, "expected function name");
    auto decl = std::make_unique<ast::FunctionDecl>(visibility, token_text(name), is_foreign);
    decl->signature = is_foreign ? parse_foreign_function_signature(token_text(name))
                                 : parse_function_signature(token_text(name));

    if (decl->is_foreign()) {
        if (check(TokenKind::LeftBrace)) {
            diagnostics_.error(peek().span, "foreign functions may not define a body");
            decl->body = parse_block_expr();
            decl->body_span = decl->body->span;
        } else {
            consume_optional_declaration_terminator();
        }
    } else if (check(TokenKind::LeftBrace)) {
        decl->body = parse_block_expr();
        decl->body_span = decl->body->span;
    } else {
        consume_optional_declaration_terminator();
    }

    const std::size_t end = decl->body_span.has_value() ? decl->body_span->end : decl->signature.span.end;
    decl->span = SourceSpan{start.span.begin, end};
    return decl;
}

std::vector<ast::GenericParam> Parser::parse_generic_params() {
    std::vector<ast::GenericParam> params;
    if (!match(TokenKind::LeftAngle)) {
        return params;
    }

    do {
        const Token name = expect(TokenKind::Identifier, "expected generic parameter name");
        params.push_back(ast::GenericParam{token_text(name), name.span});
    } while (match(TokenKind::Comma));

    expect(TokenKind::RightAngle, "expected '>' after generic parameter list");
    return params;
}

std::vector<ast::Field> Parser::parse_field_block() {
    std::vector<ast::Field> fields;
    expect(TokenKind::LeftBrace, "expected '{'");
    while (!check(TokenKind::RightBrace) && !at_end()) {
        ast::Visibility field_vis = parse_visibility();
        const Token field_name = expect(TokenKind::Identifier, "expected field name");
        expect(TokenKind::Colon, "expected ':' after field name");
        ast::TypeRef type = parse_type();
        ast::Field field;
        field.visibility = field_vis;
        field.name = token_text(field_name);
        field.type = std::move(type);
        field.span = SourceSpan{field_name.span.begin, field.type.span.end};
        fields.push_back(std::move(field));
        if (match(TokenKind::Comma)) {
            if (check(TokenKind::RightBrace)) {
                break;
            }
            continue;
        }
        break;
    }
    expect(TokenKind::RightBrace, "expected '}' after fields");
    return fields;
}

std::vector<ast::Variant> Parser::parse_variant_block() {
    std::vector<ast::Variant> variants;
    expect(TokenKind::LeftBrace, "expected '{'");
    while (!check(TokenKind::RightBrace) && !at_end()) {
        const Token variant_name = expect(TokenKind::Identifier, "expected variant name");
        ast::Variant variant;
        variant.name = token_text(variant_name);
        variant.span.begin = variant_name.span.begin;
        if (check(TokenKind::LeftBrace)) {
            variant.fields = parse_field_block();
            variant.span.end = tokens_[current_ - 1].span.end;
        } else {
            variant.span.end = variant_name.span.end;
        }
        variants.push_back(std::move(variant));
        if (match(TokenKind::Comma)) {
            if (check(TokenKind::RightBrace)) {
                break;
            }
            continue;
        }
        break;
    }
    expect(TokenKind::RightBrace, "expected '}' after variants");
    return variants;
}

std::vector<ast::Parameter> Parser::parse_parameter_list() {
    std::vector<ast::Parameter> params;
    expect(TokenKind::LeftParen, "expected '(' after function name");
    while (!check(TokenKind::RightParen) && !at_end()) {
        ast::Parameter param;
        if (match(TokenKind::KeywordAs)) {
            param.is_permit_param = true;
        }
        const Token param_name = expect(TokenKind::Identifier, "expected parameter name");
        expect(TokenKind::Colon, "expected ':' after parameter name");
        ast::TypeRef type = parse_type();
        param.name = token_text(param_name);
        param.type = std::move(type);
        param.span = SourceSpan{param_name.span.begin, param.type.span.end};
        params.push_back(std::move(param));
        if (match(TokenKind::Comma)) {
            if (check(TokenKind::RightParen)) {
                break;
            }
            continue;
        }
        break;
    }
    expect(TokenKind::RightParen, "expected ')' after parameter list");
    return params;
}

std::vector<ast::Parameter> Parser::parse_foreign_parameter_list() {
    std::vector<ast::Parameter> params;
    expect(TokenKind::LeftParen, "expected '(' after function name");
    while (!check(TokenKind::RightParen) && !at_end()) {
        if (check(TokenKind::KeywordAs)) {
            diagnostics_.error(peek().span,
                                "foreign functions may not use permit parameters (`as name: Type` is not allowed here)");
            while (!check(TokenKind::RightParen) && !at_end()) {
                advance();
            }
            break;
        }
        ast::Parameter param;
        const Token param_name = expect(TokenKind::Identifier, "expected parameter name");
        expect(TokenKind::Colon, "expected ':' after parameter name");
        ast::TypeRef type = parse_type();
        param.name = token_text(param_name);
        param.type = std::move(type);
        param.is_permit_param = false;
        param.span = SourceSpan{param_name.span.begin, param.type.span.end};
        params.push_back(std::move(param));
        if (match(TokenKind::Comma)) {
            if (check(TokenKind::RightParen)) {
                break;
            }
            continue;
        }
        break;
    }
    expect(TokenKind::RightParen, "expected ')' after parameter list");
    return params;
}

ast::FunctionSignature Parser::parse_foreign_function_signature(std::string name) {
    ast::FunctionSignature signature;
    signature.name = std::move(name);
    // Spec: foreign-fn has no generic parameters (only value parameters and `->` type).
    if (check(TokenKind::LeftAngle)) {
        diagnostics_.error(peek().span, "foreign functions may not declare generic parameters");
        (void)parse_generic_params();
    }
    const std::size_t begin = peek().span.begin;
    signature.params = parse_foreign_parameter_list();
    expect(TokenKind::Arrow, "expected '->' after parameter list");
    signature.return_type = parse_type();
    for (;;) {
        if (check(TokenKind::KeywordFails)) {
            const Token fails_tok = advance();
            diagnostics_.error(fails_tok.span, "foreign functions may not declare `fails`");
            parse_type();
            continue;
        }
        if (check(TokenKind::KeywordGrants)) {
            const Token grants_tok = advance();
            diagnostics_.error(grants_tok.span, "foreign functions may not declare `grants`");
            parse_type();
            continue;
        }
        if (check(TokenKind::KeywordProves)) {
            const Token proves_tok = advance();
            diagnostics_.error(proves_tok.span, "foreign functions may not declare `proves`");
            parse_type();
            continue;
        }
        break;
    }
    signature.span = SourceSpan{begin, signature.return_type.span.end};
    return signature;
}

ast::FunctionSignature Parser::parse_function_signature(std::string name) {
    ast::FunctionSignature signature;
    signature.name = std::move(name);
    signature.generic_params = parse_generic_params();
    const std::size_t begin = peek().span.begin;
    signature.params = parse_parameter_list();
    expect(TokenKind::Arrow, "expected '->' after parameter list");
    signature.return_type = parse_type();
    for (;;) {
        if (match(TokenKind::KeywordFails)) {
            ast::TypeRef fails_type = parse_type();
            if (signature.fails_type.has_value()) {
                diagnostics_.error(fails_type.span, "function signature repeats `fails`");
            } else {
                signature.fails_type = std::move(fails_type);
            }
            continue;
        }
        if (match(TokenKind::KeywordGrants)) {
            ast::TypeRef grants_type = parse_type();
            if (signature.grants_type.has_value()) {
                diagnostics_.error(grants_type.span, "function signature repeats `grants`");
            } else {
                signature.grants_type = std::move(grants_type);
            }
            continue;
        }
        if (match(TokenKind::KeywordProves)) {
            signature.proves_types.push_back(parse_type());
            continue;
        }
        break;
    }
    std::size_t end = signature.return_type.span.end;
    if (signature.fails_type.has_value()) {
        end = signature.fails_type->span.end;
    }
    if (signature.grants_type.has_value()) {
        end = signature.grants_type->span.end;
    }
    if (!signature.proves_types.empty()) {
        end = signature.proves_types.back().span.end;
    }
    signature.span = SourceSpan{begin, end};
    return signature;
}

ast::TypeRef Parser::parse_type() {
    const Token first = expect(TokenKind::Identifier, "expected type name");
    ast::TypeRef type;
    type.path.push_back(token_text(first));
    type.span.begin = first.span.begin;

    while (match(TokenKind::DoubleColon)) {
        const Token segment = expect(TokenKind::Identifier, "expected type name segment after '::'");
        type.path.push_back(token_text(segment));
        type.span.end = segment.span.end;
    }

    if (match(TokenKind::LeftAngle)) {
        do {
            type.args.push_back(parse_type());
        } while (match(TokenKind::Comma));
        const Token end = expect(TokenKind::RightAngle, "expected '>' after type arguments");
        type.span.end = end.span.end;
    } else {
        type.span.end = tokens_[current_ - 1].span.end;
    }

    return type;
}

std::vector<ast::TypeRef> Parser::parse_type_argument_list() {
    std::vector<ast::TypeRef> args;
    expect(TokenKind::LeftAngle, "expected '<' before type argument list");
    do {
        args.push_back(parse_type());
    } while (match(TokenKind::Comma));
    expect(TokenKind::RightAngle, "expected '>' after type argument list");
    return args;
}

std::vector<std::string> Parser::parse_path() {
    std::vector<std::string> path;
    const Token first = expect(TokenKind::Identifier, "expected identifier");
    path.push_back(token_text(first));
    while (match(TokenKind::DoubleColon)) {
        const Token segment = expect(TokenKind::Identifier, "expected identifier after '::'");
        path.push_back(token_text(segment));
    }
    return path;
}

std::vector<ast::RecordFieldInit> Parser::parse_record_field_initializers() {
    std::vector<ast::RecordFieldInit> fields;
    expect(TokenKind::LeftBrace, "expected '{'");
    while (!check(TokenKind::RightBrace) && !at_end()) {
        const Token name = expect(TokenKind::Identifier, "expected field name");
        ast::RecordFieldInit init;
        init.name = token_text(name);
        init.span.begin = name.span.begin;
        if (match(TokenKind::Colon)) {
            init.value = parse_expr();
            init.shorthand = false;
            init.span.end = init.value != nullptr ? init.value->span.end : name.span.end;
        } else {
            init.value = make_shorthand_path_expr(name);
            init.shorthand = true;
            init.span.end = name.span.end;
        }
        fields.push_back(std::move(init));
        if (match(TokenKind::Comma)) {
            if (check(TokenKind::RightBrace)) {
                break;
            }
            continue;
        }
        break;
    }
    expect(TokenKind::RightBrace, "expected '}' after initializer list");
    return fields;
}

std::unique_ptr<ast::BlockExpr> Parser::parse_block_expr() {
    const Token open = expect(TokenKind::LeftBrace, "expected '{'");
    auto block = std::make_unique<ast::BlockExpr>();
    block->span.begin = open.span.begin;

    while (!check(TokenKind::RightBrace) && !at_end()) {
        if (check(TokenKind::KeywordLet)) {
            std::unique_ptr<ast::Stmt> stmt = parse_statement();
            if (stmt != nullptr) {
                block->statements.push_back(std::move(stmt));
            }
            (void)match(TokenKind::Semicolon);
            continue;
        }

        if (!begins_expr(peek().kind)) {
            diagnostics_.error(peek().span, "expected statement or expression inside block");
            if (!at_end()) {
                advance();
            }
            continue;
        }

        std::unique_ptr<ast::Expr> expr = parse_expr();
        if (expr == nullptr) {
            break;
        }

        if (match(TokenKind::Semicolon)) {
            auto stmt = std::make_unique<ast::ExprStmt>();
            stmt->span = expr->span;
            stmt->expr = std::move(expr);
            block->statements.push_back(std::move(stmt));
            continue;
        }

        if (check(TokenKind::RightBrace)) {
            block->result = std::move(expr);
            break;
        }

        if (check(TokenKind::KeywordLet) || begins_expr(peek().kind)) {
            auto stmt = std::make_unique<ast::ExprStmt>();
            stmt->span = expr->span;
            stmt->expr = std::move(expr);
            block->statements.push_back(std::move(stmt));
            continue;
        }

        auto stmt = std::make_unique<ast::ExprStmt>();
        stmt->span = expr->span;
        stmt->expr = std::move(expr);
        block->statements.push_back(std::move(stmt));
    }

    const Token end = expect(TokenKind::RightBrace, "expected '}' after block");
    block->span.end = end.span.end;
    return block;
}

std::unique_ptr<ast::Stmt> Parser::parse_statement() {
    const Token start = expect(TokenKind::KeywordLet, "expected 'let'");
    const Token name = expect(TokenKind::Identifier, "expected binding name");
    expect(TokenKind::Equals, "expected '=' after binding name");
    auto stmt = std::make_unique<ast::LetStmt>();
    stmt->name = token_text(name);
    stmt->initializer = parse_expr();
    stmt->span = SourceSpan{start.span.begin, stmt->initializer != nullptr ? stmt->initializer->span.end : name.span.end};
    return stmt;
}

std::unique_ptr<ast::Expr> Parser::parse_expr() {
    if (check(TokenKind::KeywordMatch)) {
        return parse_match_expr();
    }
    if (check(TokenKind::KeywordGrant)) {
        return parse_grant_expr();
    }
    return parse_try_expr();
}

std::unique_ptr<ast::Expr> Parser::parse_match_expr() {
    const Token start = expect(TokenKind::KeywordMatch, "expected 'match'");
    auto match_expr = std::make_unique<ast::MatchExpr>();
    match_expr->scrutinee = parse_expr();
    expect(TokenKind::LeftBrace, "expected '{' after match scrutinee");

    while (!check(TokenKind::RightBrace) && !at_end()) {
        ast::MatchArm arm;
        arm.pattern = parse_pattern();
        expect(TokenKind::FatArrow, "expected '=>' after match pattern");
        arm.body = parse_expr();
        arm.span = SourceSpan{
            arm.pattern != nullptr ? arm.pattern->span.begin : peek().span.begin,
            arm.body != nullptr ? arm.body->span.end : peek().span.end,
        };
        match_expr->arms.push_back(std::move(arm));
        if (match(TokenKind::Comma)) {
            if (check(TokenKind::RightBrace)) {
                break;
            }
            continue;
        }
        break;
    }

    const Token end = expect(TokenKind::RightBrace, "expected '}' after match arms");
    match_expr->span = SourceSpan{start.span.begin, end.span.end};
    return match_expr;
}

std::unique_ptr<ast::Expr> Parser::parse_try_expr() {
    if (match(TokenKind::KeywordTry)) {
        const Token start = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::TryExpr>();
        expr->operand = parse_expr();
        expr->span = SourceSpan{start.span.begin, expr->operand != nullptr ? expr->operand->span.end : start.span.end};
        return expr;
    }
    return parse_fail_expr();
}

std::unique_ptr<ast::Expr> Parser::parse_grant_expr() {
    const Token start = expect(TokenKind::KeywordGrant, "expected 'grant'");
    auto expr = std::make_unique<ast::GrantExpr>();
    expr->grant_call = parse_postfix_expr();
    expect(TokenKind::KeywordAs, "expected 'as' after grant call");
    const Token binder = expect(TokenKind::Identifier, "expected permit binder name");
    expr->binder_name = token_text(binder);
    expr->body = parse_block_expr();
    expr->span = SourceSpan{start.span.begin, expr->body != nullptr ? expr->body->span.end : binder.span.end};
    return expr;
}

std::unique_ptr<ast::Expr> Parser::parse_fail_expr() {
    if (match(TokenKind::KeywordFail)) {
        const Token start = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::FailExpr>();
        expr->path = parse_path();
        expr->span.begin = start.span.begin;
        expr->span.end = tokens_[current_ - 1].span.end;
        if (check(TokenKind::LeftBrace)) {
            expr->fields = parse_record_field_initializers();
            expr->span.end = tokens_[current_ - 1].span.end;
        }
        return expr;
    }
    return parse_prove_expr();
}

std::unique_ptr<ast::Expr> Parser::parse_prove_expr() {
    if (match(TokenKind::KeywordProve)) {
        const Token start = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::ProveExpr>();
        expr->path = parse_path();
        expr->span.begin = start.span.begin;
        expr->span.end = tokens_[current_ - 1].span.end;
        if (check(TokenKind::LeftBrace)) {
            expr->fields = parse_record_field_initializers();
            expr->span.end = tokens_[current_ - 1].span.end;
        }
        return expr;
    }
    return parse_postfix_expr();
}

std::unique_ptr<ast::Expr> Parser::parse_postfix_expr() {
    std::unique_ptr<ast::Expr> expr = parse_primary_expr();
    for (;;) {
        if (expr == nullptr) {
            break;
        }
        std::vector<ast::TypeRef> type_args;
        if (check(TokenKind::LeftAngle)) {
            auto* path_expr = dynamic_cast<ast::PathExpr*>(expr.get());
            if (path_expr == nullptr) {
                diagnostics_.error(peek().span, "type arguments may only be applied to named calls or record construction");
                break;
            }
            type_args = parse_type_argument_list();
            if (check(TokenKind::LeftBrace) && looks_like_record_initializer()) {
                auto construct = std::make_unique<ast::ConstructExpr>();
                construct->path = std::move(path_expr->path);
                construct->type_args = std::move(type_args);
                construct->fields = parse_record_field_initializers();
                construct->span = SourceSpan{expr->span.begin, tokens_[current_ - 1].span.end};
                expr = std::move(construct);
                continue;
            }
            if (!check(TokenKind::LeftParen)) {
                diagnostics_.error(tokens_[current_ - 1].span,
                                   "type arguments must be followed by an argument list or record initializer");
                break;
            }
        }
        if (match(TokenKind::LeftParen)) {
            const Token open = tokens_[current_ - 1];
            auto call = std::make_unique<ast::CallExpr>();
            call->callee = std::move(expr);
            call->type_args = std::move(type_args);
            while (!check(TokenKind::RightParen) && !at_end()) {
                if (check(TokenKind::KeywordAs)) {
                    // permit argument: `as name`
                    advance(); // consume 'as'
                    auto permit_arg = std::make_unique<ast::PathExpr>();
                    permit_arg->explicit_permit_argument = true;
                    const Token permit_name = expect(TokenKind::Identifier, "expected permit name after 'as'");
                    permit_arg->path.push_back(token_text(permit_name));
                    permit_arg->span = permit_name.span;
                    call->args.push_back(std::move(permit_arg));
                } else {
                    call->args.push_back(parse_expr());
                }
                if (match(TokenKind::Comma)) {
                    if (check(TokenKind::RightParen)) {
                        break;
                    }
                    continue;
                }
                break;
            }
            const Token close = expect(TokenKind::RightParen, "expected ')' after argument list");
            call->span = SourceSpan{call->callee != nullptr ? call->callee->span.begin : open.span.begin, close.span.end};
            expr = std::move(call);
            continue;
        }
        if (match(TokenKind::Dot)) {
            const Token field_name = expect(TokenKind::Identifier, "expected field name after '.'");
            auto field_access = std::make_unique<ast::FieldAccessExpr>();
            field_access->object = std::move(expr);
            field_access->field_name = token_text(field_name);
            field_access->span = SourceSpan{field_access->object->span.begin, field_name.span.end};
            expr = std::move(field_access);
            continue;
        }
        break;
    }
    return expr;
}

std::unique_ptr<ast::Expr> Parser::parse_primary_expr() {
    if (match(TokenKind::Number)) {
        const Token token = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::NumberLiteralExpr>(token_text(token));
        expr->span = token.span;
        return expr;
    }
    if (match(TokenKind::String)) {
        const Token token = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::StringLiteralExpr>(token_text(token));
        expr->span = token.span;
        return expr;
    }
    if (check(TokenKind::LeftBrace)) {
        return parse_block_expr();
    }
    if (check(TokenKind::Identifier)) {
        const std::size_t begin = peek().span.begin;
        std::vector<std::string> path = parse_path();
        if (check(TokenKind::LeftBrace) && looks_like_record_initializer()) {
            auto expr = std::make_unique<ast::ConstructExpr>();
            expr->path = std::move(path);
            expr->fields = parse_record_field_initializers();
            expr->span = SourceSpan{begin, tokens_[current_ - 1].span.end};
            return expr;
        }
        auto expr = std::make_unique<ast::PathExpr>();
        expr->path = std::move(path);
        expr->span = SourceSpan{begin, tokens_[current_ - 1].span.end};
        return expr;
    }

    diagnostics_.error(peek().span, "expected expression");
    return nullptr;
}

std::unique_ptr<ast::Pattern> Parser::parse_pattern() {
    // Check for succeeded(...) and failed(...) as keywords
    if (check(TokenKind::KeywordSucceeded)) {
        const Token start = advance();
        auto pattern = std::make_unique<ast::SucceededPattern>();
        expect(TokenKind::LeftParen, "expected '(' after 'succeeded'");
        if (check(TokenKind::Identifier) && peek().lexeme == "_") {
            advance();
            pattern->ignore = true;
        } else {
            const Token binding = expect(TokenKind::Identifier, "expected binding or '_' inside succeeded pattern");
            pattern->binding_name = token_text(binding);
        }
        const Token close = expect(TokenKind::RightParen, "expected ')' after succeeded pattern");
        pattern->span = SourceSpan{start.span.begin, close.span.end};
        return pattern;
    }

    if (check(TokenKind::KeywordFailed)) {
        const Token start = advance();
        auto pattern = std::make_unique<ast::FailedPattern>();
        expect(TokenKind::LeftParen, "expected '(' after 'failed'");
        const std::size_t inner_begin = peek().span.begin;
        std::vector<std::string> inner_path = parse_path();
        const SourceSpan inner_span{inner_begin, tokens_[current_ - 1].span.end};
        pattern->variant = parse_variant_pattern_from_path(std::move(inner_path), inner_span);
        const Token close = expect(TokenKind::RightParen, "expected ')' after failed pattern");
        pattern->span = SourceSpan{start.span.begin, close.span.end};
        return pattern;
    }

    const std::size_t begin = peek().span.begin;
    std::vector<std::string> path = parse_path();
    const SourceSpan path_span{begin, tokens_[current_ - 1].span.end};

    // Legacy support: also handle succeeded/failed as identifiers in path
    if (path.size() == 1 && path.front() == "succeeded") {
        auto pattern = std::make_unique<ast::SucceededPattern>();
        expect(TokenKind::LeftParen, "expected '(' after 'succeeded'");
        const Token binding = expect(TokenKind::Identifier, "expected binding or '_' inside succeeded pattern");
        if (binding.lexeme == "_") {
            pattern->ignore = true;
        } else {
            pattern->binding_name = token_text(binding);
        }
        const Token close = expect(TokenKind::RightParen, "expected ')' after succeeded pattern");
        pattern->span = SourceSpan{begin, close.span.end};
        return pattern;
    }

    if (path.size() == 1 && path.front() == "failed") {
        auto pattern = std::make_unique<ast::FailedPattern>();
        expect(TokenKind::LeftParen, "expected '(' after 'failed'");
        const std::size_t inner_begin = peek().span.begin;
        std::vector<std::string> inner_path = parse_path();
        const SourceSpan inner_span{inner_begin, tokens_[current_ - 1].span.end};
        pattern->variant = parse_variant_pattern_from_path(std::move(inner_path), inner_span);
        const Token close = expect(TokenKind::RightParen, "expected ')' after failed pattern");
        pattern->span = SourceSpan{begin, close.span.end};
        return pattern;
    }

    return parse_variant_pattern_from_path(std::move(path), path_span);
}

std::unique_ptr<ast::VariantPattern> Parser::parse_variant_pattern_from_path(std::vector<std::string> path,
                                                                             SourceSpan path_span) {
    auto pattern = std::make_unique<ast::VariantPattern>();
    pattern->path = std::move(path);
    pattern->span = path_span;

    if (!match(TokenKind::LeftBrace)) {
        return pattern;
    }

    const Token open = tokens_[current_ - 1];
    if (match(TokenKind::DotDot)) {
        pattern->payload_mode = ast::VariantPattern::PayloadMode::Ignore;
        const Token close = expect(TokenKind::RightBrace, "expected '}' after '..'");
        pattern->span = SourceSpan{path_span.begin, close.span.end};
        return pattern;
    }

    if (!check(TokenKind::RightBrace)) {
        pattern->payload_mode = ast::VariantPattern::PayloadMode::Bindings;
        while (!check(TokenKind::RightBrace) && !at_end()) {
            const Token field = expect(TokenKind::Identifier, "expected field name in pattern");
            ast::VariantPattern::Binding binding;
            binding.field_name = token_text(field);
            binding.binding_name = binding.field_name;
            binding.span.begin = field.span.begin;
            if (match(TokenKind::Colon)) {
                const Token value = expect(TokenKind::Identifier, "expected binding name after ':'");
                binding.binding_name = token_text(value);
                binding.span.end = value.span.end;
            } else {
                binding.span.end = field.span.end;
            }
            pattern->bindings.push_back(std::move(binding));
            if (match(TokenKind::Comma)) {
                if (check(TokenKind::RightBrace)) {
                    break;
                }
                continue;
            }
            break;
        }
    } else {
        diagnostics_.error(open.span, "empty payload pattern is not allowed; bind fields or use '{ .. }'");
        pattern->payload_mode = ast::VariantPattern::PayloadMode::Bindings;
    }

    const Token close = expect(TokenKind::RightBrace, "expected '}' after pattern payload");
    pattern->span = SourceSpan{path_span.begin, close.span.end};
    return pattern;
}

void Parser::consume_optional_declaration_terminator() {
    if (match(TokenKind::Semicolon)) {
        return;
    }
    if (check_any({TokenKind::RightBrace, TokenKind::EndOfFile}) || is_declaration_start(peek().kind)) {
        return;
    }
}

bool Parser::begins_expr(TokenKind kind) const noexcept {
    switch (kind) {
    case TokenKind::Identifier:
    case TokenKind::Number:
    case TokenKind::String:
    case TokenKind::LeftBrace:
    case TokenKind::KeywordMatch:
    case TokenKind::KeywordGrant:
    case TokenKind::KeywordTry:
    case TokenKind::KeywordFail:
    case TokenKind::KeywordProve:
        return true;
    default:
        return false;
    }
}


bool Parser::looks_like_record_initializer() const noexcept {
    if (!check(TokenKind::LeftBrace)) {
        return false;
    }
    if (peek(1).kind == TokenKind::RightBrace) {
        return true;
    }
    if (peek(1).kind != TokenKind::Identifier) {
        return false;
    }
    return peek(2).kind == TokenKind::Colon
        || peek(2).kind == TokenKind::Comma
        || peek(2).kind == TokenKind::RightBrace;
}

} // namespace evident
