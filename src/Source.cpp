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
    segments_.push_back(Segment{path_, 0, text_.size(), line_starts_});
}

SourceFile SourceFile::combine(std::vector<SourceFile> sources) {
    if (sources.empty()) {
        return SourceFile("<empty package>", {});
    }
    if (sources.size() == 1) {
        return std::move(sources.front());
    }

    SourceFile combined;
    combined.path_ = "<package>";
    for (SourceFile& source : sources) {
        if (!combined.text_.empty() && combined.text_.back() != '\n') {
            combined.text_.push_back('\n');
        }

        const std::size_t begin = combined.text_.size();
        combined.text_ += source.text_;
        const std::size_t end = combined.text_.size();
        combined.segments_.push_back(Segment{source.path_, begin, end, source.line_starts_});
    }
    combined.build_line_index();
    return combined;
}

const std::string& SourceFile::path() const noexcept {
    return path_;
}

std::string_view SourceFile::path_at(std::size_t offset) const noexcept {
    if (const Segment* segment = find_segment(offset); segment != nullptr) {
        return segment->path;
    }
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
    const std::size_t clamped_offset = std::min(offset, text_.size());
    const Segment* segment = find_segment(clamped_offset);
    if (segment == nullptr) {
        segment = find_segment(offset);
    }

    if (segment != nullptr && !segment->line_starts.empty()) {
        const std::size_t local_offset = std::min(clamped_offset - segment->begin, segment->end - segment->begin);
        const auto it = std::upper_bound(segment->line_starts.begin(), segment->line_starts.end(), local_offset);
        const std::size_t line_index = (it == segment->line_starts.begin())
            ? 0
            : static_cast<std::size_t>((it - segment->line_starts.begin()) - 1);
        const std::size_t line_start = segment->line_starts[line_index];
        return SourceLocation{clamped_offset, line_index + 1, local_offset - line_start + 1};
    }

    if (line_starts_.empty()) {
        return SourceLocation{offset, 1, offset + 1};
    }

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

const SourceFile::Segment* SourceFile::find_segment(std::size_t offset) const noexcept {
    if (segments_.empty()) {
        return nullptr;
    }

    const std::size_t clamped_offset = std::min(offset, text_.size());
    for (const Segment& segment : segments_) {
        if (clamped_offset >= segment.begin && clamped_offset < segment.end) {
            return &segment;
        }
        if (clamped_offset == segment.end && clamped_offset == text_.size()) {
            return &segment;
        }
    }

    for (auto it = segments_.rbegin(); it != segments_.rend(); ++it) {
        if (clamped_offset >= it->end) {
            return &*it;
        }
    }
    return &segments_.front();
}

} // namespace evident
