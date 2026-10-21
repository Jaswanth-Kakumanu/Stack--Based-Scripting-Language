#include "cairn/opcode.hpp"

namespace cairn {

const char* to_string(OpCode op) noexcept {
    switch (op) {
        case OpCode::Constant:     return "CONST";
        case OpCode::True:         return "TRUE";
        case OpCode::False:        return "FALSE";
        case OpCode::GetLocal:     return "GET_LOCAL";
        case OpCode::SetLocal:     return "SET_LOCAL";
        case OpCode::Pop:          return "POP";
        case OpCode::Add:          return "ADD";
        case OpCode::Sub:          return "SUB";
        case OpCode::Mul:          return "MUL";
        case OpCode::Div:          return "DIV";
        case OpCode::Negate:       return "NEG";
        case OpCode::Not:          return "NOT";
        case OpCode::Equal:        return "EQ";
        case OpCode::NotEqual:     return "NEQ";
        case OpCode::Less:         return "LT";
        case OpCode::LessEqual:    return "LE";
        case OpCode::Greater:      return "GT";
        case OpCode::GreaterEqual: return "GE";
        case OpCode::Jump:         return "JUMP";
        case OpCode::JumpIfFalse:  return "JUMP_IF_FALSE";
        case OpCode::Loop:         return "LOOP";
        case OpCode::Print:        return "PRINT";
        case OpCode::Input:        return "INPUT";
        case OpCode::Halt:         return "HALT";
    }
    return "???";
}

int operand_bytes(OpCode op) noexcept {
    switch (op) {
        case OpCode::Constant:
        case OpCode::GetLocal:
        case OpCode::SetLocal:
        case OpCode::Jump:
        case OpCode::JumpIfFalse:
        case OpCode::Loop:
            return 2;
        default:
            return 0;
    }
}

}  // namespace cairn
