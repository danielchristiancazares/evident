#pragma once

#include "evident/Diagnostic.hpp"
#include "evident/Source.hpp"
#include "evident/Token.hpp"

#include <vector>

namespace evident {

enum class SourceCursorState {
    HasMoreSource,
    ReachedEnd,
};

enum class CharacterMatchState {
    ExpectedCharacterAbsent,
    ConsumedExpectedCharacter,
};

class Lexer {
public:
    Lexer(const SourceFile& source, DiagnosticSink& diagnostics);
    [[nodiscard]] std::vector<Token> lex();

private:
    [[nodiscard]] SourceCursorState source_cursor_state() const noexcept;
    char peek(std::size_t lookahead = 0) const noexcept;
    char advance() noexcept;
    [[nodiscard]] CharacterMatchState match_next(char expected) noexcept;

    void skip_whitespace_and_comments();
    Token lex_identifier_or_keyword();
    Token lex_number();
    Token lex_string();
    Token make_token(TokenKind kind, std::size_t begin, std::size_t end) const;
    void report_unknown(std::size_t position, char ch);

    const SourceFile& source_;
    DiagnosticSink& diagnostics_;
    std::size_t offset_ = 0;
};

} // namespace evident
