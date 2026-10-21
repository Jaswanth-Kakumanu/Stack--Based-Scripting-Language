#include "cairn/source.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace cairn {

SourceFile::SourceFile(std::string name, std::string contents)
    : name_(std::move(name)), contents_(std::move(contents)) {}

SourceFile SourceFile::from_string(std::string name, std::string contents) {
    return SourceFile(std::move(name), std::move(contents));
}

std::optional<SourceFile> SourceFile::from_path(const std::string& path,
                                                std::string& error_out) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        error_out = "cannot open '" + path + "'";
        return std::nullopt;
    }

    // Read the whole file in one shot; the buffer backs every string_view we
    // hand out afterwards.
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        error_out = "error while reading '" + path + "'";
        return std::nullopt;
    }

    return SourceFile(path, buffer.str());
}

}  // namespace cairn
