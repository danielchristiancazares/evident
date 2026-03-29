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

    SourceFile() = default;
    SourceFile(std::string path, std::string text);

    [[nodiscard]] const std::string& path() const noexcept;
    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] std::string_view slice(SourceSpan span) const;
    [[nodiscard]] SourceLocation locate(std::size_t offset) const;

private:
    void build_line_index();

    std::string path_;
    std::string text_;
    std::vector<std::size_t> line_starts_;
};

} // namespace evident
