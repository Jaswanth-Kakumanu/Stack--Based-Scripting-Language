#include "cairn/diagnostic.hpp"

#include <ostream>
#include <utility>

namespace cairn {
namespace {

// Returns the (1-based) requested line of `source`, without its newline.
std::string_view line_at(std::string_view source, std::uint32_t line) {
    std::uint32_t current = 1;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= source.size(); ++i) {
        const bool is_break = (i == source.size()) || (source[i] == '\n');
        if (!is_break) {
            continue;
        }
        if (current == line) {
            std::string_view result = source.substr(start, i - start);
            if (!result.empty() && result.back() == '\r') {
                result.remove_suffix(1);  // tolerate CRLF sources
            }
            return result;
        }
        ++current;
        start = i + 1;
    }
    return {};
}

const char* severity_label(Severity severity) noexcept {
    return severity == Severity::Error ? "error" : "warning";
}

}  // namespace

void DiagnosticBag::error(std::string message, std::uint32_t line,
                          std::uint32_t column, std::uint32_t length) {
    diagnostics_.push_back(
        Diagnostic{Severity::Error, std::move(message), line, column, length});
}

void DiagnosticBag::warning(std::string message, std::uint32_t line,
                            std::uint32_t column, std::uint32_t length) {
    diagnostics_.push_back(
        Diagnostic{Severity::Warning, std::move(message), line, column, length});
}

bool DiagnosticBag::has_errors() const noexcept {
    for (const Diagnostic& d : diagnostics_) {
        if (d.severity == Severity::Error) {
            return true;
        }
    }
    return false;
}

void DiagnosticBag::render(std::ostream& out, std::string_view source,
                           std::string_view filename) const {
    for (const Diagnostic& d : diagnostics_) {
        out << filename << ':' << d.line << ':' << d.column << ": "
            << severity_label(d.severity) << ": " << d.message << '\n';

        const std::string_view text = line_at(source, d.line);
        if (text.empty()) {
            continue;
        }

        out << "    " << text << '\n' << "    ";

        // Copy tabs across so the caret lines up in the reader's terminal.
        for (std::uint32_t i = 0; i + 1 < d.column && i < text.size(); ++i) {
            out << (text[i] == '\t' ? '\t' : ' ');
        }
        const std::uint32_t width = d.length == 0 ? 1 : d.length;
        for (std::uint32_t i = 0; i < width; ++i) {
            out << '^';
        }
        out << '\n';
    }
}

}  // namespace cairn
