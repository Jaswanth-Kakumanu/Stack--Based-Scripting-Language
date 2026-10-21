#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "cairn/chunk.hpp"
#include "cairn/value.hpp"

namespace cairn {

// The stack-based virtual machine.
//
// This is the hot loop the whole project exists to arrive at: one contiguous
// bytecode array, one preallocated value stack, and a dispatch loop that is
// either a switch or a computed-goto jump table depending on the build.
//
// I/O goes through injected streams rather than std::cout/std::cin so that the
// tests can run whole programs and assert on their output.
class VM {
public:
    enum class Result : std::uint8_t {
        Ok,
        RuntimeError,
    };

    VM(std::ostream& out, std::istream& in) noexcept;

    Result run(const Chunk& chunk);

    // Set when run() returns RuntimeError. Already formatted with a line number.
    const std::string& error() const noexcept { return error_; }

    // Instructions retired by the last run(). This is the denominator for the
    // ns/instruction figure that --bench reports.
    std::uint64_t instructions_executed() const noexcept { return instructions_; }

    // Value stack capacity. Fixed and preallocated: growing mid-run would
    // invalidate the raw pointer the loop keeps into it.
    static constexpr std::size_t kStackCapacity = 1u << 16;

    // True when this build dispatches through a computed-goto table rather than
    // a switch. Reported by --bench so a measurement is never ambiguous.
    static bool uses_computed_goto() noexcept;

private:
    std::ostream& out_;
    std::istream& in_;
    std::vector<Value> stack_;
    std::string error_;
    std::uint64_t instructions_ = 0;
};

}  // namespace cairn
