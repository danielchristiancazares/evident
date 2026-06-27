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

enum class DiagnosticErrorState {
    NoErrors,
    ContainsErrors,
};

class Diagnostic final {
public:
    [[nodiscard]] static Diagnostic reported(DiagnosticSeverity severity, SourceSpan span, std::string message);

    [[nodiscard]] DiagnosticSeverity severity() const noexcept;
    [[nodiscard]] SourceSpan span() const noexcept;
    [[nodiscard]] const std::string& message() const noexcept;

private:
    Diagnostic(DiagnosticSeverity severity, SourceSpan span, std::string message);

    DiagnosticSeverity severity_;
    SourceSpan span_;
    std::string message_;
};

class DiagnosticSink {
public:
    void error(SourceSpan span, std::string message);
    void warning(SourceSpan span, std::string message);
    void note(SourceSpan span, std::string message);

    [[nodiscard]] DiagnosticErrorState error_state() const noexcept;
    [[nodiscard]] const std::vector<Diagnostic>& items() const noexcept;
    void print(const SourceFile& source, std::ostream& out) const;

private:
    void push(DiagnosticSeverity severity, SourceSpan span, std::string message);

    std::vector<Diagnostic> items_;
};

} // namespace evident
