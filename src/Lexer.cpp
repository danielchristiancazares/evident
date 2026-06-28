#include "evident/Lexer.hpp"

#include <cctype>
#include <string>
#include <unordered_map>

namespace evident {

namespace {

const std::unordered_map<std::string_view, TokenKind> kKeywords = {
    {"module", TokenKind::KeywordModule},
    {"import", TokenKind::KeywordImport},
    {"public", TokenKind::KeywordPublic},
    {"record", TokenKind::KeywordRecord},
    {"state", TokenKind::KeywordState},
    {"reason", TokenKind::KeywordReason},
    {"proof", TokenKind::KeywordProof},
    {"permit", TokenKind::KeywordPermit},
    {"phase", TokenKind::KeywordPhase},
    {"fn", TokenKind::KeywordFn},
    {"foreign", TokenKind::KeywordForeign},
    {"fails", TokenKind::KeywordFails},
    {"grants", TokenKind::KeywordGrants},
    {"proves", TokenKind::KeywordProves},
    {"match", TokenKind::KeywordMatch},
    {"let", TokenKind::KeywordLet},
    {"try", TokenKind::KeywordTry},
    {"fail", TokenKind::KeywordFail},
    {"grant", TokenKind::KeywordGrant},
    {"as", TokenKind::KeywordAs},
    {"prove", TokenKind::KeywordProve},
    {"domain", TokenKind::KeywordDomain},
    {"boundary", TokenKind::KeywordBoundary},
    {"foundation", TokenKind::KeywordFoundation},
    {"hazard", TokenKind::KeywordHazard},
    {"fields", TokenKind::KeywordFields},
    {"positions", TokenKind::KeywordPositions},
    {"succeeded", TokenKind::KeywordSucceeded},
    {"failed", TokenKind::KeywordFailed},
};

enum class IdentifierStartRole {
    StartsIdentifier,
    ExcludedFromIdentifierStart,
};

enum class IdentifierContinuationRole {
    ContinuesIdentifier,
    EndsIdentifier,
};

IdentifierStartRole identifier_start_role(char ch) {
    const unsigned char u = static_cast<unsigned char>(ch);
    if (std::isalpha(u) != 0 || ch == '_') {
        return IdentifierStartRole::StartsIdentifier;
    }
    return IdentifierStartRole::ExcludedFromIdentifierStart;
}

IdentifierContinuationRole identifier_continuation_role(char ch) {
    const unsigned char u = static_cast<unsigned char>(ch);
    if (std::isalnum(u) != 0 || ch == '_') {
        return IdentifierContinuationRole::ContinuesIdentifier;
    }
    return IdentifierContinuationRole::EndsIdentifier;
}

bool is_decimal_digit(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

bool is_hex_digit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

unsigned hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10U + static_cast<unsigned>(ch - 'a');
    }
    return 10U + static_cast<unsigned>(ch - 'A');
}

bool is_surrogate_code_point(unsigned value) {
    return value >= 0xD800U && value <= 0xDFFFU;
}

bool is_unicode_line_separator_at(std::string_view text, std::size_t offset) {
    return offset + 2 < text.size()
        && static_cast<unsigned char>(text[offset]) == 0xE2U
        && static_cast<unsigned char>(text[offset + 1]) == 0x80U
        && (static_cast<unsigned char>(text[offset + 2]) == 0xA8U
            || static_cast<unsigned char>(text[offset + 2]) == 0xA9U);
}

} // namespace

Lexer::Lexer(const SourceFile& source, DiagnosticSink& diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

std::vector<Token> Lexer::lex() {
    std::vector<Token> tokens;
    while (source_cursor_state() == SourceCursorState::HasMoreSource) {
        skip_whitespace_and_comments();
        if (source_cursor_state() == SourceCursorState::ReachedEnd) {
            break;
        }

        const std::size_t begin = offset_;
        const char ch = advance();
        switch (ch) {
        case '{':
            tokens.push_back(make_token(TokenKind::LeftBrace, begin, offset_));
            break;
        case '}':
            tokens.push_back(make_token(TokenKind::RightBrace, begin, offset_));
            break;
        case '(':
            tokens.push_back(make_token(TokenKind::LeftParen, begin, offset_));
            break;
        case ')':
            tokens.push_back(make_token(TokenKind::RightParen, begin, offset_));
            break;
        case '[':
            tokens.push_back(make_token(TokenKind::LeftBracket, begin, offset_));
            break;
        case ']':
            tokens.push_back(make_token(TokenKind::RightBracket, begin, offset_));
            break;
        case '<':
            tokens.push_back(make_token(TokenKind::LeftAngle, begin, offset_));
            break;
        case '>':
            tokens.push_back(make_token(TokenKind::RightAngle, begin, offset_));
            break;
        case ',':
            tokens.push_back(make_token(TokenKind::Comma, begin, offset_));
            break;
        case ':':
            if (match_next(':') == CharacterMatchState::ConsumedExpectedCharacter) {
                tokens.push_back(make_token(TokenKind::DoubleColon, begin, offset_));
            } else {
                tokens.push_back(make_token(TokenKind::Colon, begin, offset_));
            }
            break;
        case ';':
            tokens.push_back(make_token(TokenKind::Semicolon, begin, offset_));
            break;
        case '.':
            if (match_next('.') == CharacterMatchState::ConsumedExpectedCharacter) {
                tokens.push_back(make_token(TokenKind::DotDot, begin, offset_));
            } else {
                tokens.push_back(make_token(TokenKind::Dot, begin, offset_));
            }
            break;
        case '=':
            if (match_next('>') == CharacterMatchState::ConsumedExpectedCharacter) {
                tokens.push_back(make_token(TokenKind::FatArrow, begin, offset_));
            } else {
                tokens.push_back(make_token(TokenKind::Equals, begin, offset_));
            }
            break;
        case '-':
            if (match_next('>') == CharacterMatchState::ConsumedExpectedCharacter) {
                tokens.push_back(make_token(TokenKind::Arrow, begin, offset_));
            } else {
                report_unknown(begin, ch);
                tokens.push_back(make_token(TokenKind::RejectedLexeme, begin, offset_));
            }
            break;
        case '"':
            --offset_;
            tokens.push_back(lex_string());
            break;
        default:
            if (identifier_start_role(ch) == IdentifierStartRole::StartsIdentifier) {
                --offset_;
                tokens.push_back(lex_identifier_or_keyword());
            } else if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                --offset_;
                tokens.push_back(lex_number());
            } else {
                report_unknown(begin, ch);
                tokens.push_back(make_token(TokenKind::RejectedLexeme, begin, offset_));
            }
            break;
        }
    }

    tokens.push_back(
        Token::classified(TokenKind::EndOfFile, {}, SourceSpan{source_.text().size(), source_.text().size()}));
    return tokens;
}

SourceCursorState Lexer::source_cursor_state() const noexcept {
    if (offset_ >= source_.text().size()) {
        return SourceCursorState::ReachedEnd;
    }
    return SourceCursorState::HasMoreSource;
}

char Lexer::peek(std::size_t lookahead) const noexcept {
    const std::size_t index = offset_ + lookahead;
    if (index >= source_.text().size()) {
        return '\0';
    }
    return source_.text()[index];
}

char Lexer::advance() noexcept {
    if (source_cursor_state() == SourceCursorState::ReachedEnd) {
        return '\0';
    }
    return source_.text()[offset_++];
}

CharacterMatchState Lexer::match_next(char expected) noexcept {
    if (source_cursor_state() == SourceCursorState::ReachedEnd || source_.text()[offset_] != expected) {
        return CharacterMatchState::ExpectedCharacterAbsent;
    }
    ++offset_;
    return CharacterMatchState::ConsumedExpectedCharacter;
}

void Lexer::skip_whitespace_and_comments() {
    for (;;) {
        while (source_cursor_state() == SourceCursorState::HasMoreSource
               && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            advance();
        }

        if (peek() == '-' && peek(1) == '-') {
            while (source_cursor_state() == SourceCursorState::HasMoreSource && peek() != '\n') {
                advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::lex_identifier_or_keyword() {
    const std::size_t begin = offset_;
    while (source_cursor_state() == SourceCursorState::HasMoreSource
           && identifier_continuation_role(peek()) == IdentifierContinuationRole::ContinuesIdentifier) {
        advance();
    }
    const Token token = make_token(TokenKind::Identifier, begin, offset_);
    const auto it = kKeywords.find(token.lexeme());
    if (it == kKeywords.end()) {
        return token;
    }
    return Token::classified(it->second, token.lexeme(), token.span());
}

Token Lexer::lex_number() {
    const std::size_t begin = offset_;
    while (source_cursor_state() == SourceCursorState::HasMoreSource && is_decimal_digit(peek())) {
        advance();
    }
    if (peek() == '.' && peek(1) != '.' && is_decimal_digit(peek(1))) {
        advance();
        while (source_cursor_state() == SourceCursorState::HasMoreSource && is_decimal_digit(peek())) {
            advance();
        }
    }
    if ((peek() == 'e' || peek() == 'E')
        && (is_decimal_digit(peek(1))
            || ((peek(1) == '+' || peek(1) == '-') && is_decimal_digit(peek(2))))) {
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        while (source_cursor_state() == SourceCursorState::HasMoreSource && is_decimal_digit(peek())) {
            advance();
        }
    }
    return make_token(TokenKind::Number, begin, offset_);
}

Token Lexer::lex_string() {
    const std::size_t begin = offset_;
    advance();
    bool reported_error = false;
    auto report_once = [&](SourceSpan span, const std::string& message) {
        if (!reported_error) {
            diagnostics_.error(span, message);
            reported_error = true;
        }
    };
    while (source_cursor_state() == SourceCursorState::HasMoreSource) {
        const std::size_t char_begin = offset_;
        if (is_unicode_line_separator_at(source_.text(), char_begin)) {
            offset_ += 3;
            report_once(SourceSpan{char_begin, offset_}, "unescaped line terminator in string literal");
            continue;
        }
        const char ch = advance();
        if (ch == '"') {
            return make_token(TokenKind::String, begin, offset_);
        }
        if (ch == '\n' || ch == '\r') {
            report_once(SourceSpan{char_begin, offset_}, "unescaped line terminator in string literal");
            continue;
        }
        if (ch == '\0') {
            report_once(SourceSpan{char_begin, offset_}, "unescaped NUL byte in string literal");
            continue;
        }
        if (ch != '\\') {
            continue;
        }
        if (source_cursor_state() == SourceCursorState::ReachedEnd) {
            report_once(SourceSpan{char_begin, offset_}, "unterminated string escape");
            break;
        }
        if (is_unicode_line_separator_at(source_.text(), offset_)) {
            offset_ += 3;
            report_once(SourceSpan{char_begin, offset_}, "unterminated string escape");
            continue;
        }

        const char escaped = advance();
        switch (escaped) {
        case '"':
        case '\\':
        case 'n':
        case 'r':
        case 't':
        case '0':
            break;
        case '\n':
        case '\r':
            report_once(SourceSpan{char_begin, offset_}, "unterminated string escape");
            break;
        case 'u': {
            if (peek() != '{') {
                report_once(SourceSpan{char_begin, offset_}, "invalid Unicode escape in string literal");
                break;
            }
            advance();
            const std::size_t digits_begin = offset_;
            unsigned value = 0;
            std::size_t digit_count = 0;
            bool too_many_digits = false;
            while (source_cursor_state() == SourceCursorState::HasMoreSource && is_hex_digit(peek())) {
                const char digit = advance();
                if (digit_count < 6) {
                    value = value * 16U + hex_digit_value(digit);
                } else {
                    too_many_digits = true;
                }
                ++digit_count;
            }
            const bool has_closing_brace = peek() == '}';
            if (has_closing_brace) {
                advance();
            }
            if (digit_count == 0 || too_many_digits || !has_closing_brace) {
                report_once(SourceSpan{char_begin, offset_}, "invalid Unicode escape in string literal");
                break;
            }
            if (value > 0x10FFFFU || is_surrogate_code_point(value)) {
                report_once(SourceSpan{digits_begin, offset_ - 1},
                            "Unicode escape does not name a Unicode scalar value");
            }
            break;
        }
        default:
            report_once(SourceSpan{char_begin, offset_},
                        std::string("invalid string escape '\\") + escaped + "'");
            break;
        }
    }
    if (!reported_error) {
        diagnostics_.error(SourceSpan{begin, offset_}, "unterminated string literal");
    }
    return make_token(TokenKind::String, begin, offset_);
}

Token Lexer::make_token(TokenKind kind, std::size_t begin, std::size_t end) const {
    return Token::classified(kind, source_.slice(SourceSpan{begin, end}), SourceSpan{begin, end});
}

void Lexer::report_unknown(std::size_t position, char ch) {
    diagnostics_.error(SourceSpan{position, position + 1}, std::string("unexpected character '") + ch + "'");
}

} // namespace evident
