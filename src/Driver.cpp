#include "evident/Driver.hpp"

#include "evident/Ast.hpp"
#include "evident/Backend.hpp"
#include "evident/Diagnostic.hpp"
#include "evident/Hir.hpp"
#include "evident/Lexer.hpp"
#include "evident/Mir.hpp"
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

bool has_native_emit_request(const DriverOptions& options) {
    return options.emit_llvm_path.has_value()
        || options.emit_asm_path.has_value()
        || options.emit_obj_path.has_value()
        || options.emit_exe_path.has_value();
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

    std::optional<mir::Package> mir_package;
    if (options.dump_mir || has_native_emit_request(options)) {
        mir_package = mir::lower(package);
    }

    if (options.dump_mir) {
        std::cout << mir::dump(*mir_package);
    }

    if (options.emit_stub_path.has_value()) {
        if (!write_text_file(*options.emit_stub_path, hir::emit_stub_backend(package))) {
            std::cerr << "failed to write stub output to " << *options.emit_stub_path << '\n';
            return 1;
        }
    }

    std::optional<backend::EmitOptions> emit_options;
    if (options.emit_llvm_path.has_value()) {
        emit_options = backend::EmitOptions{backend::EmitKind::Llvm, *options.emit_llvm_path, options.target_triple};
    } else if (options.emit_asm_path.has_value()) {
        emit_options = backend::EmitOptions{backend::EmitKind::Assembly, *options.emit_asm_path, options.target_triple};
    } else if (options.emit_obj_path.has_value()) {
        emit_options = backend::EmitOptions{backend::EmitKind::Object, *options.emit_obj_path, options.target_triple};
    } else if (options.emit_exe_path.has_value()) {
        emit_options = backend::EmitOptions{backend::EmitKind::Executable, *options.emit_exe_path, options.target_triple};
    }

    if (emit_options.has_value()) {
        const std::expected<void, std::string> emitted = backend::emit_artifact(package, *mir_package, *emit_options);
        if (!emitted.has_value()) {
            std::cerr << emitted.error() << '\n';
            return 1;
        }
    }

    return 0;
}

} // namespace evident
