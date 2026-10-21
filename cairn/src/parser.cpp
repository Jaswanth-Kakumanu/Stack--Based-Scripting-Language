#include "cairn/parser.hpp"

#include <string>
#include <utility>

namespace cairn {
namespace {

// Binding power for binary operators. Higher binds tighter. Zero means "not a
// binary operator", which is how the climbing loop knows to stop.
//
// One table replaces the usual cascade of equality()/comparison()/term()/factor()
// functions. Adding an operator is a row here, not a new function plus two edits
// to its neighbours.
int precedence_of(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
            return 1;
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return 2;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return 3;
        case TokenKind::Star:
        case TokenKind::Slash:
            return 4;
        default:
            return 0;
    }
}

BinaryOp binary_op_of(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Plus:         return BinaryOp::Add;
        case TokenKind::Minus:        return BinaryOp::Sub;
        case TokenKind::Star:         return BinaryOp::Mul;
        case TokenKind::Slash:        return BinaryOp::Div;
        case TokenKind::EqualEqual:   return BinaryOp::Equal;
        case TokenKind::BangEqual:    return BinaryOp::NotEqual;
        case TokenKind::Less:         return BinaryOp::Less;
        case TokenKind::LessEqual:    return BinaryOp::LessEqual;
        case TokenKind::Greater:      return BinaryOp::Greater;
        default:                      return BinaryOp::GreaterEqual;
    }
}

// Tokens that can only begin a new statement. Recovery stops at these.
bool is_statement_start(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Let:
        case TokenKind::If:
        case TokenKind::While:
        case TokenKind::Print:
        case TokenKind::LBrace:
        case TokenKind::RBrace:
            return true;
        default:
            return false;
    }
}

SourceLocation location_of(const Token& token) noexcept {
    return SourceLocation{token.line, token.column};
}

std::string describe(const Token& token) {
    if (token.kind == TokenKind::EndOfFile) {
        return "end of file";
    }
    return "'" + std::string(token.lexeme) + "'";
}

}  // namespace

Parser::Parser(const std::vector<Token>& tokens, DiagnosticBag& diagnostics) noexcept
    : diagnostics_(diagnostics) {
    // Invalid tokens are kept, not filtered. Deleting them would silently close
    // the hole a bad character left behind and let a malformed program parse as
    // if it were fine; keeping them means the parser trips exactly where the
    // problem is. What it must not do is complain a second time - see fail().
    tokens_ = tokens;
    if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile) {
        Token eof;
        eof.kind = TokenKind::EndOfFile;
        tokens_.push_back(eof);
    }
}

const Token& Parser::peek(std::size_t ahead) const noexcept {
    const std::size_t index = current_ + ahead;
    return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

const Token& Parser::previous() const noexcept {
    return current_ > 0 ? tokens_[current_ - 1] : tokens_.front();
}

bool Parser::at_end() const noexcept { return peek().kind == TokenKind::EndOfFile; }

bool Parser::check(TokenKind kind) const noexcept { return peek().kind == kind; }

const Token& Parser::advance() noexcept {
    if (!at_end()) {
        ++current_;
    }
    return previous();
}

bool Parser::match(TokenKind kind) noexcept {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

const Token& Parser::consume(TokenKind kind, const char* what) {
    if (check(kind)) {
        return advance();
    }
    fail(peek(), std::string("expected ") + what + ", found " + describe(peek()));
}

void Parser::fail(const Token& token, const std::string& message) {
    // An Invalid token is a lexical error the lexer has already described. Two
    // diagnostics for one typo - "unexpected character '@'" followed by
    // "expected an expression" - is noise, so the unwind happens silently and
    // the user sees only the message that names the actual cause.
    if (token.kind != TokenKind::Invalid) {
        const std::uint32_t width =
            token.lexeme.empty() ? 1u : static_cast<std::uint32_t>(token.lexeme.size());
        diagnostics_.error(message, token.line, token.column, width);
    }
    throw ParseError{};
}

// Panic-mode recovery: discard tokens until something that can only begin a new
// statement. Without this, one missing semicolon cascades into a screenful of
// bogus errors.
void Parser::synchronize(std::size_t statement_start) {
    // A statement that failed on its very first token consumed nothing. Forcing
    // one step here is what guarantees the parse loop terminates - without it,
    // input like "} ; }" spins forever re-failing on the same token.
    if (current_ == statement_start) {
        advance();
    }

    while (!at_end()) {
        if (previous().kind == TokenKind::Semicolon) {
            return;  // just past a statement boundary
        }
        if (is_statement_start(peek().kind)) {
            return;  // right before one
        }
        advance();
    }
}

std::vector<StmtPtr> Parser::parse() {
    std::vector<StmtPtr> program;
    while (!at_end()) {
        const std::size_t start = current_;
        try {
            program.push_back(declaration());
        } catch (const ParseError&) {
            synchronize(start);
        }
    }
    return program;
}

StmtPtr Parser::declaration() {
    if (check(TokenKind::Let)) {
        return var_declaration();
    }
    return statement();
}

StmtPtr Parser::statement() {
    // A declaration here would be one whose existence depends on a branch,
    // which the compiler's compile-time slot assignment cannot express. The
    // grammar rules it out rather than the compiler catching it later.
    if (check(TokenKind::Let)) {
        fail(peek(),
             "a declaration cannot be the body of 'if' or 'while'; wrap it in braces");
    }
    if (check(TokenKind::Print))  return print_statement();
    if (check(TokenKind::If))     return if_statement();
    if (check(TokenKind::While))  return while_statement();
    if (check(TokenKind::LBrace)) return block();
    return expression_statement();
}

StmtPtr Parser::var_declaration() {
    const Token& keyword = advance();  // 'let'
    const Token& name = consume(TokenKind::Identifier, "a variable name after 'let'");
    consume(TokenKind::Assign, "'=' after the variable name");
    ExprPtr initializer = expression();
    consume(TokenKind::Semicolon, "';' after the declaration");
    return std::make_unique<VarDeclStmt>(name.lexeme, std::move(initializer),
                                         location_of(keyword));
}

StmtPtr Parser::print_statement() {
    const Token& keyword = advance();  // 'print'
    ExprPtr value = expression();
    consume(TokenKind::Semicolon, "';' after the printed expression");
    return std::make_unique<PrintStmt>(std::move(value), location_of(keyword));
}

StmtPtr Parser::if_statement() {
    const Token& keyword = advance();  // 'if'
    consume(TokenKind::LParen, "'(' after 'if'");
    ExprPtr condition = expression();
    consume(TokenKind::RParen, "')' after the condition");

    StmtPtr then_branch = statement();
    StmtPtr else_branch;
    // 'else' binds to the nearest unmatched 'if' simply because this recursion
    // consumes it at the innermost level. The dangling-else ambiguity resolves
    // itself; no special case needed.
    if (match(TokenKind::Else)) {
        else_branch = statement();
    }
    return std::make_unique<IfStmt>(std::move(condition), std::move(then_branch),
                                    std::move(else_branch), location_of(keyword));
}

StmtPtr Parser::while_statement() {
    const Token& keyword = advance();  // 'while'
    consume(TokenKind::LParen, "'(' after 'while'");
    ExprPtr condition = expression();
    consume(TokenKind::RParen, "')' after the condition");
    StmtPtr body = statement();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body),
                                       location_of(keyword));
}

StmtPtr Parser::block() {
    const Token& brace = advance();  // '{'
    std::vector<StmtPtr> statements;

    while (!check(TokenKind::RBrace) && !at_end()) {
        const std::size_t start = current_;
        try {
            statements.push_back(declaration());
        } catch (const ParseError&) {
            synchronize(start);
        }
    }

    consume(TokenKind::RBrace, "'}' to close the block");
    return std::make_unique<BlockStmt>(std::move(statements), location_of(brace));
}

StmtPtr Parser::expression_statement() {
    const Token& start = peek();
    ExprPtr expr = expression();
    consume(TokenKind::Semicolon, "';' after the expression");
    return std::make_unique<ExpressionStmt>(std::move(expr), location_of(start));
}

ExprPtr Parser::expression() { return assignment(); }

ExprPtr Parser::assignment() {
    ExprPtr target = binary(1);

    if (match(TokenKind::Assign)) {
        const Token& equals = previous();
        // Right-associative: a = b = c parses as a = (b = c).
        ExprPtr value = assignment();

        // The kind tag answers this without RTTI.
        if (target->kind != ExprKind::Variable) {
            fail(equals, "left side of '=' is not a variable");
        }
        const auto& variable = static_cast<const VariableExpr&>(*target);
        return std::make_unique<AssignExpr>(variable.name, std::move(value),
                                            variable.loc);
    }

    return target;
}

// Precedence climbing. Every operator here is left-associative, so the
// recursive call is made with min_precedence + 1; a right-associative operator
// would pass min_precedence unchanged.
ExprPtr Parser::binary(int min_precedence) {
    ExprPtr left = unary();

    for (;;) {
        const int precedence = precedence_of(peek().kind);
        if (precedence == 0 || precedence < min_precedence) {
            return left;
        }
        const Token& op_token = advance();
        ExprPtr right = binary(precedence + 1);
        left = std::make_unique<BinaryExpr>(binary_op_of(op_token.kind), std::move(left),
                                            std::move(right), location_of(op_token));
    }
}

ExprPtr Parser::unary() {
    if (check(TokenKind::Minus) || check(TokenKind::Bang)) {
        const Token& op_token = advance();
        const UnaryOp op =
            op_token.kind == TokenKind::Minus ? UnaryOp::Negate : UnaryOp::Not;
        // Recursing into unary() rather than primary() makes --x and !!x parse.
        ExprPtr operand = unary();
        return std::make_unique<UnaryExpr>(op, std::move(operand), location_of(op_token));
    }
    return primary();
}

ExprPtr Parser::primary() {
    const Token& token = peek();

    switch (token.kind) {
        case TokenKind::Integer:
            advance();
            return std::make_unique<IntLiteralExpr>(token.int_value, location_of(token));
        case TokenKind::True:
            advance();
            return std::make_unique<BoolLiteralExpr>(true, location_of(token));
        case TokenKind::False:
            advance();
            return std::make_unique<BoolLiteralExpr>(false, location_of(token));
        case TokenKind::Identifier:
            advance();
            return std::make_unique<VariableExpr>(token.lexeme, location_of(token));
        case TokenKind::Input:
            advance();
            return std::make_unique<InputExpr>(location_of(token));
        case TokenKind::LParen: {
            advance();
            ExprPtr inner = expression();
            consume(TokenKind::RParen, "')' after the expression");
            // No Grouping node: parentheses exist only to steer the parse, and
            // the resulting tree shape already records their effect.
            return inner;
        }
        default:
            fail(token, "expected an expression, found " + describe(token));
    }
}

}  // namespace cairn
