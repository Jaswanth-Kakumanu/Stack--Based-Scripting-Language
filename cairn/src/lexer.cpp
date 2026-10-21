#include "cairn/lexer.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace cairn {
namespace {

struct KeywordEntry {
    std::string_view text;
    TokenKind kind;
};

// Eight reserved words. A linear scan over a contiguous constexpr array beats
// an unordered_map here: no hashing, no pointer chasing, and the whole table
// sits in one cache line's worth of comparisons.
constexpr KeywordEntry kKeywords[] = {
    {"let", TokenKind::Let},     {"if", TokenKind::If},
    {"else", TokenKind::Else},   {"while", TokenKind::While},
    {"print", TokenKind::Print}, {"input", TokenKind::Input},
    {"true", TokenKind::True},   {"false", TokenKind::False},
};

TokenKind keyword_or_identifier(std::string_view text) noexcept {
    for (const KeywordEntry& entry : kKeywords) {
        if (entry.text == text) {
            return entry.kind;
        }
    }
    return TokenKind::Identifier;
}

// Locale-independent classifiers. std::isalpha and friends are locale-aware
// and take a signed int, which is a classic UB trap on bytes above 0x7F.
constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

constexpr bool is_ident_start(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

constexpr bool is_ident_continue(char c) noexcept {
    return is_ident_start(c) || is_digit(c);
}

std::string describe(char c) {
    if (c >= 0x20 && c < 0x7F) {
        return std::string("'") + c + "'";
    }
    static const char* kHex = "0123456789ABCDEF";
    const auto byte = static_cast<unsigned char>(c);
    std::string out = "byte 0x";
    out += kHex[byte >> 4];
    out += kHex[byte & 0x0F];
    return out;
}

}  // namespace

Lexer::Lexer(std::string_view source, DiagnosticBag& diagnostics) noexcept
    : source_(source), diagnostics_(diagnostics) {}

bool Lexer::at_end() const noexcept { return offset_ >= source_.size(); }

char Lexer::peek(std::size_t ahead) const noexcept {
    const std::size_t index = static_cast<std::size_t>(offset_) + ahead;
    return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() noexcept {
    const char c = source_[offset_];
    ++offset_;
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char expected) noexcept {
    if (at_end() || source_[offset_] != expected) {
        return false;
    }
    advance();
    return true;
}

Token Lexer::make(TokenKind kind, std::uint32_t start_offset,
                  std::uint32_t start_line,
                  std::uint32_t start_column) const noexcept {
    Token token;
    token.kind = kind;
    token.lexeme = source_.substr(start_offset, offset_ - start_offset);
    token.line = start_line;
    token.column = start_column;
    token.offset = start_offset;
    return token;
}

void Lexer::skip_trivia() {
    for (;;) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }

        // Line comment: // ... to end of line
        if (c == '/' && peek(1) == '/') {
            while (!at_end() && peek() != '\n') {
                advance();
            }
            continue;
        }

        // Block comment: /* ... */ (not nested)
        if (c == '/' && peek(1) == '*') {
            const std::uint32_t start_line = line_;
            const std::uint32_t start_column = column_;
            advance();  // '/'
            advance();  // '*'
            bool closed = false;
            while (!at_end()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                diagnostics_.error("unterminated block comment", start_line,
                                   start_column, 2);
            }
            continue;
        }

        return;
    }
}

Token Lexer::lex_number(std::uint32_t start_offset, std::uint32_t start_line,
                        std::uint32_t start_column) {
    while (is_digit(peek())) {
        advance();
    }

    // Reject 123abc outright. Letting the lexer emit Integer(123) followed by
    // Identifier(abc) would push a confusing parse error onto a later phase,
    // far from the actual mistake.
    if (is_ident_start(peek())) {
        while (is_ident_continue(peek())) {
            advance();
        }
        Token token = make(TokenKind::Invalid, start_offset, start_line, start_column);
        diagnostics_.error("invalid number literal '" + std::string(token.lexeme) + "'",
                           start_line, start_column,
                           static_cast<std::uint32_t>(token.lexeme.size()));
        return token;
    }

    Token token = make(TokenKind::Integer, start_offset, start_line, start_column);

    // Manual accumulation with an overflow guard. std::stoll would allocate and
    // signal failure by throwing, neither of which belongs in a scan loop.
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    std::int64_t value = 0;
    bool overflow = false;
    for (const char c : token.lexeme) {
        const std::int64_t digit = c - '0';
        if (value > (kMax - digit) / 10) {
            overflow = true;
            break;
        }
        value = value * 10 + digit;
    }

    if (overflow) {
        diagnostics_.error(
            "integer literal '" + std::string(token.lexeme) + "' does not fit in 64 bits",
            start_line, start_column, static_cast<std::uint32_t>(token.lexeme.size()));
        token.kind = TokenKind::Invalid;
        return token;
    }

    token.int_value = value;
    return token;
}

Token Lexer::lex_identifier(std::uint32_t start_offset, std::uint32_t start_line,
                            std::uint32_t start_column) {
    while (is_ident_continue(peek())) {
        advance();
    }
    Token token = make(TokenKind::Identifier, start_offset, start_line, start_column);
    token.kind = keyword_or_identifier(token.lexeme);
    return token;
}

Token Lexer::next() {
    skip_trivia();

    const std::uint32_t start_offset = offset_;
    const std::uint32_t start_line = line_;
    const std::uint32_t start_column = column_;

    if (at_end()) {
        return make(TokenKind::EndOfFile, start_offset, start_line, start_column);
    }

    const char c = advance();

    if (is_digit(c)) {
        return lex_number(start_offset, start_line, start_column);
    }
    if (is_ident_start(c)) {
        return lex_identifier(start_offset, start_line, start_column);
    }

    switch (c) {
        case '+': return make(TokenKind::Plus, start_offset, start_line, start_column);
        case '-': return make(TokenKind::Minus, start_offset, start_line, start_column);
        case '*': return make(TokenKind::Star, start_offset, start_line, start_column);
        case '/': return make(TokenKind::Slash, start_offset, start_line, start_column);
        case '(': return make(TokenKind::LParen, start_offset, start_line, start_column);
        case ')': return make(TokenKind::RParen, start_offset, start_line, start_column);
        case '{': return make(TokenKind::LBrace, start_offset, start_line, start_column);
        case '}': return make(TokenKind::RBrace, start_offset, start_line, start_column);
        case ';': return make(TokenKind::Semicolon, start_offset, start_line, start_column);

        // Maximal munch: '==' wins over '=' when both could match.
        case '=':
            return make(match('=') ? TokenKind::EqualEqual : TokenKind::Assign,
                        start_offset, start_line, start_column);
        case '!':
            return make(match('=') ? TokenKind::BangEqual : TokenKind::Bang,
                        start_offset, start_line, start_column);
        case '<':
            return make(match('=') ? TokenKind::LessEqual : TokenKind::Less,
                        start_offset, start_line, start_column);
        case '>':
            return make(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater,
                        start_offset, start_line, start_column);
        default:
            break;
    }

    diagnostics_.error("unexpected character " + describe(c), start_line, start_column, 1);
    return make(TokenKind::Invalid, start_offset, start_line, start_column);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    // One growth step for typical sources: roughly one token per four bytes.
    tokens.reserve(source_.size() / 4 + 8);

    for (;;) {
        Token token = next();
        const bool done = token.kind == TokenKind::EndOfFile;
        tokens.push_back(token);
        if (done) {
            break;
        }
    }
    return tokens;
}

}  // namespace cairn
