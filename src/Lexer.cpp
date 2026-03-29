#include "evident/Lexer.hpp"

#include <cctype>
#include <unordered_map>

namespace evident {

namespace {

const std::unordered_map<std::string_view, TokenKind> kKeywords = {
    {"module", TokenKind::KeywordModule},
    {"public", TokenKind::KeywordPublic},
    {"struct", TokenKind::KeywordStruct},
    {"state", TokenKind::KeywordState},
    {"reason", TokenKind::KeywordReason},
    {"proof", TokenKind::KeywordProof},
    {"permit", TokenKind::KeywordPermit},
    {"trait", TokenKind::KeywordTrait},
    {"fn", TokenKind::KeywordFn},
    {"foreign", TokenKind::KeywordForeign},
    {"yields", TokenKind::KeywordYields},
    {"match", TokenKind::KeywordMatch},
    {"let", TokenKind::KeywordLet},
    {"try", TokenKind::KeywordTry},
    {"fail", TokenKind::KeywordFail},
    {"for", TokenKind::KeywordFor},
};

bool is_ident_start(char ch) {
    const unsigned char u = static_cast<unsigned char>(ch);
    return std::isalpha(u) != 0 || ch == '_';
}

bool is_ident_continue(char ch) {
    const unsigned char u = static_cast<unsigned char>(ch);
    return std::isalnum(u) != 0 || ch == '_';
}

} // namespace

Lexer::Lexer(const SourceFile& source, DiagnosticSink& diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

std::vector<Token> Lexer::lex() {
    std::vector<Token> tokens;
    while (!at_end()) {
        skip_whitespace_and_comments();
        if (at_end()) {
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
            if (match(':')) {
                tokens.push_back(make_token(TokenKind::DoubleColon, begin, offset_));
            } else {
                tokens.push_back(make_token(TokenKind::Colon, begin, offset_));
            }
            break;
        case ';':
            tokens.push_back(make_token(TokenKind::Semicolon, begin, offset_));
            break;
        case '.':
            if (match('.')) {
                tokens.push_back(make_token(TokenKind::DotDot, begin, offset_));
            } else {
                tokens.push_back(make_token(TokenKind::Dot, begin, offset_));
            }
            break;
        case '=':
            if (match('>')) {
                tokens.push_back(make_token(TokenKind::FatArrow, begin, offset_));
            } else {
                tokens.push_back(make_token(TokenKind::Equals, begin, offset_));
            }
            break;
        case '-':
            if (match('>')) {
                tokens.push_back(make_token(TokenKind::Arrow, begin, offset_));
            } else {
                report_unknown(begin, ch);
                tokens.push_back(make_token(TokenKind::Unknown, begin, offset_));
            }
            break;
        case '"':
            --offset_;
            tokens.push_back(lex_string());
            break;
        default:
            if (is_ident_start(ch)) {
                --offset_;
                tokens.push_back(lex_identifier_or_keyword());
            } else if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                --offset_;
                tokens.push_back(lex_number());
            } else {
                report_unknown(begin, ch);
                tokens.push_back(make_token(TokenKind::Unknown, begin, offset_));
            }
            break;
        }
    }

    tokens.push_back(Token{TokenKind::EndOfFile, {}, SourceSpan{source_.text().size(), source_.text().size()}});
    return tokens;
}

bool Lexer::at_end() const noexcept {
    return offset_ >= source_.text().size();
}

char Lexer::peek(std::size_t lookahead) const noexcept {
    const std::size_t index = offset_ + lookahead;
    if (index >= source_.text().size()) {
        return '\0';
    }
    return source_.text()[index];
}

char Lexer::advance() noexcept {
    if (at_end()) {
        return '\0';
    }
    return source_.text()[offset_++];
}

bool Lexer::match(char expected) noexcept {
    if (at_end() || source_.text()[offset_] != expected) {
        return false;
    }
    ++offset_;
    return true;
}

void Lexer::skip_whitespace_and_comments() {
    for (;;) {
        while (!at_end() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
            advance();
        }

        if (peek() == '-' && peek(1) == '-') {
            while (!at_end() && peek() != '\n') {
                advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::lex_identifier_or_keyword() {
    const std::size_t begin = offset_;
    while (!at_end() && is_ident_continue(peek())) {
        advance();
    }
    const Token token = make_token(TokenKind::Identifier, begin, offset_);
    const auto it = kKeywords.find(token.lexeme);
    if (it == kKeywords.end()) {
        return token;
    }
    return Token{it->second, token.lexeme, token.span};
}

Token Lexer::lex_number() {
    const std::size_t begin = offset_;
    while (!at_end() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        advance();
    }
    return make_token(TokenKind::Number, begin, offset_);
}

Token Lexer::lex_string() {
    const std::size_t begin = offset_;
    advance();
    while (!at_end()) {
        const char ch = advance();
        if (ch == '"') {
            return make_token(TokenKind::String, begin, offset_);
        }
        if (ch == '\\' && !at_end()) {
            advance();
        }
    }
    diagnostics_.error(SourceSpan{begin, offset_}, "unterminated string literal");
    return make_token(TokenKind::String, begin, offset_);
}

Token Lexer::make_token(TokenKind kind, std::size_t begin, std::size_t end) const {
    return Token{kind, source_.slice(SourceSpan{begin, end}), SourceSpan{begin, end}};
}

void Lexer::report_unknown(std::size_t position, char ch) {
    diagnostics_.error(SourceSpan{position, position + 1}, std::string("unexpected character '") + ch + "'");
}

} // namespace evident
