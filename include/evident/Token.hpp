#pragma once

#include "evident/Source.hpp"

#include <string_view>

namespace evident {

enum class TokenKind {
    EndOfFile,
    Identifier,
    Number,
    String,

    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    LeftAngle,
    RightAngle,
    Comma,
    Colon,
    Semicolon,
    Dot,
    DotDot,
    DoubleColon,
    Arrow,
    FatArrow,
    Equals,

    KeywordModule,
    KeywordImport,
    KeywordPublic,
    KeywordRecord,
    KeywordState,
    KeywordReason,
    KeywordProof,
    KeywordPermit,
    KeywordPhase,
    KeywordFn,
    KeywordForeign,
    KeywordFails,
    KeywordGrants,
    KeywordProves,
    KeywordMatch,
    KeywordLet,
    KeywordTry,
    KeywordFail,
    KeywordGrant,
    KeywordAs,
    KeywordProve,
    KeywordTraverse,
    KeywordCopying,
    KeywordConsuming,
    KeywordCarrying,
    KeywordDomain,
    KeywordBoundary,
    KeywordFoundation,
    KeywordHazard,
    KeywordFields,
    KeywordPositions,
    KeywordSucceeded,
    KeywordFailed,

    RejectedLexeme,
};

class Token final {
public:
    [[nodiscard]] static Token classified(TokenKind kind, std::string_view lexeme, SourceSpan span) {
        return Token(kind, lexeme, span);
    }

    [[nodiscard]] TokenKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::string_view lexeme() const noexcept { return lexeme_; }
    [[nodiscard]] SourceSpan span() const noexcept { return span_; }

private:
    TokenKind kind_;
    std::string_view lexeme_;
    SourceSpan span_;

    Token(TokenKind kind, std::string_view lexeme, SourceSpan span)
        : kind_(kind),
          lexeme_(lexeme),
          span_(span) {}
};

[[nodiscard]] std::string_view token_kind_name(TokenKind kind);

} // namespace evident
