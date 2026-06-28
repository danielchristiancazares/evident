#include "evident/Source.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace evident {

namespace {

struct Utf8ValidationResult {
    bool well_formed = true;
    std::size_t error_offset = 0;
};

bool is_utf8_continuation(unsigned char byte) {
    return byte >= 0x80U && byte <= 0xBFU;
}

Utf8ValidationResult validate_utf8(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        unsigned char min_second = 0x80U;
        unsigned char max_second = 0xBFU;
        if (lead >= 0xC2U && lead <= 0xDFU) {
            continuation_count = 1;
        } else if (lead == 0xE0U) {
            continuation_count = 2;
            min_second = 0xA0U;
        } else if (lead >= 0xE1U && lead <= 0xECU) {
            continuation_count = 2;
        } else if (lead == 0xEDU) {
            continuation_count = 2;
            max_second = 0x9FU;
        } else if (lead >= 0xEEU && lead <= 0xEFU) {
            continuation_count = 2;
        } else if (lead == 0xF0U) {
            continuation_count = 3;
            min_second = 0x90U;
        } else if (lead >= 0xF1U && lead <= 0xF3U) {
            continuation_count = 3;
        } else if (lead == 0xF4U) {
            continuation_count = 3;
            max_second = 0x8FU;
        } else {
            return Utf8ValidationResult{false, index};
        }

        if (index + continuation_count >= text.size()) {
            return Utf8ValidationResult{false, index};
        }

        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        if (second < min_second || second > max_second) {
            return Utf8ValidationResult{false, index + 1};
        }
        for (std::size_t offset = 2; offset <= continuation_count; ++offset) {
            if (!is_utf8_continuation(static_cast<unsigned char>(text[index + offset]))) {
                return Utf8ValidationResult{false, index + offset};
            }
        }
        index += continuation_count + 1;
    }
    return Utf8ValidationResult{};
}

} // namespace

std::expected<SourceFile, std::string> SourceFile::load(std::string path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open source file: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();
    const Utf8ValidationResult utf8 = validate_utf8(text);
    if (!utf8.well_formed) {
        return std::unexpected("source file is not well-formed UTF-8 at byte offset "
                               + std::to_string(utf8.error_offset) + ": " + path);
    }
    if (const std::size_t nul_offset = text.find('\0'); nul_offset != std::string::npos) {
        return std::unexpected("source file contains U+0000 at byte offset "
                               + std::to_string(nul_offset) + ": " + path);
    }
    return SourceFile(std::move(path), std::move(text));
}

SourceFile::SourceFile(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)) {
    build_line_index();
    segments_.push_back(Segment{path_, 0, text_.size(), line_starts_});
}

SourceFile::SourceFile(std::string path, std::string text, std::vector<Segment> segments)
    : path_(std::move(path)), text_(std::move(text)), segments_(std::move(segments)) {
    build_line_index();
}

SourceFile SourceFile::combine(std::vector<SourceFile> sources) {
    if (sources.empty()) {
        return SourceFile("<empty package>", {});
    }
    if (sources.size() == 1) {
        return std::move(sources.front());
    }

    std::string combined_text;
    std::vector<Segment> combined_segments;
    combined_segments.reserve(sources.size());
    for (SourceFile& source : sources) {
        if (!combined_text.empty() && combined_text.back() != '\n') {
            combined_text.push_back('\n');
        }

        const std::size_t begin = combined_text.size();
        combined_text += source.text_;
        const std::size_t end = combined_text.size();
        combined_segments.push_back(Segment{source.path_, begin, end, source.line_starts_});
    }
    return SourceFile("<package>", std::move(combined_text), std::move(combined_segments));
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
        return SourceLocation::at(clamped_offset, line_index + 1, local_offset - line_start + 1);
    }

    if (line_starts_.empty()) {
        return SourceLocation::at(offset, 1, offset + 1);
    }

    const auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), clamped_offset);
    const std::size_t line_index = (it == line_starts_.begin()) ? 0 : static_cast<std::size_t>((it - line_starts_.begin()) - 1);
    const std::size_t line_start = line_starts_[line_index];
    return SourceLocation::at(clamped_offset, line_index + 1, clamped_offset - line_start + 1);
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
