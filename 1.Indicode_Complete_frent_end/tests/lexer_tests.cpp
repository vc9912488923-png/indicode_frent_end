#include "indicode/lexer.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}

int main() {
    const indicode::SourceFile source{
        "example.indi",
        "function add(a: Int, b: Int) -> Int {\r\n"
        "  total = a + b # comment\r\n"
        "  say(\"hello!\")\r\n"
        "}"
    };
    const auto result = indicode::Lexer(source).lex();

    require(result.diagnostics.empty(), "valid source should have no diagnostics");
    require(result.tokens.front().kind == indicode::TokenKind::Keyword,
            "function should be recognized as a keyword");
    require(result.tokens[1].lexeme == "add", "function name should remain an identifier");
    require(result.tokens[11].kind == indicode::TokenKind::Arrow,
            "function return arrow should be recognized");

    bool saw_newline = false;
    bool saw_string = false;
    for (const auto& token : result.tokens) {
        saw_newline = saw_newline || token.kind == indicode::TokenKind::Newline;
        saw_string = saw_string ||
                     (token.kind == indicode::TokenKind::StringLiteral && token.lexeme == "\"hello!\"");
    }
    require(saw_newline, "CRLF should produce newline separator tokens");
    require(saw_string, "quoted text should be emitted as one string token");
    require(result.tokens.back().kind == indicode::TokenKind::EndOfFile,
            "token stream should end with EOF");

    const indicode::SourceFile columns{"columns.indi", "x = \"\xC3\xA9\" y\nx\t= 1\n"};
    const auto column_result = indicode::Lexer(columns).lex();
    const indicode::Token* y_token = nullptr;
    const indicode::Token* tabbed_equal_token = nullptr;
    for (const auto& token : column_result.tokens) {
        if (token.lexeme == "y") y_token = &token;
        if (token.kind == indicode::TokenKind::Equal && token.span.start.line == 2) {
            tabbed_equal_token = &token;
        }
    }
    require(column_result.diagnostics.empty() && y_token != nullptr &&
                y_token->span.start.column == 9,
            "lexer columns should count a UTF-8 scalar once, not once per encoded byte");
    require(tabbed_equal_token != nullptr && tabbed_equal_token->span.start.column == 5,
            "lexer columns should expand tabs to the configured four-column stops");

    const indicode::SourceFile malformed{"bad.indi", "value = 1e+\ntext = \"open\n"};
    const auto errors = indicode::Lexer(malformed).lex();
    require(errors.diagnostics.size() == 2,
            "malformed exponent and unterminated string should both be diagnosed");
    require(errors.diagnostics[0].code == "E-LEX-004", "exponent diagnostic code should be stable");
    require(errors.diagnostics[1].code == "E-LEX-005", "string diagnostic code should be stable");

    const indicode::SourceFile unicode{
        "unicode.indi",
        "letter = \"\xC3\xA9\"\nface = \"\\u{1F600}\"\n"
    };
    const auto unicode_result = indicode::Lexer(unicode).lex();
    require(unicode_result.diagnostics.empty(), "valid UTF-8 and Unicode escapes should be accepted");

    const indicode::SourceFile invalid_utf8{"invalid_utf8.indi", "word = \"\xC3(\"\n"};
    const auto invalid_utf8_result = indicode::Lexer(invalid_utf8).lex();
    bool saw_utf8_error = false;
    for (const auto& diagnostic : invalid_utf8_result.diagnostics) {
        saw_utf8_error = saw_utf8_error || diagnostic.code == "E-LEX-006";
    }
    require(saw_utf8_error, "malformed UTF-8 in a string should be diagnosed");

    const indicode::SourceFile invalid_comment_utf8{
        "invalid_comment_utf8.indi",
        std::string("# line ") + "\xC3" + "(\n/* block " + "\xFF" + " */\n"
    };
    const auto invalid_comment_result = indicode::Lexer(invalid_comment_utf8).lex();
    require(invalid_comment_result.diagnostics.size() == 2 &&
                invalid_comment_result.diagnostics[0].code == "E-LEX-006" &&
                invalid_comment_result.diagnostics[1].code == "E-LEX-006",
            "malformed UTF-8 in line and block comments should be diagnosed");

    const indicode::SourceFile invalid_escape{"invalid_escape.indi", "word = \"\\q\"\n"};
    const auto invalid_escape_result = indicode::Lexer(invalid_escape).lex();
    require(!invalid_escape_result.diagnostics.empty() &&
                invalid_escape_result.diagnostics[0].code == "E-LEX-007",
            "unsupported string escapes should have a stable diagnostic");

        const indicode::SourceFile numeric_literals{
            "numbers.indi",
            "binary = 0b1010\noctal = 0o17\nhex = 0x2a\ngrouped = 1_000_000\n"
        };
        const auto numeric_result = indicode::Lexer(numeric_literals).lex();
        require(numeric_result.diagnostics.empty(),
                "binary, octal, hexadecimal, and separated integer literals should lex");

        const indicode::SourceFile malformed_numbers{
            "bad_numbers.indi",
            "bad_binary = 0b102\nbad_hex = 0x\nbad_separator = 12__3\n"
        };
        const auto malformed_number_result = indicode::Lexer(malformed_numbers).lex();
        require(malformed_number_result.diagnostics.size() == 3,
                "invalid radix digits, missing radix digits, and misplaced separators should be rejected");
        require(malformed_number_result.diagnostics[0].code == "E-LEX-009",
                "malformed numeric diagnostic should have a stable code");

        const indicode::SourceFile comments{
            "comments.indi",
            "first = 1 /* outer\n/* nested */ comment */ second = 2\n"
        };
        const auto comment_result = indicode::Lexer(comments).lex();
        require(comment_result.diagnostics.empty(), "nested block comments should be accepted");
        bool preserved_comment_newline = false;
        for (const auto& token : comment_result.tokens) {
            preserved_comment_newline = preserved_comment_newline ||
                                        (token.kind == indicode::TokenKind::Newline && token.span.start.line == 1);
        }
        require(preserved_comment_newline, "block-comment newlines should remain statement separators");

        const indicode::SourceFile open_comment{"open_comment.indi", "value = 1 /* never closed"};
        const auto open_comment_result = indicode::Lexer(open_comment).lex();
        require(open_comment_result.diagnostics.size() == 1 &&
                    open_comment_result.diagnostics[0].code == "E-LEX-010",
                "unterminated block comments should produce a recoverable diagnostic");

    const indicode::SourceFile typed_tokens{
        "payloads.indi",
        "function varName = 0b10_10 + 0o17 + 0x2a + 1_000 + 1_2.3_4e+5_6 + \"hello\\n\"\n"
    };
    const auto typed_result = indicode::Lexer(typed_tokens).lex();
    require(typed_result.diagnostics.empty(), "valid typed-token fixture should lex cleanly");
    const auto function_token = std::find_if(
        typed_result.tokens.begin(), typed_result.tokens.end(),
        [](const indicode::Token& token) { return token.lexeme == "function"; });
    require(function_token != typed_result.tokens.end() &&
                std::get<indicode::KeywordPayload>(function_token->payload).value ==
                    indicode::Keyword::Function,
            "keyword tokens should expose their typed keyword identity");
    const auto find_token = [&typed_result](const std::string& spelling) {
        return std::find_if(typed_result.tokens.begin(), typed_result.tokens.end(),
                            [&spelling](const indicode::Token& token) {
                                return token.lexeme == spelling;
                            });
    };
    const auto binary_token = find_token("0b10_10");
    const auto octal_token = find_token("0o17");
    const auto hexadecimal_token = find_token("0x2a");
    const auto decimal_token = find_token("1_000");
    const auto float_token = std::find_if(
        typed_result.tokens.begin(), typed_result.tokens.end(),
        [](const indicode::Token& token) { return token.kind == indicode::TokenKind::FloatLiteral; });
    const auto string_token = std::find_if(
        typed_result.tokens.begin(), typed_result.tokens.end(),
        [](const indicode::Token& token) { return token.kind == indicode::TokenKind::StringLiteral; });
    require(binary_token != typed_result.tokens.end() &&
                binary_token->span.end.offset - binary_token->span.start.offset == binary_token->lexeme.size() &&
                std::get<indicode::IntegerLiteralPayload>(binary_token->payload).radix == 2 &&
                std::get<indicode::IntegerLiteralPayload>(binary_token->payload).digits == "1010",
            "integer payload should expose radix and separator-free digits without changing spelling or spans");
    require(octal_token != typed_result.tokens.end() &&
                std::get<indicode::IntegerLiteralPayload>(octal_token->payload).radix == 8 &&
                std::get<indicode::IntegerLiteralPayload>(octal_token->payload).digits == "17" &&
                hexadecimal_token != typed_result.tokens.end() &&
                std::get<indicode::IntegerLiteralPayload>(hexadecimal_token->payload).radix == 16 &&
                std::get<indicode::IntegerLiteralPayload>(hexadecimal_token->payload).digits == "2a" &&
                decimal_token != typed_result.tokens.end() &&
                std::get<indicode::IntegerLiteralPayload>(decimal_token->payload).radix == 10 &&
                std::get<indicode::IntegerLiteralPayload>(decimal_token->payload).digits == "1000",
            "all integer radices should expose normalized typed payloads");
    require(float_token != typed_result.tokens.end() &&
                float_token->span.end.offset - float_token->span.start.offset == float_token->lexeme.size() &&
                std::get<indicode::FloatLiteralPayload>(float_token->payload).normalized == "12.34e+56",
            "float payload should preserve exact numeric value spelling without separators");
    require(string_token != typed_result.tokens.end() &&
                string_token->span.end.offset - string_token->span.start.offset == string_token->lexeme.size() &&
                std::get<indicode::StringLiteralPayload>(string_token->payload).encoded_contents == "hello\\n",
            "string payload should retain the escape-preserving contents and exact source span");

    const indicode::SourceFile trivia_source{
        "trivia.indi",
        "  value\t= 1 # line comment\n/* outer\n /* nested */ done */value = 2\n"
    };
    const auto trivia_result = indicode::Lexer(trivia_source).lex();
    require(trivia_result.diagnostics.empty(), "valid trivia fixture should lex cleanly");
    bool saw_line_comment = false;
    bool saw_nested_block_comment = false;
    std::size_t last_trivia_offset = 0;
    for (const auto& trivia : trivia_result.trivia) {
        require(trivia.span.end.offset - trivia.span.start.offset == trivia.lexeme.size() &&
                    trivia_source.contents.substr(trivia.span.start.offset, trivia.lexeme.size()) ==
                        trivia.lexeme,
                "trivia spelling and source span should preserve exact source bytes");
        require(trivia.span.start.offset >= last_trivia_offset,
                "trivia should be retained in source order");
        last_trivia_offset = trivia.span.end.offset;
        saw_line_comment = saw_line_comment ||
                           (trivia.kind == indicode::TriviaKind::LineComment &&
                            trivia.lexeme == "# line comment");
        saw_nested_block_comment = saw_nested_block_comment ||
                                   (trivia.kind == indicode::TriviaKind::BlockComment &&
                                    trivia.lexeme == "/* outer\n /* nested */ done */");
    }
    require(saw_line_comment && saw_nested_block_comment,
            "line and nested block comments should be preserved as distinct trivia");
    require(std::count_if(trivia_result.tokens.begin(), trivia_result.tokens.end(),
                          [](const indicode::Token& token) {
                              return token.kind == indicode::TokenKind::Newline;
                          }) == 3,
            "preserving block-comment trivia must retain its newline separator tokens");

    constexpr std::array<indicode::Keyword, 14> keywords{
        indicode::Keyword::Function, indicode::Keyword::Record, indicode::Keyword::Return,
        indicode::Keyword::If, indicode::Keyword::Else, indicode::Keyword::Repeat,
        indicode::Keyword::Times, indicode::Keyword::For, indicode::Keyword::In,
        indicode::Keyword::While, indicode::Keyword::Let, indicode::Keyword::Var,
        indicode::Keyword::True, indicode::Keyword::False
    };
    std::string keyword_source;
    for (const auto keyword : keywords) {
        const auto name = indicode::keyword_name(keyword);
        indicode::Keyword round_trip{};
        require(indicode::keyword_from_name(name, round_trip) && round_trip == keyword,
                "every reserved word should round-trip through the typed keyword inventory");
        keyword_source += name + ' ';
    }
    const auto keyword_result = indicode::Lexer({"keywords.indi", keyword_source}).lex();
    require(keyword_result.diagnostics.empty() &&
                std::count_if(keyword_result.tokens.begin(), keyword_result.tokens.end(),
                              [](const indicode::Token& token) {
                                  return token.kind == indicode::TokenKind::Keyword;
                              }) == static_cast<std::ptrdiff_t>(keywords.size()),
            "lexer should classify exactly the complete v1 reserved-word inventory");

    constexpr std::array<std::pair<std::string_view, indicode::TokenKind>, 28> punctuation{{
        std::pair<std::string_view, indicode::TokenKind>{"(", indicode::TokenKind::LeftParen},
        {")", indicode::TokenKind::RightParen},
        {"{", indicode::TokenKind::LeftBrace},
        {"}", indicode::TokenKind::RightBrace},
        {"[", indicode::TokenKind::LeftBracket},
        {"]", indicode::TokenKind::RightBracket},
        {",", indicode::TokenKind::Comma},
        {":", indicode::TokenKind::Colon},
        {".", indicode::TokenKind::Dot},
        {"..", indicode::TokenKind::DotDot},
        {";", indicode::TokenKind::Semicolon},
        {"->", indicode::TokenKind::Arrow},
        {"+", indicode::TokenKind::Plus},
        {"-", indicode::TokenKind::Minus},
        {"*", indicode::TokenKind::Star},
        {"/", indicode::TokenKind::Slash},
        {"%", indicode::TokenKind::Percent},
        {"!", indicode::TokenKind::Bang},
        {"~", indicode::TokenKind::Tilde},
        {"=", indicode::TokenKind::Equal},
        {"==", indicode::TokenKind::EqualEqual},
        {"!=", indicode::TokenKind::BangEqual},
        {"<", indicode::TokenKind::Less},
        {"<=", indicode::TokenKind::LessEqual},
        {">", indicode::TokenKind::Greater},
        {">=", indicode::TokenKind::GreaterEqual},
        {"&&", indicode::TokenKind::AndAnd},
        {"||", indicode::TokenKind::OrOr}
    }};
    std::string punctuation_source;
    for (const auto& token : punctuation) {
        punctuation_source += token.first;
        punctuation_source += ' ';
    }
    const auto punctuation_result =
        indicode::Lexer({"punctuation.indi", punctuation_source}).lex();
    require(punctuation_result.diagnostics.empty() &&
                punctuation_result.tokens.size() == punctuation.size() + 1,
            "lexer should recognize the complete v1 punctuation and operator inventory");
    for (std::size_t index = 0; index < punctuation.size(); ++index) {
        require(punctuation_result.tokens[index].kind == punctuation[index].second &&
                    punctuation_result.tokens[index].lexeme == punctuation[index].first,
                "punctuation spelling should map to its documented token kind");
    }

    std::cout << "Lexer tests passed\n";
}