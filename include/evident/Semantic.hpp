#pragma once

#include "evident/Ast.hpp"
#include "evident/Diagnostic.hpp"

namespace evident {

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticSink& diagnostics);
    void analyze(const ast::TranslationUnit& unit);

private:
    DiagnosticSink& diagnostics_;
};

} // namespace evident
