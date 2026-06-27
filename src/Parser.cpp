#include "evident/Parser.hpp"

#include <utility>

namespace evident {

namespace {

enum class DeclarationStartState {
    DoesNotStartDeclaration,
    StartsDeclaration,
};

enum class FieldSeparatorRecovery {
    StopFieldBlock,
    ContinueWithNextField,
};

enum class ParameterSeparatorRecovery {
    StopParameterList,
    ContinueWithNextParameter,
};

enum class VariantSeparatorRecovery {
    StopVariantBlock,
    ContinueWithNextVariant,
};

enum class PhasePositionSeparatorRecovery {
    StopPositionList,
    ContinueWithNextPosition,
};

enum class MatchArmSeparatorRecovery {
    StopMatchArms,
    ContinueWithNextArm,
};

DeclarationStartState declaration_start_state(TokenKind kind) {
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
        return DeclarationStartState::StartsDeclaration;
    default:
        return DeclarationStartState::DoesNotStartDeclaration;
    }
}

std::string token_text(const Token& token) {
    return std::string(token.lexeme());
}

std::unique_ptr<ast::PathExpr> make_shorthand_path_expr(const Token& token) {
    auto expr = std::make_unique<ast::PathExpr>();
    expr->path.push_back(token_text(token));
    expr->span = token.span();
    return expr;
}

FieldSeparatorRecovery field_after_missing_separator(TokenKind first, TokenKind second, TokenKind third) {
    if (first == TokenKind::Identifier && second == TokenKind::Colon) {
        return FieldSeparatorRecovery::ContinueWithNextField;
    }
    if (first == TokenKind::KeywordPublic && second == TokenKind::Identifier && third == TokenKind::Colon) {
        return FieldSeparatorRecovery::ContinueWithNextField;
    }
    return FieldSeparatorRecovery::StopFieldBlock;
}

ParameterSeparatorRecovery parameter_after_missing_separator(TokenKind first, TokenKind second, TokenKind third) {
    if (first == TokenKind::Identifier && second == TokenKind::Colon) {
        return ParameterSeparatorRecovery::ContinueWithNextParameter;
    }
    if (first == TokenKind::KeywordAs && second == TokenKind::Identifier && third == TokenKind::Colon) {
        return ParameterSeparatorRecovery::ContinueWithNextParameter;
    }
    return ParameterSeparatorRecovery::StopParameterList;
}

VariantSeparatorRecovery variant_after_missing_separator(TokenKind first, TokenKind second) {
    if (first != TokenKind::Identifier) {
        return VariantSeparatorRecovery::StopVariantBlock;
    }
    switch (second) {
    case TokenKind::Comma:
    case TokenKind::LeftBrace:
    case TokenKind::RightBrace:
        return VariantSeparatorRecovery::ContinueWithNextVariant;
    default:
        return VariantSeparatorRecovery::StopVariantBlock;
    }
}

PhasePositionSeparatorRecovery phase_position_after_missing_separator(TokenKind first) {
    if (first == TokenKind::Identifier) {
        return PhasePositionSeparatorRecovery::ContinueWithNextPosition;
    }
    return PhasePositionSeparatorRecovery::StopPositionList;
}

MatchArmSeparatorRecovery match_arm_after_missing_separator(TokenKind first, TokenKind second) {
    if ((first == TokenKind::KeywordSucceeded || first == TokenKind::KeywordFailed)
        && second == TokenKind::LeftParen) {
        return MatchArmSeparatorRecovery::ContinueWithNextArm;
    }
    if (first != TokenKind::Identifier) {
        return MatchArmSeparatorRecovery::StopMatchArms;
    }
    switch (second) {
    case TokenKind::FatArrow:
    case TokenKind::DoubleColon:
    case TokenKind::LeftBrace:
        return MatchArmSeparatorRecovery::ContinueWithNextArm;
    default:
        return MatchArmSeparatorRecovery::StopMatchArms;
    }
}

} // namespace

Parser::Parser(const SourceFile& source, std::vector<Token> tokens, DiagnosticSink& diagnostics)
    : source_(source), tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

ast::TranslationUnit Parser::parse() {
    ast::TranslationUnit unit;
    while (cursor_state() == ParserCursorState::HasMoreTokens) {
        if (token_check(TokenKind::EndOfFile) == TokenCheckState::Matches) {
            break;
        }

        if (token_check(TokenKind::KeywordImport) == TokenCheckState::Matches) {
            parse_import_into(unit);
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

ParserCursorState Parser::cursor_state() const noexcept {
    if (peek().kind() == TokenKind::EndOfFile) {
        return ParserCursorState::ReachedEnd;
    }
    return ParserCursorState::HasMoreTokens;
}

TokenCheckState Parser::token_check(TokenKind kind) const noexcept {
    if (peek().kind() == kind) {
        return TokenCheckState::Matches;
    }
    return TokenCheckState::DifferentToken;
}

TokenCheckState Parser::token_check_any(std::initializer_list<TokenKind> kinds) const noexcept {
    for (TokenKind kind : kinds) {
        if (token_check(kind) == TokenCheckState::Matches) {
            return TokenCheckState::Matches;
        }
    }
    return TokenCheckState::DifferentToken;
}

const Token& Parser::advance() {
    if (cursor_state() == ParserCursorState::HasMoreTokens) {
        ++current_;
    }
    return tokens_[current_ - 1];
}

TokenConsumptionState Parser::consume_if(TokenKind kind) {
    if (token_check(kind) == TokenCheckState::DifferentToken) {
        return TokenConsumptionState::NotConsumed;
    }
    advance();
    return TokenConsumptionState::Consumed;
}

TokenConsumptionState Parser::consume_any(std::initializer_list<TokenKind> kinds) {
    for (TokenKind kind : kinds) {
        if (consume_if(kind) == TokenConsumptionState::Consumed) {
            return TokenConsumptionState::Consumed;
        }
    }
    return TokenConsumptionState::NotConsumed;
}

const Token& Parser::expect(TokenKind kind, std::string_view message) {
    if (token_check(kind) == TokenCheckState::Matches) {
        return advance();
    }
    diagnostics_.error(peek().span(), std::string(message));
    return peek();
}

void Parser::synchronize() {
    while (cursor_state() == ParserCursorState::HasMoreTokens) {
        if (peek().kind() == TokenKind::Semicolon || peek().kind() == TokenKind::RightBrace) {
            advance();
            return;
        }
        if (declaration_start_state(peek().kind()) == DeclarationStartState::StartsDeclaration) {
            return;
        }
        advance();
    }
}

ModuleKindTokenRole Parser::module_kind_token_role(TokenKind kind) const noexcept {
    switch (kind) {
    case TokenKind::KeywordDomain:
    case TokenKind::KeywordBoundary:
    case TokenKind::KeywordFoundation:
    case TokenKind::KeywordHazard:
        return ModuleKindTokenRole::ModuleKindKeyword;
    default:
        return ModuleKindTokenRole::OrdinaryToken;
    }
}

ast::Visibility Parser::parse_visibility() {
    if (consume_if(TokenKind::KeywordPublic) == TokenConsumptionState::Consumed) {
        return ast::Visibility::Public;
    }
    return ast::Visibility::Private;
}

void Parser::parse_import_into(ast::TranslationUnit& unit) {
    const Token start = expect(TokenKind::KeywordImport, "expected 'import'");
    ast::ImportDecl import_decl;
    import_decl.span.begin = start.span().begin;

    if (token_check(TokenKind::Identifier) == TokenCheckState::DifferentToken) {
        diagnostics_.error(peek().span(), "expected module path after 'import'");
        if (consume_if(TokenKind::Semicolon) == TokenConsumptionState::Consumed) {
            import_decl.span.end = tokens_[current_ - 1].span().end;
        } else {
            import_decl.span.end = start.span().end;
        }
        return;
    }

    import_decl.path = parse_path();
    const Token end = expect(TokenKind::Semicolon, "expected ';' after import declaration");
    import_decl.span.end = end.span().end;
    unit.imports.push_back(std::move(import_decl));
}

std::unique_ptr<ast::Decl> Parser::parse_top_level_decl() {
    const ast::Visibility visibility = parse_visibility();
    if (token_check(TokenKind::KeywordModule) == TokenCheckState::Matches) {
        reject_kindless_module();
        return nullptr;
    }
    if (module_kind_token_role(peek().kind()) == ModuleKindTokenRole::ModuleKindKeyword) {
        return parse_module(visibility);
    }
    diagnostics_.error(peek().span(),
                       "only module declarations are allowed at the top level; wrap declarations in "
                       "`domain module Name { ... }` (or boundary, foundation, hazard)");
    if (cursor_state() == ParserCursorState::HasMoreTokens) {
        advance();
    }
    return nullptr;
}

std::unique_ptr<ast::Decl> Parser::parse_decl() {
    const ast::Visibility visibility = parse_visibility();

    if (token_check(TokenKind::KeywordModule) == TokenCheckState::Matches) {
        reject_kindless_module();
        return nullptr;
    }
    if (module_kind_token_role(peek().kind()) == ModuleKindTokenRole::ModuleKindKeyword) {
        return parse_module(visibility);
    }
    if (consume_if(TokenKind::KeywordRecord) == TokenConsumptionState::Consumed) {
        --current_;
        return parse_record(visibility);
    }
    if (consume_if(TokenKind::KeywordState) == TokenConsumptionState::Consumed) {
        --current_;
        return parse_state(visibility);
    }
    if (consume_if(TokenKind::KeywordReason) == TokenConsumptionState::Consumed) {
        --current_;
        return parse_reason(visibility);
    }
    if (consume_if(TokenKind::KeywordProof) == TokenConsumptionState::Consumed) {
        --current_;
        return parse_proof(visibility);
    }
    if (consume_if(TokenKind::KeywordPermit) == TokenConsumptionState::Consumed) {
        --current_;
        return parse_permit(visibility);
    }
    if (consume_if(TokenKind::KeywordPhase) == TokenConsumptionState::Consumed) {
        --current_;
        return parse_phase(visibility);
    }
    if (consume_if(TokenKind::KeywordForeign) == TokenConsumptionState::Consumed) {
        return parse_function(visibility, ast::FunctionImplementation::ForeignImport);
    }
    if (consume_if(TokenKind::KeywordFn) == TokenConsumptionState::Consumed) {
        --current_;
        return parse_function(visibility, ast::FunctionImplementation::EvidentBody);
    }

    diagnostics_.error(peek().span(), "expected a declaration");
    return nullptr;
}

void Parser::reject_kindless_module() {
    diagnostics_.error(peek().span(), "expected module kind (domain, boundary, foundation, or hazard) before `module`");
    advance();
}

std::unique_ptr<ast::ModuleDecl> Parser::parse_module(ast::Visibility visibility) {
    const Token kind_token = advance(); // domain/boundary/foundation/hazard
    ast::ModuleKind module_kind = ast::ModuleKind::Domain;
    switch (kind_token.kind()) {
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
        diagnostics_.error(kind_token.span(), "expected module kind (domain, boundary, foundation, or hazard)");
        break;
    }
    expect(TokenKind::KeywordModule, "expected 'module' after module kind");
    const Token name = expect(TokenKind::Identifier, "expected module name");
    auto module = std::make_unique<ast::ModuleDecl>(visibility, token_text(name), module_kind);
    expect(TokenKind::LeftBrace, "expected '{' after module name");

    while (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        if (std::unique_ptr<ast::Decl> member = parse_decl()) {
            module->members.push_back(std::move(member));
        } else {
            synchronize();
        }
    }

    const Token end = expect(TokenKind::RightBrace, "expected '}' after module body");
    module->span = SourceSpan{kind_token.span().begin, end.span().end};
    return module;
}

std::unique_ptr<ast::RecordDecl> Parser::parse_record(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordRecord, "expected 'record'");
    const Token name = expect(TokenKind::Identifier, "expected record name");
    auto decl = std::make_unique<ast::RecordDecl>(visibility, token_text(name));
    decl->generic_params = parse_generic_params();
    decl->fields = parse_field_block();
    decl->span = SourceSpan{start.span().begin, tokens_[current_ - 1].span().end};
    return decl;
}

std::unique_ptr<ast::StateDecl> Parser::parse_state(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordState, "expected 'state'");
    const Token name = expect(TokenKind::Identifier, "expected state name");
    auto decl = std::make_unique<ast::StateDecl>(visibility, token_text(name));
    if (token_check(TokenKind::LeftAngle) == TokenCheckState::Matches) {
        diagnostics_.error(peek().span(), "state declarations may not be generic");
        parse_generic_params();
    }
    decl->variants = parse_variant_block();
    decl->span = SourceSpan{start.span().begin, tokens_[current_ - 1].span().end};
    return decl;
}

std::unique_ptr<ast::ReasonDecl> Parser::parse_reason(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordReason, "expected 'reason'");
    const Token name = expect(TokenKind::Identifier, "expected reason name");
    auto decl = std::make_unique<ast::ReasonDecl>(visibility, token_text(name));
    if (token_check(TokenKind::LeftAngle) == TokenCheckState::Matches) {
        diagnostics_.error(peek().span(), "reason declarations may not be generic");
        parse_generic_params();
    }
    decl->variants = parse_variant_block();
    decl->span = SourceSpan{start.span().begin, tokens_[current_ - 1].span().end};
    return decl;
}

std::unique_ptr<ast::ProofDecl> Parser::parse_proof(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordProof, "expected 'proof'");
    const Token name = expect(TokenKind::Identifier, "expected proof name");
    auto decl = std::make_unique<ast::ProofDecl>(visibility, token_text(name));
    if (token_check(TokenKind::LeftAngle) == TokenCheckState::Matches) {
        diagnostics_.error(peek().span(), "proof declarations may not be generic");
        parse_generic_params();
    }
    if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches) {
        decl->fields = parse_field_block();
        decl->span = SourceSpan{start.span().begin, tokens_[current_ - 1].span().end};
    } else {
        decl->span = SourceSpan{start.span().begin, name.span().end};
        consume_optional_declaration_terminator();
    }
    return decl;
}

std::unique_ptr<ast::PermitDecl> Parser::parse_permit(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordPermit, "expected 'permit'");
    const Token name = expect(TokenKind::Identifier, "expected permit name");
    auto decl = std::make_unique<ast::PermitDecl>(visibility, token_text(name));
    if (token_check(TokenKind::LeftAngle) == TokenCheckState::Matches) {
        diagnostics_.error(peek().span(), "permit declarations may not be generic");
        parse_generic_params();
    }
    decl->span = SourceSpan{start.span().begin, name.span().end};
    consume_optional_declaration_terminator();
    return decl;
}

std::unique_ptr<ast::PhaseDecl> Parser::parse_phase(ast::Visibility visibility) {
    const Token start = expect(TokenKind::KeywordPhase, "expected 'phase'");
    const Token name = expect(TokenKind::Identifier, "expected phase name");
    if (token_check(TokenKind::LeftAngle) == TokenCheckState::Matches) {
        diagnostics_.error(peek().span(), "phase declarations may not be generic");
        parse_generic_params();
    }
    expect(TokenKind::LeftBrace, "expected '{' after phase name");

    std::vector<ast::Field> fields;
    if (consume_if(TokenKind::KeywordFields) == TokenConsumptionState::Consumed) {
        fields = parse_field_block();
    } else {
        diagnostics_.error(peek().span(), "expected 'fields' in phase declaration");
        if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches) {
            fields = parse_field_block();
        }
    }

    if (consume_if(TokenKind::KeywordPositions) == TokenConsumptionState::NotConsumed) {
        diagnostics_.error(peek().span(), "expected 'positions' after fields");
        const Token close_phase = expect(TokenKind::RightBrace, "expected '}' after phase declaration");
        auto decl = std::make_unique<ast::PhaseDecl>(visibility, token_text(name));
        decl->fields = std::move(fields);
        decl->span = SourceSpan{start.span().begin, close_phase.span().end};
        return decl;
    }

    expect(TokenKind::LeftBrace, "expected '{' after 'positions'");
    std::vector<std::string> positions;
    std::vector<SourceSpan> position_spans;
    while (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        const Token pos = expect(TokenKind::Identifier, "expected position name");
        positions.push_back(token_text(pos));
        position_spans.push_back(pos.span());
        if (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed) {
            if (token_check(TokenKind::RightBrace) == TokenCheckState::Matches) {
                break;
            }
            continue;
        }
        if (phase_position_after_missing_separator(peek().kind())
            == PhasePositionSeparatorRecovery::ContinueWithNextPosition) {
            diagnostics_.error(peek().span(), "expected ',' between phase positions");
            continue;
        }
        break;
    }
    expect(TokenKind::RightBrace, "expected '}' after position list");
    const Token close_phase = expect(TokenKind::RightBrace, "expected '}' after phase declaration");
    auto decl = std::make_unique<ast::PhaseDecl>(visibility, token_text(name));
    decl->fields = std::move(fields);
    decl->positions = std::move(positions);
    decl->position_spans = std::move(position_spans);
    decl->span = SourceSpan{start.span().begin, close_phase.span().end};
    return decl;
}

std::unique_ptr<ast::FunctionDecl> Parser::parse_function(ast::Visibility visibility,
                                                          ast::FunctionImplementation implementation) {
    const Token start = implementation == ast::FunctionImplementation::ForeignImport
        ? tokens_[current_ - 1]
        : expect(TokenKind::KeywordFn, "expected 'fn'");
    if (implementation == ast::FunctionImplementation::ForeignImport) {
        expect(TokenKind::KeywordFn, "expected 'fn' after 'foreign'");
    }
    const Token name = expect(TokenKind::Identifier, "expected function name");
    auto decl = std::make_unique<ast::FunctionDecl>(visibility, token_text(name), implementation);
    decl->signature = implementation == ast::FunctionImplementation::ForeignImport
        ? parse_foreign_function_signature(token_text(name))
        : parse_function_signature(token_text(name));

    if (decl->implementation() == ast::FunctionImplementation::ForeignImport) {
        if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches) {
            diagnostics_.error(peek().span(), "foreign functions may not define a body");
            decl->body = parse_block_expr();
        } else {
            consume_optional_declaration_terminator();
        }
    } else if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches) {
        decl->body = parse_block_expr();
    } else {
        consume_optional_declaration_terminator();
    }

    const std::size_t end = decl->body != nullptr ? decl->body->span.end : decl->signature.span.end;
    decl->span = SourceSpan{start.span().begin, end};
    return decl;
}

std::vector<ast::GenericParam> Parser::parse_generic_params() {
    std::vector<ast::GenericParam> params;
    if (consume_if(TokenKind::LeftAngle) == TokenConsumptionState::NotConsumed) {
        return params;
    }

    do {
        const Token name = expect(TokenKind::Identifier, "expected generic parameter name");
        params.push_back(ast::GenericParam{token_text(name), name.span()});
    } while (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed);

    expect(TokenKind::RightAngle, "expected '>' after generic parameter list");
    return params;
}

std::vector<ast::Field> Parser::parse_field_block(FieldBlockContext context) {
    std::vector<ast::Field> fields;
    expect(TokenKind::LeftBrace, "expected '{'");
    while (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        ast::Visibility field_vis = parse_visibility();
        const Token field_name = expect(TokenKind::Identifier, "expected field name");
        expect(TokenKind::Colon, "expected ':' after field name");
        ast::TypeRef type = parse_type();
        ast::Field field;
        field.visibility = field_vis;
        field.name = token_text(field_name);
        field.type = std::move(type);
        field.span = SourceSpan{field_name.span().begin, field.type.span.end};
        fields.push_back(std::move(field));
        if (token_check(TokenKind::Comma) == TokenCheckState::Matches) {
            if (context == FieldBlockContext::VariantPayloadFields
                && peek(1).kind() == TokenKind::Identifier
                && peek(2).kind() != TokenKind::Colon) {
                diagnostics_.error(peek().span(), "expected '}' after variant payload fields");
                return fields;
            }
            advance();
            if (token_check(TokenKind::RightBrace) == TokenCheckState::Matches) {
                break;
            }
            continue;
        }
        if (field_after_missing_separator(peek().kind(), peek(1).kind(), peek(2).kind())
            == FieldSeparatorRecovery::ContinueWithNextField) {
            diagnostics_.error(peek().span(), "expected ',' between fields");
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
    while (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        const Token variant_name = expect(TokenKind::Identifier, "expected variant name");
        ast::Variant variant;
        variant.name = token_text(variant_name);
        variant.span.begin = variant_name.span().begin;
        if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches) {
            variant.fields = parse_field_block(FieldBlockContext::VariantPayloadFields);
            variant.span.end = tokens_[current_ - 1].span().end;
        } else {
            variant.span.end = variant_name.span().end;
        }
        variants.push_back(std::move(variant));
        if (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed) {
            if (token_check(TokenKind::RightBrace) == TokenCheckState::Matches) {
                break;
            }
            continue;
        }
        if (variant_after_missing_separator(peek().kind(), peek(1).kind())
            == VariantSeparatorRecovery::ContinueWithNextVariant) {
            diagnostics_.error(peek().span(), "expected ',' between variants");
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
    while (token_check(TokenKind::RightParen) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        ast::Parameter param;
        if (consume_if(TokenKind::KeywordAs) == TokenConsumptionState::Consumed) {
            param.authority = ast::ParameterAuthority::PermitBinding;
        }
        const Token param_name = expect(TokenKind::Identifier, "expected parameter name");
        expect(TokenKind::Colon, "expected ':' after parameter name");
        ast::TypeRef type = parse_type();
        param.name = token_text(param_name);
        param.type = std::move(type);
        param.span = SourceSpan{param_name.span().begin, param.type.span.end};
        params.push_back(std::move(param));
        if (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed) {
            if (token_check(TokenKind::RightParen) == TokenCheckState::Matches) {
                break;
            }
            continue;
        }
        if (parameter_after_missing_separator(peek().kind(), peek(1).kind(), peek(2).kind())
            == ParameterSeparatorRecovery::ContinueWithNextParameter) {
            diagnostics_.error(peek().span(), "expected ',' between parameters");
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
    while (token_check(TokenKind::RightParen) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        if (token_check(TokenKind::KeywordAs) == TokenCheckState::Matches) {
            diagnostics_.error(peek().span(),
                                "foreign functions may not use permit parameters (`as name: Type` is not allowed here)");
            while (token_check(TokenKind::RightParen) == TokenCheckState::DifferentToken
                   && cursor_state() == ParserCursorState::HasMoreTokens) {
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
        param.span = SourceSpan{param_name.span().begin, param.type.span.end};
        params.push_back(std::move(param));
        if (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed) {
            if (token_check(TokenKind::RightParen) == TokenCheckState::Matches) {
                break;
            }
            continue;
        }
        if (parameter_after_missing_separator(peek().kind(), peek(1).kind(), peek(2).kind())
            == ParameterSeparatorRecovery::ContinueWithNextParameter) {
            diagnostics_.error(peek().span(), "expected ',' between parameters");
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
    if (token_check(TokenKind::LeftAngle) == TokenCheckState::Matches) {
        diagnostics_.error(peek().span(), "foreign functions may not declare generic parameters");
        parse_generic_params();
    }
    const std::size_t begin = peek().span().begin;
    signature.params = parse_foreign_parameter_list();
    signature.return_type = parse_return_type_after_parameter_list();
    for (;;) {
        if (token_check(TokenKind::KeywordFails) == TokenCheckState::Matches) {
            const Token fails_tok = advance();
            diagnostics_.error(fails_tok.span(), "foreign functions may not declare `fails`");
            parse_type();
            continue;
        }
        if (token_check(TokenKind::KeywordGrants) == TokenCheckState::Matches) {
            const Token grants_tok = advance();
            diagnostics_.error(grants_tok.span(), "foreign functions may not declare `grants`");
            parse_type();
            continue;
        }
        if (token_check(TokenKind::KeywordProves) == TokenCheckState::Matches) {
            const Token proves_tok = advance();
            diagnostics_.error(proves_tok.span(), "foreign functions may not declare `proves`");
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
    const std::size_t begin = peek().span().begin;
    signature.params = parse_parameter_list();
    signature.return_type = parse_return_type_after_parameter_list();
    for (;;) {
        if (consume_if(TokenKind::KeywordFails) == TokenConsumptionState::Consumed) {
            ast::TypeRef fails_type = parse_type();
            if (signature.failure.behavior() == ast::FunctionFailureBehavior::YieldsReason) {
                diagnostics_.error(fails_type.span, "function signature repeats `fails`");
            } else {
                signature.failure = ast::FunctionFailureContract::yields_reason(std::move(fails_type));
            }
            continue;
        }
        if (consume_if(TokenKind::KeywordGrants) == TokenConsumptionState::Consumed) {
            ast::TypeRef grants_type = parse_type();
            if (signature.authority.effect() == ast::FunctionAuthorityEffect::GrantsScopedPermit) {
                diagnostics_.error(grants_type.span, "function signature repeats `grants`");
            } else {
                signature.authority = ast::FunctionAuthorityContract::grants_scoped_permit(std::move(grants_type));
            }
            continue;
        }
        if (consume_if(TokenKind::KeywordProves) == TokenConsumptionState::Consumed) {
            signature.proves_types.push_back(parse_type());
            continue;
        }
        break;
    }
    std::size_t end = signature.return_type.span.end;
    if (signature.failure.behavior() == ast::FunctionFailureBehavior::YieldsReason) {
        end = signature.failure.reason_type().span.end;
    }
    if (signature.authority.effect() == ast::FunctionAuthorityEffect::GrantsScopedPermit) {
        end = signature.authority.permit_type().span.end;
    }
    if (!signature.proves_types.empty()) {
        end = signature.proves_types.back().span.end;
    }
    signature.span = SourceSpan{begin, end};
    return signature;
}

ast::TypeRef Parser::parse_return_type_after_parameter_list() {
    if (consume_if(TokenKind::Arrow) == TokenConsumptionState::Consumed) {
        return parse_type();
    }

    diagnostics_.error(peek().span(), "expected '->' after parameter list");
    if (token_check(TokenKind::Identifier) == TokenCheckState::Matches) {
        return parse_type();
    }

    ast::TypeRef missing_type;
    missing_type.span = peek().span();
    return missing_type;
}

ast::TypeRef Parser::parse_type() {
    const Token first = expect(TokenKind::Identifier, "expected type name");
    ast::TypeRef type;
    type.path.push_back(token_text(first));
    type.span.begin = first.span().begin;

    while (consume_if(TokenKind::DoubleColon) == TokenConsumptionState::Consumed) {
        const Token segment = expect(TokenKind::Identifier, "expected type name segment after '::'");
        type.path.push_back(token_text(segment));
        type.span.end = segment.span().end;
    }

    if (consume_if(TokenKind::LeftAngle) == TokenConsumptionState::Consumed) {
        do {
            type.args.push_back(parse_type());
        } while (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed);
        const Token end = expect(TokenKind::RightAngle, "expected '>' after type arguments");
        type.span.end = end.span().end;
    } else {
        type.span.end = tokens_[current_ - 1].span().end;
    }

    return type;
}

std::vector<ast::TypeRef> Parser::parse_type_argument_list() {
    std::vector<ast::TypeRef> args;
    expect(TokenKind::LeftAngle, "expected '<' before type argument list");
    do {
        args.push_back(parse_type());
    } while (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed);
    expect(TokenKind::RightAngle, "expected '>' after type argument list");
    return args;
}

std::vector<std::string> Parser::parse_path() {
    std::vector<std::string> path;
    const Token first = expect(TokenKind::Identifier, "expected identifier");
    path.push_back(token_text(first));
    while (consume_if(TokenKind::DoubleColon) == TokenConsumptionState::Consumed) {
        const Token segment = expect(TokenKind::Identifier, "expected identifier after '::'");
        path.push_back(token_text(segment));
    }
    return path;
}

std::vector<ast::RecordFieldInit> Parser::parse_record_field_initializers() {
    std::vector<ast::RecordFieldInit> fields;
    expect(TokenKind::LeftBrace, "expected '{'");
    while (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        const Token name = expect(TokenKind::Identifier, "expected field name");
        ast::RecordFieldInit init;
        init.name = token_text(name);
        init.span.begin = name.span().begin;
        if (consume_if(TokenKind::Colon) == TokenConsumptionState::Consumed) {
            init.value = parse_expr();
            init.span.end = init.value != nullptr ? init.value->span.end : name.span().end;
        } else {
            init.value = make_shorthand_path_expr(name);
            init.spelling = ast::FieldInitSpelling::ShorthandBinding;
            init.span.end = name.span().end;
        }
        fields.push_back(std::move(init));
        if (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed) {
            if (token_check(TokenKind::RightBrace) == TokenCheckState::Matches) {
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
    block->span.begin = open.span().begin;

    while (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        if (token_check(TokenKind::KeywordLet) == TokenCheckState::Matches) {
            std::unique_ptr<ast::Stmt> stmt = parse_statement();
            if (stmt != nullptr) {
                block->statements.push_back(std::move(stmt));
            }
            consume_optional_statement_terminator();
            continue;
        }

        if (expression_start_state(peek().kind()) == ExpressionStartState::DoesNotBeginExpression) {
            diagnostics_.error(peek().span(), "expected statement or expression inside block");
            if (cursor_state() == ParserCursorState::HasMoreTokens) {
                advance();
            }
            continue;
        }

        std::unique_ptr<ast::Expr> expr = parse_expr();
        if (expr == nullptr) {
            break;
        }

        if (consume_if(TokenKind::Semicolon) == TokenConsumptionState::Consumed) {
            auto stmt = std::make_unique<ast::ExprStmt>();
            stmt->span = expr->span;
            stmt->expr = std::move(expr);
            block->statements.push_back(std::move(stmt));
            continue;
        }

        if (token_check(TokenKind::RightBrace) == TokenCheckState::Matches) {
            block->result = std::move(expr);
            break;
        }

        if (token_check(TokenKind::KeywordLet) == TokenCheckState::Matches
            || expression_start_state(peek().kind()) == ExpressionStartState::BeginsExpression) {
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
    block->span.end = end.span().end;
    return block;
}

std::unique_ptr<ast::Stmt> Parser::parse_statement() {
    const Token start = expect(TokenKind::KeywordLet, "expected 'let'");
    const Token name = expect(TokenKind::Identifier, "expected binding name");
    expect(TokenKind::Equals, "expected '=' after binding name");
    auto stmt = std::make_unique<ast::LetStmt>();
    stmt->name = token_text(name);
    stmt->initializer = parse_expr();
    stmt->span = SourceSpan{start.span().begin, stmt->initializer != nullptr ? stmt->initializer->span.end : name.span().end};
    return stmt;
}

std::unique_ptr<ast::Expr> Parser::parse_expr() {
    if (token_check(TokenKind::KeywordMatch) == TokenCheckState::Matches) {
        return parse_match_expr();
    }
    if (token_check(TokenKind::KeywordGrant) == TokenCheckState::Matches) {
        return parse_grant_expr();
    }
    return parse_try_expr();
}

std::unique_ptr<ast::Expr> Parser::parse_match_expr() {
    const Token start = expect(TokenKind::KeywordMatch, "expected 'match'");
    auto match_expr = std::make_unique<ast::MatchExpr>();
    match_expr->scrutinee = parse_expr();
    expect(TokenKind::LeftBrace, "expected '{' after match scrutinee");

    while (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken
           && cursor_state() == ParserCursorState::HasMoreTokens) {
        ast::MatchArm arm;
        arm.pattern = parse_pattern();
        expect(TokenKind::FatArrow, "expected '=>' after match pattern");
        arm.body = parse_expr();
        arm.span = SourceSpan{
            arm.pattern != nullptr ? arm.pattern->span.begin : peek().span().begin,
            arm.body != nullptr ? arm.body->span.end : peek().span().end,
        };
        match_expr->arms.push_back(std::move(arm));
        if (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed) {
            if (token_check(TokenKind::RightBrace) == TokenCheckState::Matches) {
                break;
            }
            continue;
        }
        if (match_arm_after_missing_separator(peek().kind(), peek(1).kind())
            == MatchArmSeparatorRecovery::ContinueWithNextArm) {
            diagnostics_.error(peek().span(), "expected ',' between match arms");
            continue;
        }
        break;
    }

    const Token end = expect(TokenKind::RightBrace, "expected '}' after match arms");
    match_expr->span = SourceSpan{start.span().begin, end.span().end};
    return match_expr;
}

std::unique_ptr<ast::Expr> Parser::parse_try_expr() {
    if (consume_if(TokenKind::KeywordTry) == TokenConsumptionState::Consumed) {
        const Token start = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::TryExpr>();
        expr->operand = parse_expr();
        expr->span = SourceSpan{start.span().begin, expr->operand != nullptr ? expr->operand->span.end : start.span().end};
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
    expr->span = SourceSpan{start.span().begin, expr->body != nullptr ? expr->body->span.end : binder.span().end};
    return expr;
}

std::unique_ptr<ast::Expr> Parser::parse_fail_expr() {
    if (consume_if(TokenKind::KeywordFail) == TokenConsumptionState::Consumed) {
        const Token start = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::FailExpr>();
        expr->path = parse_path();
        expr->span.begin = start.span().begin;
        expr->span.end = tokens_[current_ - 1].span().end;
        if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches) {
            expr->fields = parse_record_field_initializers();
            expr->span.end = tokens_[current_ - 1].span().end;
        }
        return expr;
    }
    return parse_prove_expr();
}

std::unique_ptr<ast::Expr> Parser::parse_prove_expr() {
    if (consume_if(TokenKind::KeywordProve) == TokenConsumptionState::Consumed) {
        const Token start = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::ProveExpr>();
        expr->path = parse_path();
        expr->span.begin = start.span().begin;
        expr->span.end = tokens_[current_ - 1].span().end;
        if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches) {
            expr->fields = parse_record_field_initializers();
            expr->span.end = tokens_[current_ - 1].span().end;
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
        if (token_check(TokenKind::LeftAngle) == TokenCheckState::Matches) {
            if (expr->kind != ast::ExprKind::Path) {
                diagnostics_.error(peek().span(), "type arguments may only be applied to named calls or record construction");
                break;
            }
            auto& path_expr = static_cast<ast::PathExpr&>(*expr);
            type_args = parse_type_argument_list();
            if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches
                && record_initializer_lookahead() == RecordInitializerLookahead::RecordInitializer) {
                auto construct = std::make_unique<ast::ConstructExpr>();
                construct->path = std::move(path_expr.path);
                construct->type_args = std::move(type_args);
                construct->fields = parse_record_field_initializers();
                construct->span = SourceSpan{expr->span.begin, tokens_[current_ - 1].span().end};
                expr = std::move(construct);
                continue;
            }
            if (token_check(TokenKind::LeftParen) == TokenCheckState::DifferentToken) {
                diagnostics_.error(tokens_[current_ - 1].span(),
                                   "type arguments must be followed by an argument list or record initializer");
                break;
            }
        }
        if (consume_if(TokenKind::LeftParen) == TokenConsumptionState::Consumed) {
            const Token open = tokens_[current_ - 1];
            auto call = std::make_unique<ast::CallExpr>();
            call->callee = std::move(expr);
            call->type_args = std::move(type_args);
            while (token_check(TokenKind::RightParen) == TokenCheckState::DifferentToken
                   && cursor_state() == ParserCursorState::HasMoreTokens) {
                if (token_check(TokenKind::KeywordAs) == TokenCheckState::Matches) {
                    // permit argument: `as name`
                    advance(); // consume 'as'
                    auto permit_arg = std::make_unique<ast::PathExpr>();
                    permit_arg->argument_role = ast::PathArgumentRole::PermitArgument;
                    const Token permit_name = expect(TokenKind::Identifier, "expected permit name after 'as'");
                    permit_arg->path.push_back(token_text(permit_name));
                    permit_arg->span = permit_name.span();
                    call->args.push_back(std::move(permit_arg));
                } else {
                    call->args.push_back(parse_expr());
                }
                if (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed) {
                    if (token_check(TokenKind::RightParen) == TokenCheckState::Matches) {
                        break;
                    }
                    continue;
                }
                break;
            }
            const Token close = expect(TokenKind::RightParen, "expected ')' after argument list");
            call->span = SourceSpan{call->callee != nullptr ? call->callee->span.begin : open.span().begin, close.span().end};
            expr = std::move(call);
            continue;
        }
        if (consume_if(TokenKind::Dot) == TokenConsumptionState::Consumed) {
            const Token field_name = expect(TokenKind::Identifier, "expected field name after '.'");
            auto field_access = std::make_unique<ast::FieldAccessExpr>();
            field_access->object = std::move(expr);
            field_access->field_name = token_text(field_name);
            field_access->span = SourceSpan{field_access->object->span.begin, field_name.span().end};
            expr = std::move(field_access);
            continue;
        }
        break;
    }
    return expr;
}

std::unique_ptr<ast::Expr> Parser::parse_primary_expr() {
    if (consume_if(TokenKind::Number) == TokenConsumptionState::Consumed) {
        const Token token = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::NumberLiteralExpr>(token_text(token));
        expr->span = token.span();
        return expr;
    }
    if (consume_if(TokenKind::String) == TokenConsumptionState::Consumed) {
        const Token token = tokens_[current_ - 1];
        auto expr = std::make_unique<ast::StringLiteralExpr>(token_text(token));
        expr->span = token.span();
        return expr;
    }
    if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches) {
        return parse_block_expr();
    }
    if (token_check(TokenKind::Identifier) == TokenCheckState::Matches) {
        const std::size_t begin = peek().span().begin;
        std::vector<std::string> path = parse_path();
        if (token_check(TokenKind::LeftBrace) == TokenCheckState::Matches
            && record_initializer_lookahead() == RecordInitializerLookahead::RecordInitializer) {
            auto expr = std::make_unique<ast::ConstructExpr>();
            expr->path = std::move(path);
            expr->fields = parse_record_field_initializers();
            expr->span = SourceSpan{begin, tokens_[current_ - 1].span().end};
            return expr;
        }
        auto expr = std::make_unique<ast::PathExpr>();
        expr->path = std::move(path);
        expr->span = SourceSpan{begin, tokens_[current_ - 1].span().end};
        return expr;
    }

    diagnostics_.error(peek().span(), "expected expression");
    return nullptr;
}

std::unique_ptr<ast::Pattern> Parser::parse_pattern() {
    // Check for succeeded(...) and failed(...) as keywords
    if (token_check(TokenKind::KeywordSucceeded) == TokenCheckState::Matches) {
        const Token start = advance();
        auto pattern = std::make_unique<ast::SucceededPattern>();
        expect(TokenKind::LeftParen, "expected '(' after 'succeeded'");
        if (token_check(TokenKind::Identifier) == TokenCheckState::Matches && peek().lexeme() == "_") {
            advance();
            pattern->binding = ast::SuccessPatternBinding::DiscardedValue;
        } else {
            const Token binding = expect(TokenKind::Identifier, "expected binding or '_' inside succeeded pattern");
            pattern->binding = ast::SuccessPatternBinding::NamedBinding;
            pattern->binding_name = token_text(binding);
        }
        const Token close = expect(TokenKind::RightParen, "expected ')' after succeeded pattern");
        pattern->span = SourceSpan{start.span().begin, close.span().end};
        return pattern;
    }

    if (token_check(TokenKind::KeywordFailed) == TokenCheckState::Matches) {
        const Token start = advance();
        auto pattern = std::make_unique<ast::FailedPattern>();
        expect(TokenKind::LeftParen, "expected '(' after 'failed'");
        const std::size_t inner_begin = peek().span().begin;
        std::vector<std::string> inner_path = parse_path();
        const SourceSpan inner_span{inner_begin, tokens_[current_ - 1].span().end};
        pattern->variant = parse_variant_pattern_from_path(std::move(inner_path), inner_span);
        const Token close = expect(TokenKind::RightParen, "expected ')' after failed pattern");
        pattern->span = SourceSpan{start.span().begin, close.span().end};
        return pattern;
    }

    const std::size_t begin = peek().span().begin;
    std::vector<std::string> path = parse_path();
    const SourceSpan path_span{begin, tokens_[current_ - 1].span().end};

    // Legacy support: also handle succeeded/failed as identifiers in path
    if (path.size() == 1 && path.front() == "succeeded") {
        auto pattern = std::make_unique<ast::SucceededPattern>();
        expect(TokenKind::LeftParen, "expected '(' after 'succeeded'");
        const Token binding = expect(TokenKind::Identifier, "expected binding or '_' inside succeeded pattern");
        if (binding.lexeme() == "_") {
            pattern->binding = ast::SuccessPatternBinding::DiscardedValue;
        } else {
            pattern->binding = ast::SuccessPatternBinding::NamedBinding;
            pattern->binding_name = token_text(binding);
        }
        const Token close = expect(TokenKind::RightParen, "expected ')' after succeeded pattern");
        pattern->span = SourceSpan{begin, close.span().end};
        return pattern;
    }

    if (path.size() == 1 && path.front() == "failed") {
        auto pattern = std::make_unique<ast::FailedPattern>();
        expect(TokenKind::LeftParen, "expected '(' after 'failed'");
        const std::size_t inner_begin = peek().span().begin;
        std::vector<std::string> inner_path = parse_path();
        const SourceSpan inner_span{inner_begin, tokens_[current_ - 1].span().end};
        pattern->variant = parse_variant_pattern_from_path(std::move(inner_path), inner_span);
        const Token close = expect(TokenKind::RightParen, "expected ')' after failed pattern");
        pattern->span = SourceSpan{begin, close.span().end};
        return pattern;
    }

    return parse_variant_pattern_from_path(std::move(path), path_span);
}

std::unique_ptr<ast::VariantPattern> Parser::parse_variant_pattern_from_path(std::vector<std::string> path,
                                                                             SourceSpan path_span) {
    auto pattern = std::make_unique<ast::VariantPattern>();
    pattern->path = std::move(path);
    pattern->span = path_span;

    if (consume_if(TokenKind::LeftBrace) == TokenConsumptionState::NotConsumed) {
        return pattern;
    }

    const Token open = tokens_[current_ - 1];
    if (consume_if(TokenKind::DotDot) == TokenConsumptionState::Consumed) {
        pattern->payload_mode = ast::VariantPattern::PayloadMode::Ignore;
        const Token close = expect(TokenKind::RightBrace, "expected '}' after '..'");
        pattern->span = SourceSpan{path_span.begin, close.span().end};
        return pattern;
    }

    if (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken) {
        pattern->payload_mode = ast::VariantPattern::PayloadMode::Bindings;
        while (token_check(TokenKind::RightBrace) == TokenCheckState::DifferentToken
               && cursor_state() == ParserCursorState::HasMoreTokens) {
            const Token field = expect(TokenKind::Identifier, "expected field name in pattern");
            ast::VariantPattern::Binding binding;
            binding.field_name = token_text(field);
            binding.binding_name = binding.field_name;
            binding.span.begin = field.span().begin;
            if (consume_if(TokenKind::Colon) == TokenConsumptionState::Consumed) {
                const Token value = expect(TokenKind::Identifier, "expected binding name after ':'");
                binding.binding_name = token_text(value);
                binding.span.end = value.span().end;
            } else {
                binding.span.end = field.span().end;
            }
            pattern->bindings.push_back(std::move(binding));
            if (consume_if(TokenKind::Comma) == TokenConsumptionState::Consumed) {
                if (token_check(TokenKind::RightBrace) == TokenCheckState::Matches) {
                    break;
                }
                continue;
            }
            break;
        }
    } else {
        diagnostics_.error(open.span(), "empty payload pattern is not allowed; bind fields or use '{ .. }'");
        pattern->payload_mode = ast::VariantPattern::PayloadMode::Bindings;
    }

    const Token close = expect(TokenKind::RightBrace, "expected '}' after pattern payload");
    pattern->span = SourceSpan{path_span.begin, close.span().end};
    return pattern;
}

void Parser::consume_optional_declaration_terminator() {
    if (consume_if(TokenKind::Semicolon) == TokenConsumptionState::Consumed) {
        return;
    }
    if (token_check_any({TokenKind::RightBrace, TokenKind::EndOfFile}) == TokenCheckState::Matches
        || declaration_start_state(peek().kind()) == DeclarationStartState::StartsDeclaration) {
        return;
    }
}

void Parser::consume_optional_statement_terminator() {
    if (token_check(TokenKind::Semicolon) == TokenCheckState::Matches) {
        advance();
    }
}

ExpressionStartState Parser::expression_start_state(TokenKind kind) const noexcept {
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
        return ExpressionStartState::BeginsExpression;
    default:
        return ExpressionStartState::DoesNotBeginExpression;
    }
}


RecordInitializerLookahead Parser::record_initializer_lookahead() const noexcept {
    if (token_check(TokenKind::LeftBrace) == TokenCheckState::DifferentToken) {
        return RecordInitializerLookahead::BlockExpression;
    }
    if (peek(1).kind() == TokenKind::RightBrace) {
        return RecordInitializerLookahead::RecordInitializer;
    }
    if (peek(1).kind() != TokenKind::Identifier) {
        return RecordInitializerLookahead::BlockExpression;
    }
    if (peek(2).kind() == TokenKind::Colon
        || peek(2).kind() == TokenKind::Comma
        || peek(2).kind() == TokenKind::RightBrace) {
        return RecordInitializerLookahead::RecordInitializer;
    }
    return RecordInitializerLookahead::BlockExpression;
}

} // namespace evident
