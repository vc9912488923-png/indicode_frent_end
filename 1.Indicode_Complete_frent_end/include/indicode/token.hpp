#pragma once

#include "indicode/source.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace indicode {

enum class TokenKind {
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,
    Keyword,
    Newline,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Comma,
    Colon,
    Dot,
    DotDot,
    Semicolon,
    Arrow,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    Tilde,
    Equal,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AndAnd,
    OrOr,
    Invalid,
    EndOfFile
};

enum class Keyword {
    Function,
    Record,
    Return,
    If,
    Else,
    Repeat,
    Times,
    For,
    In,
    While,
    Let,
    Var,
    True,
    False,
    Move,
    Borrow,
    BorrowMut,
    Unless
};

struct KeywordPayload {
    Keyword value;
};

struct IntegerLiteralPayload {
    std::uint8_t radix;
    std::string digits;
};

struct FloatLiteralPayload {
    std::string normalized;
};

struct StringLiteralPayload {
    std::string encoded_contents;
};

using TokenPayload = std::variant<std::monostate, KeywordPayload,
                                  IntegerLiteralPayload, FloatLiteralPayload,
                                  StringLiteralPayload>;

struct Token {
    TokenKind kind;
    std::string lexeme;
    SourceSpan span;
    TokenPayload payload{};
};

enum class TriviaKind {
    Whitespace,
    LineComment,
    BlockComment
};

struct Trivia {
    TriviaKind kind;
    std::string lexeme;
    SourceSpan span;
};

std::string token_kind_name(TokenKind kind);
std::string keyword_name(Keyword keyword);
bool keyword_from_name(const std::string& name, Keyword& keyword);
std::string trivia_kind_name(TriviaKind kind);

}