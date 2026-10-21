#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cairn/opcode.hpp"
#include "cairn/value.hpp"

namespace cairn {

// A compiled program: the bytecode itself, the constants it references, and
// enough line information to report a runtime error against source.
//
// The code is a flat std::vector<std::uint8_t>. That is the whole point of the
// bytecode step: the VM's dispatch loop walks one contiguous array with no
// pointer chasing, which is the hot path this project exists to demonstrate.
class Chunk {
public:
    void write(OpCode op, std::uint32_t line);
    void write_u16(std::uint16_t operand, std::uint32_t line);

    // Appends a constant, reusing an existing entry when the value is already
    // present. Returns its index, or kInvalidConstant when the pool is full.
    static constexpr std::uint32_t kInvalidConstant = 0xFFFFFFFFu;
    std::uint32_t add_constant(const Value& value);

    // Overwrites the two operand bytes at `offset`. Used to backpatch jumps
    // whose target was unknown when the instruction was emitted.
    void patch_u16(std::size_t offset, std::uint16_t operand) noexcept;

    std::uint16_t read_u16(std::size_t offset) const noexcept;

    const std::vector<std::uint8_t>& code() const noexcept { return code_; }
    const std::vector<Value>& constants() const noexcept { return constants_; }
    std::size_t size() const noexcept { return code_.size(); }

    // Source line for the instruction byte at `offset`.
    std::uint32_t line_at(std::size_t offset) const noexcept;

    // Human-readable listing, one instruction per line. This is `--bytecode`.
    std::string disassemble(std::string_view name) const;

    // Disassembles the single instruction starting at `offset` and returns the
    // offset of the next one.
    std::size_t disassemble_instruction(std::string& out, std::size_t offset) const;

private:
    std::vector<std::uint8_t> code_;
    std::vector<Value> constants_;

    // Run-length encoded line numbers. Source lines are overwhelmingly runs of
    // consecutive instructions, so storing (line, count) pairs costs a fraction
    // of one uint32 per byte and keeps the code array free of debug data.
    struct LineRun {
        std::uint32_t line = 0;
        std::uint32_t count = 0;
    };
    std::vector<LineRun> lines_;

    void note_line(std::uint32_t line, std::uint32_t bytes);
};

}  // namespace cairn
