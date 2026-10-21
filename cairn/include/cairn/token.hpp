#pragma once

#include <cstdint>
#include <string_view>

namespace cairn {

// The complete token set for the Cairn language.
//
// This enum is the contract between Phase 1 (lexer) and every later phase.
// Adding a token here means touching the parser, compiler and VM, so it is
// deliberately frozen before any scanning code was written.
enum class TokenKind : std::uint8_t {
    // Literals and names
    Integer,
    Identifier,
    True,
    False,

    // Keywords
    Let,
    If,
    Else,
    While,
    Print,
    Input,

    // Arithmetic operators
    Plus,
    Minus,
    Star,
    Slash,

    // Comparison and assignment
    Assign,        // =
    EqualEqual,    // ==
    BangEqual,     // !=
    Less,          // <
    LessEqual,     // <=
    Greater,       // >
    GreaterEqual,  // >=
    Bang,          // !

    // Punctuation
    LParen,
    RParen,
    LBrace,
    RBrace,
    Semicolon,

    // Control
    EndOfFile,
    Invalid,
};

// Stable, human-readable name for a token kind (used by --tokens and tests).
const char* to_string(TokenKind kind) noexcept;

// True for reserved words, i.e. identifiers the lexer promoted to keywords.
bool is_keyword(TokenKind kind) noexcept;

// A single lexical token.
//
// `lexeme` is a non-owning view into the source buffer: the lexer performs
// zero heap allocations per token. The Token is trivially copyable, which
// matters once the parser walks millions of them.
struct Token {
    TokenKind kind = TokenKind::Invalid;
    std::string_view lexeme{};

    // Only meaningful when kind == TokenKind::Integer. Literals are converted
    // here rather than in the parser so that overflow is reported with an
    // exact source location while we still have one.
    std::int64_t int_value = 0;

    std::uint32_t line = 1;    // 1-based
    std::uint32_t column = 1;  // 1-based, counted in bytes
    std::uint32_t offset = 0;  // byte offset into the source buffer
};

}  // namespace cairn
