#include "evident/Driver.hpp"

#include "evident/Ast.hpp"
#include "evident/Diagnostic.hpp"
#include "evident/Hir.hpp"
#include "evident/Lexer.hpp"
#include "evident/Parser.hpp"
#include "evident/Semantic.hpp"
#include "evident/Source.hpp"
#include "evident/Token.hpp"

#include <fstream>
#include <iostream>

namespace evident {

namespace {

void dump_tokens(const std::vector<Token>& tokens) {
    for (const Token& token : tokens) {
        std::cout << token_kind_name(token.kind) << "\t" << token.lexeme << '\n';
    }
}

bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

} // namespace

int run_driver(const DriverOptions& options) {
    const std::expected<SourceFile, std::string> source_result = SourceFile::load(options.input_path);
    if (!source_result.has_value()) {
        std::cerr << source_result.error() << '\n';
        return 1;
    }
    const SourceFile source = *source_result;

    DiagnosticSink diagnostics;
    Lexer lexer(source, diagnostics);
    std::vector<Token> tokens = lexer.lex();
    if (options.dump_tokens) {
        dump_tokens(tokens);
    }
    if (diagnostics.has_errors()) {
        diagnostics.print(source, std::cerr);
        return 1;
    }

    Parser parser(source, tokens, diagnostics);
    ast::TranslationUnit unit = parser.parse();
    if (diagnostics.has_errors()) {
        diagnostics.print(source, std::cerr);
        return 1;
    }
    if (options.dump_ast) {
        std::cout << ast::dump(unit, source.text());
    }

    SemanticAnalyzer semantic(diagnostics);
    semantic.analyze(unit);
    if (diagnostics.has_errors()) {
        diagnostics.print(source, std::cerr);
        return 1;
    }

    hir::Package package = hir::lower(unit);
    if (options.dump_hir) {
        std::cout << hir::dump(package);
    }

    if (options.emit_stub_path.has_value()) {
        if (!write_text_file(*options.emit_stub_path, hir::emit_stub_backend(package))) {
            std::cerr << "failed to write stub output to " << *options.emit_stub_path << '\n';
            return 1;
        }
    }

    return 0;
}

} // namespace evident
