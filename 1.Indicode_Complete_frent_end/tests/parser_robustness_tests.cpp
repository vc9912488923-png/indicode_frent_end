#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <utility>

int main() {
    constexpr char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        "_(){}[],:.;+-*/%!~=<>|&#\" \t\n";
    std::mt19937 generator(0x1d1c0de);
    std::uniform_int_distribution<std::size_t> length_distribution(0, 128);
    std::uniform_int_distribution<std::size_t> character_distribution(0, sizeof(alphabet) - 2);

    for (std::size_t sample = 0; sample < 1000; ++sample) {
        std::string source_text;
        const auto length = length_distribution(generator);
        source_text.reserve(length);
        for (std::size_t index = 0; index < length; ++index) {
            source_text.push_back(alphabet[character_distribution(generator)]);
        }

        const indicode::SourceFile source{"generated.indi", std::move(source_text)};
        const auto lexed = indicode::Lexer(source).lex();
        if (lexed.tokens.empty() || lexed.tokens.back().kind != indicode::TokenKind::EndOfFile) {
            std::cerr << "FAIL: lexer did not terminate with an EOF token\n";
            return EXIT_FAILURE;
        }
        static_cast<void>(indicode::Parser(lexed.tokens).parse());
    }

    std::cout << "Parser robustness smoke test passed\n";
}
