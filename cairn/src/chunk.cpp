#include "cairn/chunk.hpp"

#include <iomanip>
#include <limits>
#include <sstream>

namespace cairn {

void Chunk::note_line(std::uint32_t line, std::uint32_t bytes) {
    if (!lines_.empty() && lines_.back().line == line) {
        lines_.back().count += bytes;
        return;
    }
    lines_.push_back(LineRun{line, bytes});
}

void Chunk::write(OpCode op, std::uint32_t line) {
    code_.push_back(static_cast<std::uint8_t>(op));
    note_line(line, 1);
}

void Chunk::write_u16(std::uint16_t operand, std::uint32_t line) {
    // Little-endian, matching the host order the VM will read it back in.
    code_.push_back(static_cast<std::uint8_t>(operand & 0xFF));
    code_.push_back(static_cast<std::uint8_t>((operand >> 8) & 0xFF));
    note_line(line, 2);
}

std::uint32_t Chunk::add_constant(const Value& value) {
    // Deduplicate. A loop body mentioning `1` twenty times should not grow the
    // pool twenty times, and identical constants sharing an index keeps the
    // pool small enough to stay resident in cache.
    for (std::size_t i = 0; i < constants_.size(); ++i) {
        if (constants_[i] == value) {
            return static_cast<std::uint32_t>(i);
        }
    }
    if (constants_.size() > std::numeric_limits<std::uint16_t>::max()) {
        return kInvalidConstant;
    }
    constants_.push_back(value);
    return static_cast<std::uint32_t>(constants_.size() - 1);
}

void Chunk::patch_u16(std::size_t offset, std::uint16_t operand) noexcept {
    if (offset + 1 >= code_.size()) {
        return;
    }
    code_[offset] = static_cast<std::uint8_t>(operand & 0xFF);
    code_[offset + 1] = static_cast<std::uint8_t>((operand >> 8) & 0xFF);
}

std::uint16_t Chunk::read_u16(std::size_t offset) const noexcept {
    if (offset + 1 >= code_.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(code_[offset] |
                                      (static_cast<std::uint16_t>(code_[offset + 1]) << 8));
}

std::uint32_t Chunk::line_at(std::size_t offset) const noexcept {
    std::size_t consumed = 0;
    for (const LineRun& run : lines_) {
        consumed += run.count;
        if (offset < consumed) {
            return run.line;
        }
    }
    return lines_.empty() ? 0 : lines_.back().line;
}

std::size_t Chunk::disassemble_instruction(std::string& out, std::size_t offset) const {
    std::ostringstream line;
    line << std::setfill('0') << std::setw(4) << offset << std::setfill(' ') << "  ";

    // Print the source line only when it changes, so runs stay readable.
    const std::uint32_t source_line = line_at(offset);
    if (offset > 0 && source_line == line_at(offset - 1)) {
        line << "   |  ";
    } else {
        line << std::setw(4) << source_line << "  ";
    }

    const auto op = static_cast<OpCode>(code_[offset]);
    const int operands = operand_bytes(op);

    if (operands == 0) {
        // No trailing padding when there is no operand to align against, so the
        // listing diffs cleanly in tests.
        line << to_string(op);
        out += line.str();
        out += '\n';
        return offset + 1;
    }
    line << std::left << std::setw(15) << to_string(op) << std::right;

    const std::uint16_t operand = read_u16(offset + 1);
    const std::size_t next = offset + 3;
    line << std::setw(5) << operand;

    switch (op) {
        case OpCode::Constant:
            if (operand < constants_.size()) {
                line << "   ; " << to_string(constants_[operand]);
            }
            break;
        case OpCode::Jump:
        case OpCode::JumpIfFalse:
            line << "   -> " << std::setfill('0') << std::setw(4) << (next + operand);
            break;
        case OpCode::Loop:
            line << "   -> " << std::setfill('0') << std::setw(4)
                 << (next >= operand ? next - operand : 0);
            break;
        default:
            break;
    }

    out += line.str();
    out += '\n';
    return next;
}

std::string Chunk::disassemble(std::string_view name) const {
    std::string out = "== ";
    out.append(name);
    out += " ==\n";

    std::size_t offset = 0;
    while (offset < code_.size()) {
        offset = disassemble_instruction(out, offset);
    }

    out += "-- constants --\n";
    for (std::size_t i = 0; i < constants_.size(); ++i) {
        std::ostringstream line;
        line << std::setfill('0') << std::setw(4) << i << std::setfill(' ') << "  "
             << to_string(constants_[i]) << '\n';
        out += line.str();
    }
    return out;
}

}  // namespace cairn
