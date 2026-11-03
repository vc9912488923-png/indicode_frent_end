#include "indicode/lexer.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace indicode {
namespace {

bool is_digit(char value) {
    return value >= '0' && value <= '9';
}

int digit_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool is_alpha(char value) {
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') || value == '_';
}

bool is_alphanumeric(char value) {
    return is_alpha(value) || is_digit(value);
}

int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::size_t utf8_sequence_length(const std::string& text, std::size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    std::size_t length = 0;
    if (first >= 0xc2 && first <= 0xdf) length = 2;
    else if (first >= 0xe0 && first <= 0xef) length = 3;
    else if (first >= 0xf0 && first <= 0xf4) length = 4;
    else return 0;
    if (offset + length > text.size()) return 0;

    const auto second = static_cast<unsigned char>(text[offset + 1]);
    if ((second & 0xc0) != 0x80) return 0;
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second > 0x9f) ||
        (first == 0xf0 && second < 0x90) || (first == 0xf4 && second > 0x8f)) return 0;
    for (std::size_t index = 2; index < length; ++index) {
        if ((static_cast<unsigned char>(text[offset + index]) & 0xc0) != 0x80) return 0;
    }
    return length;
}

}

std::string token_kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier: return "Identifier";
    case TokenKind::IntegerLiteral: return "IntegerLiteral";
    case TokenKind::FloatLiteral: return "FloatLiteral";
    case TokenKind::StringLiteral: return "StringLiteral";
    case TokenKind::Keyword: return "Keyword";
    case TokenKind::Newline: return "Newline";
    case TokenKind::LeftParen: return "LeftParen";
    case TokenKind::RightParen: return "RightParen";
    case TokenKind::LeftBrace: return "LeftBrace";
    case TokenKind::RightBrace: return "RightBrace";
    case TokenKind::LeftBracket: return "LeftBracket";
    case TokenKind::RightBracket: return "RightBracket";
    case TokenKind::Comma: return "Comma";
    case TokenKind::Colon: return "Colon";
    case TokenKind::Dot: return "Dot";
    case TokenKind::DotDot: return "DotDot";
    case TokenKind::Semicolon: return "Semicolon";
    case TokenKind::Arrow: return "Arrow";
    case TokenKind::Plus: return "Plus";
    case TokenKind::Minus: return "Minus";
    case TokenKind::Star: return "Star";
    case TokenKind::Slash: return "Slash";
    case TokenKind::Percent: return "Percent";
    case TokenKind::Bang: return "Bang";
    case TokenKind::Tilde: return "Tilde";
    case TokenKind::Equal: return "Equal";
    case TokenKind::EqualEqual: return "EqualEqual";
    case TokenKind::BangEqual: return "BangEqual";
    case TokenKind::Less: return "Less";
    case TokenKind::LessEqual: return "LessEqual";
    case TokenKind::Greater: return "Greater";
    case TokenKind::GreaterEqual: return "GreaterEqual";
    case TokenKind::AndAnd: return "AndAnd";
    case TokenKind::OrOr: return "OrOr";
    case TokenKind::Invalid: return "Invalid";
    case TokenKind::EndOfFile: return "EndOfFile";
    }
    return "Unknown";
}

std::string keyword_name(Keyword keyword) {
    switch (keyword) {
    case Keyword::Function: return "function";
    case Keyword::Record: return "record";
    case Keyword::Return: return "return";
    case Keyword::If: return "if";
    case Keyword::Else: return "else";
    case Keyword::Repeat: return "repeat";
    case Keyword::Times: return "times";
    case Keyword::For: return "for";
    case Keyword::In: return "in";
    case Keyword::While: return "while";
    case Keyword::Let: return "let";
    case Keyword::Var: return "var";
    case Keyword::True: return "true";
    case Keyword::False: return "false";
    }
    return "invalid";
}

bool keyword_from_name(const std::string& name, Keyword& keyword) {
    static constexpr std::array keywords{
        Keyword::Function, Keyword::Record, Keyword::Return, Keyword::If,
        Keyword::Else, Keyword::Repeat, Keyword::Times, Keyword::For,
        Keyword::In, Keyword::While, Keyword::Let, Keyword::Var,
        Keyword::True, Keyword::False
    };
    for (const auto candidate : keywords) {
        if (name == keyword_name(candidate)) {
            keyword = candidate;
            return true;
        }
    }
    return false;
}

std::string trivia_kind_name(TriviaKind kind) {
    switch (kind) {
    case TriviaKind::Whitespace: return "Whitespace";
    case TriviaKind::LineComment: return "LineComment";
    case TriviaKind::BlockComment: return "BlockComment";
    }
    return "Unknown";
}

Lexer::Lexer(const SourceFile& source) : source_(source) {}

LexResult Lexer::lex() {
    while (!at_end()) {
        scan_token();
    }

    const auto eof_location = location();
    result_.tokens.push_back({TokenKind::EndOfFile, "", {eof_location, eof_location}});
    return std::move(result_);
}

bool Lexer::at_end() const {
    return offset_ >= source_.contents.size();
}

char Lexer::current() const {
    return at_end() ? '\0' : source_.contents[offset_];
}

char Lexer::peek(std::size_t distance) const {
    const auto next = offset_ + distance;
    return next >= source_.contents.size() ? '\0' : source_.contents[next];
}

char Lexer::advance() {
    const char value = current();
    ++offset_;
    const auto byte = static_cast<unsigned char>(value);
    if (value == '\t') {
        column_ += 4 - ((column_ - 1) % 4);
    } else if ((byte & 0xc0) != 0x80) {
        ++column_;
    }
    return value;
}

bool Lexer::match(char expected) {
    if (at_end() || current() != expected) {
        return false;
    }
    advance();
    return true;
}

SourceLocation Lexer::location() const {
    return {offset_, line_, column_};
}

void Lexer::add_token(TokenKind kind, SourceLocation start) {
    add_token(kind, start, {});
}

void Lexer::add_token(TokenKind kind, SourceLocation start, TokenPayload payload) {
    const auto end = location();
    result_.tokens.push_back({
        kind,
        source_.contents.substr(start.offset, end.offset - start.offset),
        {start, end},
        std::move(payload)
    });
}

void Lexer::add_trivia(TriviaKind kind, SourceLocation start) {
    const auto end = location();
    result_.trivia.push_back({
        kind,
        source_.contents.substr(start.offset, end.offset - start.offset),
        {start, end}
    });
}

void Lexer::add_error(std::string code, std::string message, SourceLocation start) {
    const auto end = location();
    result_.diagnostics.push_back({DiagnosticSeverity::Error, std::move(code),
                                   std::move(message), {start, end}});
}

void Lexer::scan_token() {
    const auto start = location();
    const char value = advance();

    switch (value) {
    case ' ':
    case '\t':
    case '\f':
        add_trivia(TriviaKind::Whitespace, start);
        return;
    case '\r':
        if (current() == '\n') {
            ++offset_;
        }
        ++line_;
        column_ = 1;
        result_.tokens.push_back({TokenKind::Newline, "\n", {start, location()}});
        return;
    case '\n':
        ++line_;
        column_ = 1;
        result_.tokens.push_back({TokenKind::Newline, "\n", {start, location()}});
        return;
    case '#':
        while (!at_end() && current() != '\n' && current() != '\r') {
            scan_comment_character();
        }
        add_trivia(TriviaKind::LineComment, start);
        return;
    case '(': add_token(TokenKind::LeftParen, start); return;
    case ')': add_token(TokenKind::RightParen, start); return;
    case '{': add_token(TokenKind::LeftBrace, start); return;
    case '}': add_token(TokenKind::RightBrace, start); return;
    case '[': add_token(TokenKind::LeftBracket, start); return;
    case ']': add_token(TokenKind::RightBracket, start); return;
    case ',': add_token(TokenKind::Comma, start); return;
    case ':': add_token(TokenKind::Colon, start); return;
    case '.': add_token(match('.') ? TokenKind::DotDot : TokenKind::Dot, start); return;
    case ';': add_token(TokenKind::Semicolon, start); return;
    case '+': add_token(TokenKind::Plus, start); return;
    case '*': add_token(TokenKind::Star, start); return;
    case '/':
        if (match('*')) {
            scan_block_comment(start);
            add_trivia(TriviaKind::BlockComment, start);
        } else {
            add_token(TokenKind::Slash, start);
        }
        return;
    case '%': add_token(TokenKind::Percent, start); return;
    case '~': add_token(TokenKind::Tilde, start); return;
    case '-': add_token(match('>') ? TokenKind::Arrow : TokenKind::Minus, start); return;
    case '!': add_token(match('=') ? TokenKind::BangEqual : TokenKind::Bang, start); return;
    case '=': add_token(match('=') ? TokenKind::EqualEqual : TokenKind::Equal, start); return;
    case '<': add_token(match('=') ? TokenKind::LessEqual : TokenKind::Less, start); return;
    case '>': add_token(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater, start); return;
    case '&':
        if (match('&')) {
            add_token(TokenKind::AndAnd, start);
        } else {
            add_error("E-LEX-001", "expected '&' after '&'", start);
            add_token(TokenKind::Invalid, start);
        }
        return;
    case '|':
        if (match('|')) {
            add_token(TokenKind::OrOr, start);
        } else {
            add_error("E-LEX-002", "expected '|' after '|'", start);
            add_token(TokenKind::Invalid, start);
        }
        return;
    case '"': scan_string(start); return;
    default:
        if (is_alpha(value)) {
            scan_identifier(start);
        } else if (is_digit(value)) {
            scan_number(start);
        } else if (static_cast<unsigned char>(value) >= 0x80) {
            const auto length = utf8_sequence_length(source_.contents, start.offset);
            if (length == 0) {
                add_error("E-LEX-006", "invalid UTF-8 sequence", start);
                add_token(TokenKind::Invalid, start);
            } else {
                for (std::size_t index = 1; index < length; ++index) advance();
                add_error("E-LEX-008", "non-ASCII identifiers are not supported yet", start);
                add_token(TokenKind::Invalid, start);
            }
        } else {
            add_error("E-LEX-003", "unrecognized character", start);
            add_token(TokenKind::Invalid, start);
        }
        return;
    }
}

void Lexer::scan_identifier(SourceLocation start) {
    while (is_alphanumeric(current())) {
        advance();
    }
    const auto lexeme = source_.contents.substr(start.offset, offset_ - start.offset);
    Keyword keyword{};
    if (keyword_from_name(lexeme, keyword)) {
        add_token(TokenKind::Keyword, start, KeywordPayload{keyword});
    } else {
        add_token(TokenKind::Identifier, start);
    }
}

void Lexer::scan_number(SourceLocation start) {
    unsigned base = 10;
    if (source_.contents[start.offset] == '0') {
        if (current() == 'b' || current() == 'B') base = 2;
        else if (current() == 'o' || current() == 'O') base = 8;
        else if (current() == 'x' || current() == 'X') base = 16;
        if (base != 10) advance();
    }

    bool malformed = false;
    auto scan_digits = [&](unsigned radix) {
        bool saw_digit = base == 10;
        bool previous_was_digit = base == 10;
        while (true) {
            const int digit = digit_value(current());
            if (digit >= 0 && static_cast<unsigned>(digit) < radix) {
                advance();
                saw_digit = true;
                previous_was_digit = true;
            } else if (current() == '_') {
                const int next_digit = digit_value(peek());
                if (!previous_was_digit || next_digit < 0 || static_cast<unsigned>(next_digit) >= radix) {
                    malformed = true;
                }
                advance();
                previous_was_digit = false;
            } else {
                break;
            }
        }
        if (!saw_digit) malformed = true;
    };

    scan_digits(base);
    bool is_float = false;
    if (base == 10 && current() == '.' && is_digit(peek())) {
        is_float = true;
        advance();
        scan_digits(10);
    }

    if (base == 10 && (current() == 'e' || current() == 'E')) {
        is_float = true;
        advance();
        if (current() == '+' || current() == '-') {
            advance();
        }
        if (!is_digit(current())) {
            add_error("E-LEX-004", "expected a digit in numeric exponent", start);
            add_token(TokenKind::Invalid, start);
            return;
        }
        scan_digits(10);
    }

    if (base != 10 && (is_alphanumeric(current()) || current() == '_')) {
        malformed = true;
        while (is_alphanumeric(current()) || current() == '_') advance();
    } else if (is_alpha(current())) {
        malformed = true;
        while (is_alphanumeric(current())) advance();
    }

    if (malformed) {
        add_error("E-LEX-009", "malformed integer literal or unsupported numeric suffix", start);
        add_token(TokenKind::Invalid, start);
        return;
    }

    const auto spelling = source_.contents.substr(start.offset, offset_ - start.offset);
    std::string normalized;
    normalized.reserve(spelling.size());
    for (const char character : spelling) {
        if (character != '_') normalized.push_back(character);
    }
    if (is_float) {
        add_token(TokenKind::FloatLiteral, start, FloatLiteralPayload{std::move(normalized)});
    } else {
        const std::size_t prefix_length = base == 10 ? 0 : 2;
        add_token(TokenKind::IntegerLiteral, start,
                  IntegerLiteralPayload{static_cast<std::uint8_t>(base),
                                        normalized.substr(prefix_length)});
    }
}

void Lexer::scan_string(SourceLocation start) {
    bool terminated = false;
    bool valid = true;
    while (!at_end()) {
        if (current() == '"') {
            advance();
            terminated = true;
            break;
        }
        if (current() == '\n' || current() == '\r') {
            break;
        }
        if (current() == '\\') {
            const auto escape_start = location();
            advance();
            if (at_end()) {
                valid = false;
                add_error("E-LEX-007", "incomplete string escape", escape_start);
                break;
            }
            const char escape = advance();
            if (escape == 'u') {
                std::uint32_t scalar = 0;
                bool has_digits = false;
                bool escape_valid = current() == '{';
                if (escape_valid) {
                    advance();
                    while (!at_end() && current() != '}') {
                        const int digit = hex_value(current());
                        if (digit < 0 || scalar > 0x10ffff / 16) {
                            escape_valid = false;
                        } else {
                            scalar = scalar * 16 + static_cast<std::uint32_t>(digit);
                            if (scalar > 0x10ffff) escape_valid = false;
                        }
                        has_digits = true;
                        advance();
                    }
                    if (current() == '}') advance();
                    else escape_valid = false;
                }
                if (!has_digits || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
                    escape_valid = false;
                }
                if (!escape_valid) {
                    valid = false;
                    add_error("E-LEX-007", "invalid Unicode escape; expected a Unicode scalar in \\u{...}",
                              escape_start);
                }
            } else if (escape != '"' && escape != '\\' && escape != 'n' && escape != 'r' &&
                       escape != 't' && escape != '0') {
                valid = false;
                add_error("E-LEX-007", "unsupported string escape", escape_start);
            }
        } else if (static_cast<unsigned char>(current()) >= 0x80) {
            const auto utf8_length = utf8_sequence_length(source_.contents, offset_);
            if (utf8_length == 0) {
                const auto invalid_start = location();
                advance();
                valid = false;
                add_error("E-LEX-006", "invalid UTF-8 sequence in string literal", invalid_start);
            } else {
                for (std::size_t index = 0; index < utf8_length; ++index) advance();
            }
        } else {
            advance();
        }
    }

    if (!terminated) {
        add_error("E-LEX-005", "unterminated string literal", start);
        add_token(TokenKind::Invalid, start);
        return;
    }
    if (valid) {
        const auto end = location();
        const auto contents_start = start.offset + 1;
        const auto contents_end = end.offset - 1;
        add_token(TokenKind::StringLiteral, start,
                  StringLiteralPayload{source_.contents.substr(contents_start,
                                                               contents_end - contents_start)});
    } else {
        add_token(TokenKind::Invalid, start);
    }
}

void Lexer::scan_comment_character() {
    if (static_cast<unsigned char>(current()) < 0x80) {
        advance();
        return;
    }

    const auto start = location();
    const auto length = utf8_sequence_length(source_.contents, offset_);
    if (length == 0) {
        advance();
        add_error("E-LEX-006", "invalid UTF-8 sequence in comment", start);
        return;
    }
    for (std::size_t index = 0; index < length; ++index) {
        advance();
    }
}

void Lexer::scan_block_comment(SourceLocation start) {
    std::size_t depth = 1;
    while (!at_end() && depth > 0) {
        if (current() == '/' && peek() == '*') {
            advance();
            advance();
            ++depth;
        } else if (current() == '*' && peek() == '/') {
            advance();
            advance();
            --depth;
        } else if (current() == '\r' || current() == '\n') {
            const auto newline_start = location();
            const char newline = advance();
            if (newline == '\r' && current() == '\n') {
                advance();
            }
            ++line_;
            column_ = 1;
            result_.tokens.push_back({TokenKind::Newline, "\n", {newline_start, location()}});
        } else {
            scan_comment_character();
        }
    }
    if (depth != 0) {
        add_error("E-LEX-010", "unterminated block comment", start);
        add_token(TokenKind::Invalid, start);
    }
}

}