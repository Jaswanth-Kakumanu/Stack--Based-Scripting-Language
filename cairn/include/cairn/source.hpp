#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cairn {

// Owns the entire contents of one source file.
//
// The file is read exactly once into a single contiguous buffer. Every Token
// and Diagnostic refers back into this buffer by view or by offset, so the
// SourceFile must outlive them.
class SourceFile {
public:
    // Reads `path` from disk. Returns nullopt and fills `error_out` on failure.
    static std::optional<SourceFile> from_path(const std::string& path,
                                               std::string& error_out);

    // In-memory source, used heavily by the tests.
    static SourceFile from_string(std::string name, std::string contents);

    std::string_view text() const noexcept { return contents_; }
    const std::string& name() const noexcept { return name_; }

private:
    SourceFile(std::string name, std::string contents);

    std::string name_;
    std::string contents_;
};

}  // namespace cairn
