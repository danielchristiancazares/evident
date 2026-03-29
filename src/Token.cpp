#include "evident/Token.hpp"

namespace evident {

std::string_view token_kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::EndOfFile:
        return "EndOfFile";
    case TokenKind::Identifier:
        return "Identifier";
    case TokenKind::Number:
        return "Number";
    case TokenKind::String:
        return "String";
    case TokenKind::LeftBrace:
        return "LeftBrace";
    case TokenKind::RightBrace:
        return "RightBrace";
    case TokenKind::LeftParen:
        return "LeftParen";
    case TokenKind::RightParen:
        return "RightParen";
    case TokenKind::LeftBracket:
        return "LeftBracket";
    case TokenKind::RightBracket:
        return "RightBracket";
    case TokenKind::LeftAngle:
        return "LeftAngle";
    case TokenKind::RightAngle:
        return "RightAngle";
    case TokenKind::Comma:
        return "Comma";
    case TokenKind::Colon:
        return "Colon";
    case TokenKind::Semicolon:
        return "Semicolon";
    case TokenKind::Dot:
        return "Dot";
    case TokenKind::DotDot:
        return "DotDot";
    case TokenKind::DoubleColon:
        return "DoubleColon";
    case TokenKind::Arrow:
        return "Arrow";
    case TokenKind::FatArrow:
        return "FatArrow";
    case TokenKind::Equals:
        return "Equals";
    case TokenKind::KeywordModule:
        return "module";
    case TokenKind::KeywordPublic:
        return "public";
    case TokenKind::KeywordStruct:
        return "struct";
    case TokenKind::KeywordState:
        return "state";
    case TokenKind::KeywordReason:
        return "reason";
    case TokenKind::KeywordProof:
        return "proof";
    case TokenKind::KeywordPermit:
        return "permit";
    case TokenKind::KeywordTrait:
        return "trait";
    case TokenKind::KeywordFn:
        return "fn";
    case TokenKind::KeywordForeign:
        return "foreign";
    case TokenKind::KeywordYields:
        return "yields";
    case TokenKind::KeywordMatch:
        return "match";
    case TokenKind::KeywordLet:
        return "let";
    case TokenKind::KeywordTry:
        return "try";
    case TokenKind::KeywordFail:
        return "fail";
    case TokenKind::KeywordFor:
        return "for";
    case TokenKind::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

} // namespace evident
