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

class SourceLocation final {
public:
    [[nodiscard]] static SourceLocation at(std::size_t offset, std::size_t line, std::size_t column) {
        return SourceLocation(offset, line, column);
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    [[nodiscard]] std::size_t column() const noexcept { return column_; }

private:
    std::size_t offset_;
    std::size_t line_;
    std::size_t column_;

    SourceLocation(std::size_t offset, std::size_t line, std::size_t column)
        : offset_(offset),
          line_(line),
          column_(column) {}
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
