#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cairn/diagnostic.hpp"
#include "cairn/token.hpp"

namespace cairn {

// Hand-written single-pass scanner.
//
// No regex, no generator. The scan is strictly O(n) in the source length with
// at most one byte of lookahead, and allocates nothing per token.
class Lexer {
public:
    Lexer(std::string_view source, DiagnosticBag& diagnostics) noexcept;

    // Scans the whole buffer. The returned vector always ends with an
    // EndOfFile token, even when errors were reported.
    std::vector<Token> tokenize();

    // Produces the next token. Invalid input yields a Token of kind Invalid
    // and a recorded diagnostic; scanning then continues.
    Token next();

private:
    bool at_end() const noexcept;
    char peek(std::size_t ahead = 0) const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;

    void skip_trivia();  // whitespace and comments

    Token make(TokenKind kind, std::uint32_t start_offset, std::uint32_t start_line,
               std::uint32_t start_column) const noexcept;

    Token lex_number(std::uint32_t start_offset, std::uint32_t start_line,
                     std::uint32_t start_column);
    Token lex_identifier(std::uint32_t start_offset, std::uint32_t start_line,
                         std::uint32_t start_column);

    std::string_view source_;
    DiagnosticBag& diagnostics_;

    std::uint32_t offset_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
};

}  // namespace cairn
