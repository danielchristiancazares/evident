#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace evident {

struct SourceSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct SourceLocation {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

class SourceFile {
public:
    static std::expected<SourceFile, std::string> load(std::string path);
    static SourceFile combine(std::vector<SourceFile> sources);

    SourceFile(std::string path, std::string text);

    [[nodiscard]] const std::string& path() const noexcept;
    [[nodiscard]] std::string_view path_at(std::size_t offset) const noexcept;
    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] std::string_view slice(SourceSpan span) const;
    [[nodiscard]] SourceLocation locate(std::size_t offset) const;

private:
    struct Segment {
        std::string path;
        std::size_t begin = 0;
        std::size_t end = 0;
        std::vector<std::size_t> line_starts;
    };

    SourceFile(std::string path, std::string text, std::vector<Segment> segments);

    void build_line_index();
    [[nodiscard]] const Segment* find_segment(std::size_t offset) const noexcept;

    std::string path_;
    std::string text_;
    std::vector<std::size_t> line_starts_;
    std::vector<Segment> segments_;
};

} // namespace evident
