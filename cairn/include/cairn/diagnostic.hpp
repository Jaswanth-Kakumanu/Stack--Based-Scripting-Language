#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace cairn {

enum class Severity : std::uint8_t {
    Error,
    Warning,
};

// One reported problem, anchored to a span of source text.
struct Diagnostic {
    Severity severity = Severity::Error;
    std::string message;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::uint32_t length = 1;  // width of the caret underline
};

// Collects diagnostics instead of throwing.
//
// The lexer never aborts on the first bad character: it records the problem,
// recovers, and keeps scanning. A user with five typos should learn about all
// five in one run.
class DiagnosticBag {
public:
    void error(std::string message, std::uint32_t line, std::uint32_t column,
               std::uint32_t length = 1);
    void warning(std::string message, std::uint32_t line, std::uint32_t column,
                 std::uint32_t length = 1);

    bool has_errors() const noexcept;
    std::size_t size() const noexcept { return diagnostics_.size(); }
    const std::vector<Diagnostic>& all() const noexcept { return diagnostics_; }

    // Pretty-prints every diagnostic as:
    //
    //   file.cairn:3:11: error: unexpected character '@'
    //       let x = @ 5;
    //               ^
    void render(std::ostream& out, std::string_view source,
                std::string_view filename) const;

private:
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace cairn
