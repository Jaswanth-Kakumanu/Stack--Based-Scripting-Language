#include "cairn/token.hpp"

namespace cairn {

const char* to_string(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Integer:      return "Integer";
        case TokenKind::Identifier:   return "Identifier";
        case TokenKind::True:         return "True";
        case TokenKind::False:        return "False";
        case TokenKind::Let:          return "Let";
        case TokenKind::If:           return "If";
        case TokenKind::Else:         return "Else";
        case TokenKind::While:        return "While";
        case TokenKind::Print:        return "Print";
        case TokenKind::Input:        return "Input";
        case TokenKind::Plus:         return "Plus";
        case TokenKind::Minus:        return "Minus";
        case TokenKind::Star:         return "Star";
        case TokenKind::Slash:        return "Slash";
        case TokenKind::Assign:       return "Assign";
        case TokenKind::EqualEqual:   return "EqualEqual";
        case TokenKind::BangEqual:    return "BangEqual";
        case TokenKind::Less:         return "Less";
        case TokenKind::LessEqual:    return "LessEqual";
        case TokenKind::Greater:      return "Greater";
        case TokenKind::GreaterEqual: return "GreaterEqual";
        case TokenKind::Bang:         return "Bang";
        case TokenKind::LParen:       return "LParen";
        case TokenKind::RParen:       return "RParen";
        case TokenKind::LBrace:       return "LBrace";
        case TokenKind::RBrace:       return "RBrace";
        case TokenKind::Semicolon:    return "Semicolon";
        case TokenKind::EndOfFile:    return "EndOfFile";
        case TokenKind::Invalid:      return "Invalid";
    }
    return "Invalid";
}

bool is_keyword(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Let:
        case TokenKind::If:
        case TokenKind::Else:
        case TokenKind::While:
        case TokenKind::Print:
        case TokenKind::Input:
        case TokenKind::True:
        case TokenKind::False:
            return true;
        default:
            return false;
    }
}

}  // namespace cairn
