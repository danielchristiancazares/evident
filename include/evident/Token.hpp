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
    KeywordPublic,
    KeywordStruct,
    KeywordState,
    KeywordReason,
    KeywordProof,
    KeywordPermit,
    KeywordTrait,
    KeywordFn,
    KeywordForeign,
    KeywordYields,
    KeywordMatch,
    KeywordLet,
    KeywordTry,
    KeywordFail,
    KeywordFor,

    Unknown,
};

struct Token {
    TokenKind kind = TokenKind::Unknown;
    std::string_view lexeme{};
    SourceSpan span{};
};

[[nodiscard]] std::string_view token_kind_name(TokenKind kind);

} // namespace evident
