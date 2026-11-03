#pragma once

#include "indicode/diagnostic.hpp"
#include "indicode/source.hpp"
#include "indicode/token.hpp"

#include <vector>

namespace indicode {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Trivia> trivia;
    std::vector<Diagnostic> diagnostics;
};

class Lexer {
public:
    explicit Lexer(const SourceFile& source);

    LexResult lex();

private:
    const SourceFile& source_;
    LexResult result_;
    std::size_t offset_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;

    bool at_end() const;
    char current() const;
    char peek(std::size_t distance = 1) const;
    char advance();
    bool match(char expected);
    SourceLocation location() const;
    void add_token(TokenKind kind, SourceLocation start);
    void add_token(TokenKind kind, SourceLocation start, TokenPayload payload);
    void add_trivia(TriviaKind kind, SourceLocation start);
    void add_error(std::string code, std::string message, SourceLocation start);
    void scan_token();
    void scan_identifier(SourceLocation start);
    void scan_number(SourceLocation start);
    void scan_string(SourceLocation start);
    void scan_comment_character();
    void scan_block_comment(SourceLocation start);
};

}