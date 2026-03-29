#pragma once

#include "evident/Source.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace evident {

enum class DiagnosticSeverity {
    Error,
    Warning,
    Note,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    SourceSpan span{};
    std::string message;
};

class DiagnosticSink {
public:
    void error(SourceSpan span, std::string message);
    void warning(SourceSpan span, std::string message);
    void note(SourceSpan span, std::string message);

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] const std::vector<Diagnostic>& items() const noexcept;
    void print(const SourceFile& source, std::ostream& out) const;

private:
    void push(DiagnosticSeverity severity, SourceSpan span, std::string message);

    std::vector<Diagnostic> items_;
};

} // namespace evident
