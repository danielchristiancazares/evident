#include "evident/Diagnostic.hpp"

#include <algorithm>
#include <iostream>
#include <ostream>
#include <string_view>
#include <utility>

namespace evident {

namespace {

std::string_view severity_name(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Note:
        return "note";
    }
    return "error";
}

} // namespace

Diagnostic Diagnostic::reported(DiagnosticSeverity severity, SourceSpan span, std::string message) {
    return Diagnostic(severity, span, std::move(message));
}

Diagnostic::Diagnostic(DiagnosticSeverity severity, SourceSpan span, std::string message)
    : severity_(severity), span_(span), message_(std::move(message)) {}

DiagnosticSeverity Diagnostic::severity() const noexcept {
    return severity_;
}

SourceSpan Diagnostic::span() const noexcept {
    return span_;
}

const std::string& Diagnostic::message() const noexcept {
    return message_;
}

void DiagnosticSink::error(SourceSpan span, std::string message) {
    push(DiagnosticSeverity::Error, span, std::move(message));
}

void DiagnosticSink::warning(SourceSpan span, std::string message) {
    push(DiagnosticSeverity::Warning, span, std::move(message));
}

void DiagnosticSink::note(SourceSpan span, std::string message) {
    push(DiagnosticSeverity::Note, span, std::move(message));
}

DiagnosticErrorState DiagnosticSink::error_state() const noexcept {
    if (std::any_of(items_.begin(), items_.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity() == DiagnosticSeverity::Error;
    })) {
        return DiagnosticErrorState::ContainsErrors;
    }
    return DiagnosticErrorState::NoErrors;
}

const std::vector<Diagnostic>& DiagnosticSink::items() const noexcept {
    return items_;
}

void DiagnosticSink::print(const SourceFile& source, std::ostream& out) const {
    for (const Diagnostic& diagnostic : items_) {
        const SourceSpan span = diagnostic.span();
        const SourceLocation location = source.locate(span.begin);
        out << source.path_at(span.begin) << ':' << location.line << ':' << location.column << ": "
            << severity_name(diagnostic.severity()) << ": " << diagnostic.message() << '\n';

        const SourceLocation line_location = source.locate(span.begin);
        const std::size_t line_start = source.locate(span.begin).offset - (line_location.column - 1);
        std::size_t line_end = line_start;
        while (line_end < source.text().size() && source.text()[line_end] != '\n') {
            ++line_end;
        }

        const std::string_view line_text = source.slice(SourceSpan{line_start, line_end});
        out << "    " << line_text << '\n';
        out << "    ";
        for (std::size_t i = 1; i < line_location.column; ++i) {
            out << ' ';
        }

        std::size_t highlight_width = 1;
        if (span.end > span.begin) {
            highlight_width = span.end - span.begin;
            if (line_start + highlight_width > line_end) {
                highlight_width = std::max<std::size_t>(1, line_end - span.begin);
            }
        }
        for (std::size_t i = 0; i < highlight_width; ++i) {
            out << '^';
        }
        out << '\n';
    }
}

void DiagnosticSink::push(DiagnosticSeverity severity, SourceSpan span, std::string message) {
    items_.push_back(Diagnostic::reported(severity, span, std::move(message)));
}

} // namespace evident
