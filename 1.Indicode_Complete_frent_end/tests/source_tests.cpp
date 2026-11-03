#include "indicode/source.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool throws_out_of_range(const auto& function) {
    try {
        function();
    } catch (const std::out_of_range&) {
        return true;
    }
    return false;
}

}

int main() {
    indicode::SourceManager sources;
    const auto first = sources.add_source("first.indi", "a\tπ\r\nnext\rfinal\n");
    const auto second = sources.add_source("second.indi", "last");

    require(sources.get_source(first).name == "first.indi" &&
                sources.get_source(second).contents == "last",
            "source manager should own multiple immutable source buffers");

    const auto after_tab = sources.location(first, 2);
    require(after_tab.line == 1 && after_tab.column == 5,
            "column mapping should expand tabs to four-column stops");

    const auto after_unicode = sources.location(first, 4);
    require(after_unicode.line == 1 && after_unicode.column == 6,
            "column mapping should count a UTF-8 scalar once");

    const auto second_line = sources.line(first, 2);
    require(second_line.text == "next" && second_line.offset == 6,
            "line table should treat CRLF as one line break");
    const auto third_line = sources.line(first, 3);
    require(third_line.text == "final" && third_line.offset == 11,
            "line table should recognize lone CR line breaks");

    const auto final_location = sources.location(first, 17);
    require(final_location.line == 4 && final_location.column == 1,
            "trailing newline should create a valid empty final line");
    require(throws_out_of_range([&] { (void)sources.get_source(99); }) &&
                throws_out_of_range([&] { (void)sources.location(first, 100); }) &&
                throws_out_of_range([&] { (void)sources.line(first, 0); }),
            "invalid source IDs, offsets, and line numbers should be rejected");

    std::cout << "Source manager tests passed\n";
}
