#include "indicode/parser.hpp"

#include <algorithm>
#include <utility>

namespace indicode {

Parser::Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

ParseResult Parser::parse() {
    skip_separators();
    while (!at_end()) {
        const auto start = current_;
        declaration();
        if (current_ == start) {
            advance();
        }
        skip_separators();
    }
    return std::move(result_);
}

const Token& Parser::peek() const {
    if (tokens_.empty()) {
        static const Token eof{TokenKind::EndOfFile, "", {{0, 1, 1}, {0, 1, 1}}};
        return eof;
    }
    return tokens_[std::min(current_, tokens_.size() - 1)];
}

const Token& Parser::previous() const {
    if (tokens_.empty() || current_ == 0) {
        return peek();
    }
    return tokens_[current_ - 1];
}

bool Parser::at_end() const {
    return check(TokenKind::EndOfFile);
}

bool Parser::check(TokenKind kind) const {
    return peek().kind == kind;
}

bool Parser::check_next(TokenKind kind) const {
    return current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == kind;
}

bool Parser::generic_type_declaration_ahead() const {
    if (!check(TokenKind::Less)) {
        return false;
    }
    std::size_t depth = 0;
    for (std::size_t index = current_; index < tokens_.size(); ++index) {
        if (tokens_[index].kind == TokenKind::Less) {
            ++depth;
        } else if (tokens_[index].kind == TokenKind::Greater) {
            if (--depth == 0) {
                return index + 1 < tokens_.size() && tokens_[index + 1].kind == TokenKind::Identifier;
            }
        } else if (tokens_[index].kind == TokenKind::EndOfFile ||
                   tokens_[index].kind == TokenKind::Newline ||
                   tokens_[index].kind == TokenKind::LeftBrace) {
            return false;
        }
    }
    return false;
}

bool Parser::check_keyword(const char* keyword) const {
    return check(TokenKind::Keyword) && peek().lexeme == keyword;
}

const Token& Parser::advance() {
    if (!at_end()) {
        ++current_;
    }
    return previous();
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

bool Parser::match_keyword(const char* keyword) {
    if (!check_keyword(keyword)) {
        return false;
    }
    advance();
    return true;
}

const Token* Parser::consume(TokenKind kind, const char* message) {
    if (check(kind)) {
        return &advance();
    }
    error(peek(), message);
    return nullptr;
}

const Token* Parser::consume_identifier(const char* message) {
    return consume(TokenKind::Identifier, message);
}

void Parser::error(const Token& token, const char* message,
                   std::optional<DiagnosticFixIt> fix_it) {
    result_.diagnostics.push_back(
        {DiagnosticSeverity::Error, "E-PARSE-001", message, token.span, std::move(fix_it)});
}

void Parser::skip_separators() {
    while (match(TokenKind::Newline) || match(TokenKind::Semicolon)) {
    }
}

void Parser::synchronize() {
    std::size_t parenthesis_depth = 0;
    std::size_t bracket_depth = 0;
    while (!at_end() && !check(TokenKind::RightBrace)) {
        if (parenthesis_depth == 0 && bracket_depth == 0) {
            if (previous().kind == TokenKind::Newline ||
                previous().kind == TokenKind::Semicolon) {
                return;
            }
            if (check_keyword("function") || check_keyword("record") ||
                check_keyword("if") || check_keyword("return") ||
                check_keyword("let") || check_keyword("var") ||
                check_keyword("while") || check_keyword("repeat") ||
                check_keyword("for")) {
                return;
            }
        }
        if (match(TokenKind::LeftParen)) {
            ++parenthesis_depth;
        } else if (match(TokenKind::RightParen)) {
            if (parenthesis_depth > 0) --parenthesis_depth;
        } else if (match(TokenKind::LeftBracket)) {
            ++bracket_depth;
        } else if (match(TokenKind::RightBracket)) {
            if (bracket_depth > 0) --bracket_depth;
        } else {
            advance();
        }
    }
}

void Parser::synchronize_declaration() {
    std::size_t brace_depth = 0;
    bool saw_body = false;
    while (!at_end()) {
        if (brace_depth == 0 && (check_keyword("function") || check_keyword("record"))) {
            return;
        }
        if (check(TokenKind::Newline) || check(TokenKind::Semicolon)) {
            if (!saw_body) return;
            advance();
            continue;
        }
        if (match(TokenKind::LeftBrace)) {
            saw_body = true;
            ++brace_depth;
            continue;
        }
        if (match(TokenKind::RightBrace)) {
            if (brace_depth == 0 || --brace_depth == 0) return;
            continue;
        }
        advance();
    }
}

void Parser::synchronize_expression(TokenKind closing) {
    std::size_t parenthesis_depth = 0;
    std::size_t bracket_depth = 0;
    while (!at_end()) {
        if (parenthesis_depth == 0 && bracket_depth == 0 &&
            (check(closing) || check(TokenKind::Comma) || check(TokenKind::Newline) ||
             check(TokenKind::Semicolon) || check(TokenKind::RightBrace) ||
             check(TokenKind::RightParen) || check(TokenKind::RightBracket))) {
            return;
        }
        if (match(TokenKind::LeftParen)) {
            ++parenthesis_depth;
        } else if (match(TokenKind::RightParen)) {
            if (parenthesis_depth > 0) --parenthesis_depth;
        } else if (match(TokenKind::LeftBracket)) {
            ++bracket_depth;
        } else if (match(TokenKind::RightBracket)) {
            if (bracket_depth > 0) --bracket_depth;
        } else {
            advance();
        }
    }
}

void Parser::declaration() {
    if (match_keyword("record")) {
        if (!record_declaration()) {
            synchronize_declaration();
        }
        return;
    }
    if (match_keyword("function")) {
        if (!function_declaration()) {
            synchronize_declaration();
        }
        return;
    }

    auto parsed = statement();
    if (parsed) {
        result_.program.statements.push_back(std::move(parsed));
    } else {
        synchronize();
    }
}

std::string Parser::parse_type() {
    const auto* name = consume_identifier("expected a type name");
    if (name == nullptr) {
        return {};
    }
    std::string type = name->lexeme;
    if (type == "Tensor") {
        if (consume(TokenKind::Less, "expected '<' after Tensor") == nullptr) {
            return {};
        }
        const auto element_type = parse_type();
        if (element_type.empty() || consume(TokenKind::Comma, "expected ',' after tensor element type") == nullptr ||
            consume(TokenKind::LeftBracket, "expected '[' before tensor dimensions") == nullptr) {
            return {};
        }
        std::string dimensions;
        if (check(TokenKind::Identifier) || check(TokenKind::IntegerLiteral)) {
            do {
                if (!check(TokenKind::Identifier) && !check(TokenKind::IntegerLiteral)) {
                    error(peek(), "expected a tensor dimension name or integer");
                    return {};
                }
                if (!dimensions.empty()) dimensions += ',';
                dimensions += advance().lexeme;
            } while (match(TokenKind::Comma));
        }
        if (dimensions.empty()) {
            error(peek(), "tensor shape must contain at least one dimension");
            return {};
        }
        if (consume(TokenKind::RightBracket, "expected ']' after tensor dimensions") == nullptr ||
            consume(TokenKind::Greater, "expected '>' after tensor type") == nullptr) {
            return {};
        }
        type = "Tensor<" + element_type + ",[" + dimensions + "]>";
        return type;
    }
    if (match(TokenKind::Less)) {
        const auto argument_type = parse_type();
        if (argument_type.empty() || consume(TokenKind::Greater, "expected '>' after type argument") == nullptr) {
            return {};
        }
        type += '<' + argument_type + '>';
    }
    return type;
}

bool Parser::record_declaration() {
    const auto& record_keyword = previous();
    const auto* name = consume_identifier("expected record name");
    if (name == nullptr || consume(TokenKind::LeftBrace, "expected '{' after record name") == nullptr) {
        return false;
    }

    RecordDecl record;
    record.name = name->lexeme;
    record.span.start = record_keyword.span.start;
    skip_separators();
    while (!check(TokenKind::RightBrace) && !at_end()) {
        const auto* field_name = consume_identifier("expected record field name");
        if (field_name == nullptr || consume(TokenKind::Colon, "expected ':' after field name") == nullptr) {
            return false;
        }
        const auto field_type = parse_type();
        if (field_type.empty()) {
            return false;
        }
        record.fields.push_back({field_name->lexeme, field_type,
                                 {field_name->span.start, previous().span.end}});

        bool separated = false;
        while (match(TokenKind::Newline) || match(TokenKind::Semicolon)) {
            separated = true;
        }
        if (match(TokenKind::Comma)) {
            skip_separators();
        } else if (!check(TokenKind::RightBrace) && !separated) {
            error(peek(), "expected ',' or a newline between record fields");
            return false;
        }
    }
    const auto* closing = consume(TokenKind::RightBrace, "expected '}' after record fields");
    if (closing == nullptr) {
        return false;
    }
    record.span.end = closing->span.end;
    result_.program.records.push_back(std::move(record));
    return true;
}

bool Parser::function_declaration() {
    const auto& function_keyword = previous();
    const auto* name = consume_identifier("expected function name");
    if (name == nullptr) {
        return false;
    }

    FunctionDecl function;
    function.name = name->lexeme;
    function.span.start = function_keyword.span.start;

    if (match(TokenKind::Less)) {
        do {
            const auto* parameter_name = consume_identifier("expected generic parameter name");
            if (parameter_name == nullptr) return false;
            std::string constraint;
            if (match(TokenKind::Colon)) {
                const auto* constraint_name = consume_identifier("expected generic constraint name");
                if (constraint_name == nullptr) return false;
                constraint = constraint_name->lexeme;
            }
            function.generic_parameters.push_back(
                {parameter_name->lexeme, constraint,
                 {parameter_name->span.start, previous().span.end}});
        } while (match(TokenKind::Comma));
        if (consume(TokenKind::Greater, "expected '>' after generic parameters") == nullptr) {
            return false;
        }
    }
    if (consume(TokenKind::LeftParen, "expected '(' after function name or generic parameters") == nullptr) {
        return false;
    }

    if (!check(TokenKind::RightParen)) {
        do {
            const auto* parameter_name = consume_identifier("expected parameter name");
            if (parameter_name == nullptr) {
                return false;
            }
            if (!match(TokenKind::Colon)) {
                const SourceSpan insertion{parameter_name->span.end, parameter_name->span.end};
                error(peek(), "expected ':' after parameter name",
                      DiagnosticFixIt{insertion, ": ", "insert ':' after the parameter name"});
                return false;
            }
            OwnershipMode ownership_mode = OwnershipMode::Value;
            if (match_keyword("move")) ownership_mode = OwnershipMode::Move;
            else if (match_keyword("borrow")) ownership_mode = OwnershipMode::Borrow;
            else if (match_keyword("borrow_mut")) ownership_mode = OwnershipMode::BorrowMut;
            const auto parameter_type = parse_type();
            if (parameter_type.empty()) {
                return false;
            }
            Parameter parameter{parameter_name->lexeme, parameter_type,
                                {parameter_name->span.start, previous().span.end}};
            parameter.ownership_mode = ownership_mode;
            function.parameters.push_back(std::move(parameter));
        } while (match(TokenKind::Comma));
    }

    if (consume(TokenKind::RightParen, "expected ')' after parameters") == nullptr) {
        return false;
    }
    if (match(TokenKind::Arrow)) {
        function.return_type = parse_type();
        if (function.return_type.empty()) {
            return false;
        }
    }
    if (check(TokenKind::Identifier) && peek().lexeme == "effects") {
        advance();
        function.has_effect_annotation = true;
        if (consume(TokenKind::LeftParen, "expected '(' after effects") == nullptr) return false;
        if (!check(TokenKind::RightParen)) {
            do {
                const auto* effect_name = consume_identifier("expected effect name");
                if (effect_name == nullptr) return false;
                function.declared_effects.push_back(effect_name->lexeme);
            } while (match(TokenKind::Comma));
        }
        if (consume(TokenKind::RightParen, "expected ')' after effects") == nullptr) return false;
    }
    if (consume(TokenKind::LeftBrace, "expected '{' before function body") == nullptr) {
        return false;
    }
    if (!block(function.body)) {
        return false;
    }
    function.span.end = previous().span.end;
    result_.program.functions.push_back(std::move(function));
    return true;
}

std::unique_ptr<Stmt> Parser::statement() {
    if (match_keyword("let")) {
        return variable_statement(false);
    }
    if (match_keyword("var")) {
        return variable_statement(true);
    }
    if (match_keyword("return")) {
        return return_statement(previous());
    }
    if (match_keyword("if")) {
        return if_statement(previous());
    }
    if (match_keyword("while")) {
        return while_statement(previous());
    }
    if (match_keyword("repeat")) {
        return repeat_statement(previous());
    }
    if (match_keyword("for")) {
        return for_each_statement(previous());
    }
    if (match(TokenKind::Identifier)) {
        const auto identifier = previous();
        if (check(TokenKind::Identifier) || generic_type_declaration_ahead()) {
            --current_;
            const auto type = parse_type();
            if (type.empty()) {
                return nullptr;
            }
            return variable_statement(true, type);
        }
        if (match(TokenKind::Equal)) {
            auto result = std::make_unique<Stmt>();
            result->kind = StmtKind::Assignment;
            result->name = identifier.lexeme;
            result->span.start = identifier.span.start;
            result->expression = expression();
            if (!result->expression) {
                return nullptr;
            }
            result->span.end = result->expression->span.end;
            return result;
        }
        --current_;
    }

    auto result = std::make_unique<Stmt>();
    result->kind = StmtKind::Expression;
    result->expression = expression();
    if (!result->expression) {
        return nullptr;
    }
    result->span = result->expression->span;
    return result;
}

std::unique_ptr<Stmt> Parser::variable_statement(bool mutable_binding, std::string declared_type) {
    const auto* name = consume_identifier("expected variable name");
    if (name == nullptr) {
        return nullptr;
    }

    if (match(TokenKind::Colon)) {
        declared_type = parse_type();
        if (declared_type.empty()) {
            return nullptr;
        }
    }

    if (consume(TokenKind::Equal, "expected '=' before variable initializer") == nullptr) {
        return nullptr;
    }
    auto initializer = expression();
    if (!initializer) {
        return nullptr;
    }

    auto result = std::make_unique<Stmt>();
    result->kind = StmtKind::Variable;
    result->name = name->lexeme;
    result->declared_type = std::move(declared_type);
    result->mutable_binding = mutable_binding;
    result->span = {name->span.start, initializer->span.end};
    result->expression = std::move(initializer);
    return result;
}

std::unique_ptr<Stmt> Parser::return_statement(const Token& keyword) {
    auto result = std::make_unique<Stmt>();
    result->kind = StmtKind::Return;
    result->span.start = keyword.span.start;
    if (!check(TokenKind::Newline) && !check(TokenKind::Semicolon) &&
        !check(TokenKind::RightBrace) && !at_end()) {
        result->expression = expression();
        if (!result->expression) {
            return nullptr;
        }
        result->span.end = result->expression->span.end;
    } else {
        result->span.end = keyword.span.end;
    }
    return result;
}

std::unique_ptr<Stmt> Parser::if_statement(const Token& keyword) {
    auto result = std::make_unique<Stmt>();
    result->kind = StmtKind::If;
    result->span.start = keyword.span.start;
    result->expression = expression();
    if (!result->expression || consume(TokenKind::LeftBrace, "expected '{' after if condition") == nullptr ||
        !block(result->body)) {
        return nullptr;
    }
    if (match_keyword("else")) {
        if (match_keyword("if")) {
            auto nested_if = if_statement(previous());
            if (!nested_if) {
                return nullptr;
            }
            result->else_body.push_back(std::move(nested_if));
        } else {
            if (consume(TokenKind::LeftBrace, "expected '{' after else") == nullptr ||
                !block(result->else_body)) {
                return nullptr;
            }
        }
    }
    result->span.end = previous().span.end;
    return result;
}

std::unique_ptr<Stmt> Parser::while_statement(const Token& keyword) {
    auto result = std::make_unique<Stmt>();
    result->kind = StmtKind::While;
    result->span.start = keyword.span.start;
    result->expression = expression();
    if (!result->expression || consume(TokenKind::LeftBrace, "expected '{' after while condition") == nullptr ||
        !block(result->body)) {
        return nullptr;
    }
    result->span.end = previous().span.end;
    return result;
}

std::unique_ptr<Stmt> Parser::repeat_statement(const Token& keyword) {
    auto result = std::make_unique<Stmt>();
    result->kind = StmtKind::Repeat;
    result->span.start = keyword.span.start;
    result->expression = expression();
    if (!result->expression || !match_keyword("times")) {
        error(peek(), "expected 'times' after repeat count");
        return nullptr;
    }
    if (consume(TokenKind::LeftBrace, "expected '{' after repeat count") == nullptr || !block(result->body)) {
        return nullptr;
    }
    result->span.end = previous().span.end;
    return result;
}

std::unique_ptr<Stmt> Parser::for_each_statement(const Token& keyword) {
    const auto* name = consume_identifier("expected loop variable after 'for'");
    if (name == nullptr || !match_keyword("in")) {
        error(peek(), "expected 'in' after loop variable");
        return nullptr;
    }
    auto iterable = expression();
    if (!iterable || consume(TokenKind::LeftBrace, "expected '{' after for-each iterable") == nullptr) {
        return nullptr;
    }

    auto result = std::make_unique<Stmt>();
    result->kind = StmtKind::ForEach;
    result->name = name->lexeme;
    result->span.start = keyword.span.start;
    result->expression = std::move(iterable);
    if (!block(result->body)) {
        return nullptr;
    }
    result->span.end = previous().span.end;
    return result;
}

bool Parser::block(std::vector<std::unique_ptr<Stmt>>& statements) {
    skip_separators();
    while (!check(TokenKind::RightBrace) && !at_end()) {
        const auto start = current_;
        auto parsed = statement();
        if (parsed) {
            statements.push_back(std::move(parsed));
        } else {
            synchronize();
        }
        if (current_ == start) {
            advance();
        }
        skip_separators();
    }
    return consume(TokenKind::RightBrace, "expected '}' after block") != nullptr;
}

std::unique_ptr<Expr> Parser::expression() {
    return parse_precedence(1);
}

std::unique_ptr<Expr> Parser::parse_precedence(int minimum_precedence) {
    auto left = unary();
    if (!left) {
        return nullptr;
    }

    while (precedence(peek().kind) >= minimum_precedence) {
        const auto operation = advance();
        const auto operation_precedence = precedence(operation.kind);
        auto right = parse_precedence(operation_precedence + 1);
        if (!right) {
            return nullptr;
        }
        auto combined = std::make_unique<Expr>();
        combined->kind = operation.kind == TokenKind::DotDot ? ExprKind::Range : ExprKind::Binary;
        combined->value = operation.lexeme;
        combined->span = {left->span.start, right->span.end};
        combined->left = std::move(left);
        combined->right = std::move(right);
        left = std::move(combined);
    }
    return left;
}

std::unique_ptr<Expr> Parser::unary() {
    if (match(TokenKind::Bang) || match(TokenKind::Minus) || match(TokenKind::Plus) ||
        match(TokenKind::Tilde) || match_keyword("move") || match_keyword("borrow") ||
        match_keyword("borrow_mut")) {
        const auto operation = previous();
        auto operand = unary();
        if (!operand) {
            return nullptr;
        }
        auto result = std::make_unique<Expr>();
        result->kind = ExprKind::Unary;
        result->value = operation.lexeme;
        result->span = {operation.span.start, operand->span.end};
        result->left = std::move(operand);
        return result;
    }
    return primary();
}

std::unique_ptr<Expr> Parser::primary() {
    if (match(TokenKind::IntegerLiteral) || match(TokenKind::FloatLiteral) ||
        match(TokenKind::StringLiteral) || match(TokenKind::Keyword)) {
        const auto& token = previous();
        if (token.kind == TokenKind::Keyword && token.lexeme != "true" && token.lexeme != "false") {
            error(token, "expected an expression");
            return nullptr;
        }
        auto result = std::make_unique<Expr>();
        result->kind = ExprKind::Literal;
        result->value = token.lexeme;
        result->span = token.span;
        return result;
    }

    if (match(TokenKind::Identifier)) {
        auto result = std::make_unique<Expr>();
        result->kind = ExprKind::Name;
        result->value = previous().lexeme;
        result->span = previous().span;
        return finish_postfix(std::move(result));
    }

    if (match(TokenKind::LeftBracket)) {
        auto result = std::make_unique<Expr>();
        result->kind = ExprKind::ListLiteral;
        const auto start = previous().span.start;
        if (!check(TokenKind::RightBracket)) {
            while (true) {
                auto element = expression();
                if (!element) {
                    synchronize_expression(TokenKind::RightBracket);
                    if (match(TokenKind::Comma)) continue;
                    break;
                }
                result->arguments.push_back(std::move(element));
                if (!match(TokenKind::Comma)) break;
            }
        }
        const auto* closing = consume(TokenKind::RightBracket, "expected ']' after list elements");
        if (closing == nullptr) {
            return nullptr;
        }
        result->span = {start, closing->span.end};
        return finish_postfix(std::move(result));
    }

    if (match(TokenKind::LeftParen)) {
        auto result = expression();
        if (consume(TokenKind::RightParen, "expected ')' after expression") == nullptr) {
            return nullptr;
        }
        return result;
    }

    error(peek(), "expected an expression");
    return nullptr;
}

std::unique_ptr<Expr> Parser::finish_postfix(std::unique_ptr<Expr> expression) {
    while (true) {
        if (check(TokenKind::LeftParen) && expression->kind == ExprKind::Name) {
            advance();
            expression = finish_call(std::move(expression));
            if (!expression) {
                return nullptr;
            }
        } else if (match(TokenKind::LeftBracket)) {
            auto index = this->expression();
            if (!index) {
                synchronize_expression(TokenKind::RightBracket);
                if (consume(TokenKind::RightBracket, "expected ']' after index") == nullptr) {
                    return nullptr;
                }
                return nullptr;
            }
            const auto* closing = consume(TokenKind::RightBracket, "expected ']' after index");
            if (closing == nullptr) {
                return nullptr;
            }
            auto indexed = std::make_unique<Expr>();
            indexed->kind = ExprKind::Index;
            indexed->span = {expression->span.start, closing->span.end};
            indexed->left = std::move(expression);
            indexed->right = std::move(index);
            expression = std::move(indexed);
        } else if (match(TokenKind::Dot)) {
            const auto* member_name = consume_identifier("expected member name after '.'");
            if (member_name == nullptr) {
                return nullptr;
            }
            auto member = std::make_unique<Expr>();
            member->kind = ExprKind::Member;
            member->value = member_name->lexeme;
            member->span = {expression->span.start, member_name->span.end};
            member->left = std::move(expression);
            expression = std::move(member);
        } else {
            return expression;
        }
    }
}

std::unique_ptr<Expr> Parser::finish_call(std::unique_ptr<Expr> callee) {
    auto result = std::make_unique<Expr>();
    result->kind = ExprKind::Call;
    result->span.start = callee->span.start;
    result->value = callee->value;
    bool saw_named_argument = false;
    if (!check(TokenKind::RightParen)) {
        while (true) {
            std::string argument_name;
            if (check(TokenKind::Identifier) && check_next(TokenKind::Colon)) {
                saw_named_argument = true;
                argument_name = advance().lexeme;
                advance();
            } else if (saw_named_argument) {
                error(peek(), "positional arguments cannot follow named arguments");
                synchronize_expression(TokenKind::RightParen);
                break;
            }
            auto argument = expression();
            if (!argument) {
                synchronize_expression(TokenKind::RightParen);
                if (match(TokenKind::Comma)) continue;
                break;
            }
            result->arguments.push_back(std::move(argument));
            result->argument_names.push_back(std::move(argument_name));
            if (!match(TokenKind::Comma)) break;
        }
    }
    const auto* closing = consume(TokenKind::RightParen, "expected ')' after arguments");
    if (closing == nullptr) {
        return nullptr;
    }
    result->span.end = closing->span.end;
    return result;
}

int Parser::precedence(TokenKind kind) {
    switch (kind) {
    case TokenKind::OrOr: return 1;
    case TokenKind::AndAnd: return 2;
    case TokenKind::DotDot: return 3;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual: return 4;
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual: return 5;
    case TokenKind::Plus:
    case TokenKind::Minus: return 6;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent: return 7;
    default: return 0;
    }
}

}