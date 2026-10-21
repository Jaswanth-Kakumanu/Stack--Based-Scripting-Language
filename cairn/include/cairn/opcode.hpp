#pragma once

#include <cstddef>
#include <cstdint>

namespace cairn {

// The Cairn instruction set.
//
// One byte per opcode, followed by zero or two bytes of operand (little-endian
// uint16). See DESIGN.md D21 for why the operand width is uniform, and D23 for
// why the comparison operators each get a dedicated opcode instead of being
// synthesised from Equal/Less plus Not.
enum class OpCode : std::uint8_t {
    // Push a value.
    Constant,   // u16 index into the constant pool
    True,       // no operand: 'true' is common enough to deserve its own opcode
    False,

    // Stack slots. Every variable is resolved to a slot at compile time, so
    // there is no name lookup at runtime.
    GetLocal,   // u16 slot
    SetLocal,   // u16 slot; leaves the assigned value on the stack

    Pop,

    // Arithmetic
    Add,
    Sub,
    Mul,
    Div,
    Negate,

    // Logic and comparison
    Not,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,

    // Control flow. Offsets are u16 and relative to the byte after the operand.
    Jump,         // u16 forward offset
    JumpIfFalse,  // u16 forward offset; pops the condition
    Loop,         // u16 backward offset

    // I/O
    Print,
    Input,

    Halt,
};

// Number of distinct opcodes. The VM's computed-goto dispatch table is a flat
// array indexed by opcode byte, and a static_assert against this constant is
// what stops the table and the enum from silently drifting apart.
inline constexpr std::size_t kOpCodeCount = static_cast<std::size_t>(OpCode::Halt) + 1;

const char* to_string(OpCode op) noexcept;

// Number of operand bytes following this opcode: 0 or 2. Used by the
// disassembler and by the VM's decode step in Phase 4.
int operand_bytes(OpCode op) noexcept;

}  // namespace cairn
