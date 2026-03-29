#include "evident/Source.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace evident {

std::expected<SourceFile, std::string> SourceFile::load(std::string path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open source file: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return SourceFile(std::move(path), buffer.str());
}

SourceFile::SourceFile(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)) {
    build_line_index();
}

const std::string& SourceFile::path() const noexcept {
    return path_;
}

const std::string& SourceFile::text() const noexcept {
    return text_;
}

std::string_view SourceFile::slice(SourceSpan span) const {
    const std::size_t safe_begin = std::min(span.begin, text_.size());
    const std::size_t safe_end = std::min(span.end, text_.size());
    if (safe_end < safe_begin) {
        return {};
    }
    return std::string_view(text_).substr(safe_begin, safe_end - safe_begin);
}

SourceLocation SourceFile::locate(std::size_t offset) const {
    if (line_starts_.empty()) {
        return SourceLocation{offset, 1, offset + 1};
    }

    const std::size_t clamped_offset = std::min(offset, text_.size());
    const auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), clamped_offset);
    const std::size_t line_index = (it == line_starts_.begin()) ? 0 : static_cast<std::size_t>((it - line_starts_.begin()) - 1);
    const std::size_t line_start = line_starts_[line_index];
    return SourceLocation{clamped_offset, line_index + 1, clamped_offset - line_start + 1};
}

void SourceFile::build_line_index() {
    line_starts_.clear();
    line_starts_.push_back(0);
    for (std::size_t index = 0; index < text_.size(); ++index) {
        if (text_[index] == '\n') {
            line_starts_.push_back(index + 1);
        }
    }
}

} // namespace evident
