#include "indicode/incremental_parser.hpp"

#include "indicode/lexer.hpp"
#include "indicode/parser.hpp"

#include <algorithm>
#include <utility>

namespace indicode {
namespace {

struct TokenChunk {
    std::vector<Token> tokens;
    std::size_t start_byte = 0;
    std::size_t end_byte = 0;
};

void shift_location(SourceLocation& location, std::size_t old_start, std::size_t new_start,
                    SourceId source_id, const SourceManager& sources) {
    const auto relative = location.offset >= old_start ? location.offset - old_start : 0;
    location = sources.location(source_id, new_start + relative);
}

void shift_span(SourceSpan& span, std::size_t old_start, std::size_t new_start,
                SourceId source_id, const SourceManager& sources) {
    shift_location(span.start, old_start, new_start, source_id, sources);
    shift_location(span.end, old_start, new_start, source_id, sources);
}

void shift_expression(Expr& expression, std::size_t old_start, std::size_t new_start,
                      SourceId source_id, const SourceManager& sources) {
    shift_span(expression.span, old_start, new_start, source_id, sources);
    if (expression.left) shift_expression(*expression.left, old_start, new_start, source_id, sources);
    if (expression.right) shift_expression(*expression.right, old_start, new_start, source_id, sources);
    for (auto& argument : expression.arguments) {
        shift_expression(*argument, old_start, new_start, source_id, sources);
    }
}

void shift_statements(std::vector<std::unique_ptr<Stmt>>& statements,
                      std::size_t old_start, std::size_t new_start,
                      SourceId source_id, const SourceManager& sources) {
    for (auto& statement : statements) {
        shift_span(statement->span, old_start, new_start, source_id, sources);
        if (statement->expression) {
            shift_expression(*statement->expression, old_start, new_start, source_id, sources);
        }
        shift_statements(statement->body, old_start, new_start, source_id, sources);
        shift_statements(statement->else_body, old_start, new_start, source_id, sources);
    }
}

void shift_program(Program& program, std::size_t old_start, std::size_t new_start,
                   SourceId source_id, const SourceManager& sources) {
    for (auto& record : program.records) {
        shift_span(record.span, old_start, new_start, source_id, sources);
        for (auto& field : record.fields) {
            shift_span(field.span, old_start, new_start, source_id, sources);
        }
    }
    for (auto& function : program.functions) {
        shift_span(function.span, old_start, new_start, source_id, sources);
        for (auto& parameter : function.parameters) {
            shift_span(parameter.span, old_start, new_start, source_id, sources);
        }
        for (auto& parameter : function.generic_parameters) {
            shift_span(parameter.span, old_start, new_start, source_id, sources);
        }
        shift_statements(function.body, old_start, new_start, source_id, sources);
    }
    shift_statements(program.statements, old_start, new_start, source_id, sources);
}

std::vector<TokenChunk> split_top_level(const std::vector<Token>& tokens) {
    std::vector<TokenChunk> chunks;
    std::vector<Token> current;
    std::size_t brace_depth = 0;
    std::size_t parenthesis_depth = 0;
    std::size_t bracket_depth = 0;
    const auto finish_chunk = [&]() {
        if (current.empty()) return;
        chunks.push_back({std::move(current), 0, 0});
        auto& chunk = chunks.back();
        chunk.start_byte = chunk.tokens.front().span.start.offset;
        chunk.end_byte = chunk.tokens.back().span.end.offset;
        current.clear();
    };

    for (const auto& token : tokens) {
        if (token.kind == TokenKind::EndOfFile) break;
        if (brace_depth == 0 && (token.kind == TokenKind::Newline || token.kind == TokenKind::Semicolon)) {
            finish_chunk();
            continue;
        }

        current.push_back(token);
        if (token.kind == TokenKind::LeftBrace) ++brace_depth;
        else if (token.kind == TokenKind::RightBrace && brace_depth > 0) --brace_depth;
        else if (token.kind == TokenKind::LeftParen) ++parenthesis_depth;
        else if (token.kind == TokenKind::RightParen && parenthesis_depth > 0) --parenthesis_depth;
        else if (token.kind == TokenKind::LeftBracket) ++bracket_depth;
        else if (token.kind == TokenKind::RightBracket && bracket_depth > 0) --bracket_depth;

        if (token.kind == TokenKind::RightBrace && brace_depth == 0 &&
            parenthesis_depth == 0 && bracket_depth == 0) {
            finish_chunk();
        }
    }
    finish_chunk();
    return chunks;
}

}

IncrementalParser::IncrementalParser(SourceFile source) : source_(std::move(source)) {}

const IncrementalParseResult& IncrementalParser::parse() {
    if (!parsed_) {
        parsed_ = true;
        revision_ = 1;
    }
    return rebuild();
}

const IncrementalParseResult& IncrementalParser::apply_edit(const SourceEdit& edit) {
    if (!parsed_) parse();
    result_.diagnostics.clear();
    if (edit.start_byte > source_.contents.size() ||
        edit.erased_bytes > source_.contents.size() - edit.start_byte) {
        const SourceLocation location{edit.start_byte, 1, 1};
        result_.diagnostics.push_back({DiagnosticSeverity::Error, "E-INCR-001",
                                       "source edit range is outside the current buffer",
                                       {location, location}});
        return result_;
    }
    source_.contents.replace(edit.start_byte, edit.erased_bytes, edit.inserted_text);
    ++revision_;
    return rebuild();
}

const IncrementalParseResult& IncrementalParser::rebuild() {
    auto old_items = std::move(result_.items);
    result_ = {};
    result_.revision = revision_;

    const auto lexed = Lexer(source_).lex();
    result_.diagnostics = lexed.diagnostics;
    const auto chunks = split_top_level(lexed.tokens);
    SourceManager sources;
    const auto source_id = sources.add_source(source_.name, source_.contents);
    std::vector<bool> reused(old_items.size(), false);

    for (const auto& chunk : chunks) {
        const auto text = source_.contents.substr(chunk.start_byte,
                                                  chunk.end_byte - chunk.start_byte);
        std::size_t previous_index = old_items.size();
        for (std::size_t index = 0; index < old_items.size(); ++index) {
            if (!reused[index] && old_items[index].diagnostics.empty() &&
                old_items[index].source_text == text) {
                previous_index = index;
                break;
            }
        }

        IncrementalParseItem item;
        if (previous_index < old_items.size()) {
            reused[previous_index] = true;
            item = std::move(old_items[previous_index]);
            shift_program(item.program, item.start_byte, chunk.start_byte, source_id, sources);
            item.reused = true;
            ++result_.reused_item_count;
        } else {
            auto tokens = chunk.tokens;
            const auto eof_location = sources.location(source_id, chunk.end_byte);
            tokens.push_back({TokenKind::EndOfFile, "", {eof_location, eof_location}});
            auto parsed = Parser(tokens).parse();
            item.program = std::move(parsed.program);
            item.diagnostics = std::move(parsed.diagnostics);
            item.reused = false;
            ++result_.parsed_item_count;
            result_.diagnostics.insert(result_.diagnostics.end(), item.diagnostics.begin(),
                                       item.diagnostics.end());
        }
        item.source_text = text;
        item.start_byte = chunk.start_byte;
        result_.items.push_back(std::move(item));
    }
    return result_;
}

}