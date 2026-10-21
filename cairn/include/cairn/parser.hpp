#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cairn/ast.hpp"
#include "cairn/diagnostic.hpp"
#include "cairn/token.hpp"

namespace cairn {

// Recursive-descent parser with precedence climbing for binary operators.
//
// Grammar (EBNF):
//
//   program     -> declaration* EOF
//   declaration -> varDecl | statement
//   statement   -> printStmt | ifStmt | whileStmt | block | exprStmt
//   varDecl     -> "let" IDENT "=" expression ";"
//
// Declarations are deliberately NOT statements. An `if` or `while` body is a
// statement, so `if (c) let x = 1;` is a syntax error rather than a variable
// whose existence depends on a branch. See DESIGN.md D27.
//   printStmt   -> "print" expression ";"
//   ifStmt      -> "if" "(" expression ")" statement ( "else" statement )?
//   whileStmt   -> "while" "(" expression ")" statement
//   block       -> "{" declaration* "}"
//   exprStmt    -> expression ";"
//   expression  -> assignment
//   assignment  -> IDENT "=" assignment | binary
//   binary      -> unary ( BINOP unary )*        // precedence climbing
//   unary       -> ( "-" | "!" ) unary | primary
//   primary     -> INTEGER | "true" | "false" | IDENT | "input" | "(" expression ")"
//
// The parser owns no source text: identifier names are string_views borrowed
// from the tokens, which in turn borrow from the SourceFile buffer.
class Parser {
public:
    Parser(const std::vector<Token>& tokens, DiagnosticBag& diagnostics) noexcept;

    // Parses the whole token stream. On error, diagnostics are recorded, the
    // offending statement is skipped, and parsing resumes at the next statement
    // boundary, so one run reports as many problems as it can find.
    std::vector<StmtPtr> parse();

private:
    // Thrown to unwind out of a partially-parsed expression, caught at the
    // statement boundary. See DESIGN.md D14 for why this is the one place the
    // project uses exceptions.
    struct ParseError {};

    const Token& peek(std::size_t ahead = 0) const noexcept;
    const Token& previous() const noexcept;
    bool at_end() const noexcept;
    bool check(TokenKind kind) const noexcept;
    const Token& advance() noexcept;
    bool match(TokenKind kind) noexcept;
    const Token& consume(TokenKind kind, const char* what);

    [[noreturn]] void fail(const Token& token, const std::string& message);
    // Panic-mode recovery. `statement_start` is where the failed statement
    // began, so that a statement which consumed nothing still makes progress.
    void synchronize(std::size_t statement_start);

    StmtPtr declaration();
    StmtPtr statement();
    StmtPtr var_declaration();
    StmtPtr print_statement();
    StmtPtr if_statement();
    StmtPtr while_statement();
    StmtPtr block();
    StmtPtr expression_statement();

    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr binary(int min_precedence);
    ExprPtr unary();
    ExprPtr primary();

    std::vector<Token> tokens_;
    DiagnosticBag& diagnostics_;
    std::size_t current_ = 0;
};

}  // namespace cairn
