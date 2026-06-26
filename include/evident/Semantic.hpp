#pragma once

#include "evident/Ast.hpp"
#include "evident/Diagnostic.hpp"

namespace evident {

class SourceFile;

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticSink& diagnostics);
    void analyze(const ast::TranslationUnit& unit, const SourceFile& source);

private:
    DiagnosticSink& diagnostics_;
};

} // namespace evident
