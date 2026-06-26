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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>
#include <unordered_set>

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

std::string trim_ascii(std::string text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

bool contains_parent_traversal(const std::filesystem::path& path) {
    for (const std::filesystem::path& part : path) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

std::expected<std::vector<std::string>, std::string> read_package_manifest(
    const std::filesystem::path& root,
    const std::filesystem::path& manifest_path) {
    namespace fs = std::filesystem;

    std::ifstream input(manifest_path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open package manifest: " + manifest_path.string());
    }

    std::vector<std::string> input_paths;
    std::unordered_set<std::string> seen_entries;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim_ascii(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }

        fs::path entry_path(line);
        if (entry_path.has_root_name() || entry_path.has_root_directory()) {
            return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                   + ": package manifest source path must be relative");
        }
        if (contains_parent_traversal(entry_path)) {
            return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                   + ": package manifest source path may not contain '..'");
        }

        entry_path = entry_path.lexically_normal();
        if (entry_path.empty() || entry_path == ".") {
            return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                   + ": package manifest source path is empty");
        }
        if (entry_path.extension() != ".evd") {
            return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                   + ": package manifest source path must end in .evd");
        }

        const std::string entry_key = entry_path.generic_string();
        if (!seen_entries.insert(entry_key).second) {
            return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                   + ": duplicate package manifest source path '" + entry_key + "'");
        }

        const fs::path source_path = root / entry_path;
        std::error_code error;
        if (!fs::exists(source_path, error)) {
            if (error) {
                return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                       + ": failed to inspect package source '" + entry_key + "': "
                                       + error.message());
            }
            return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                   + ": package manifest source does not exist: " + entry_key);
        }
        if (!fs::is_regular_file(source_path, error)) {
            if (error) {
                return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                       + ": failed to inspect package source '" + entry_key + "': "
                                       + error.message());
            }
            return std::unexpected(manifest_path.string() + ":" + std::to_string(line_number)
                                   + ": package manifest source is not a file: " + entry_key);
        }
        input_paths.push_back(source_path.string());
    }

    if (input_paths.empty()) {
        return std::unexpected("package manifest contains no source files: " + manifest_path.string());
    }
    return input_paths;
}

std::expected<std::vector<std::string>, std::string> discover_package_input_paths(const std::string& package_path) {
    namespace fs = std::filesystem;

    const fs::path root(package_path);
    std::error_code error;
    if (!fs::exists(root, error)) {
        if (error) {
            return std::unexpected("failed to inspect package directory '" + package_path + "': " + error.message());
        }
        return std::unexpected("package directory does not exist: " + package_path);
    }
    if (!fs::is_directory(root, error)) {
        if (error) {
            return std::unexpected("failed to inspect package directory '" + package_path + "': " + error.message());
        }
        return std::unexpected("package path is not a directory: " + package_path);
    }

    const fs::path manifest_path = root / "evident.pkg";
    if (fs::exists(manifest_path, error)) {
        if (error) {
            return std::unexpected("failed to inspect package manifest '" + manifest_path.string() + "': "
                                   + error.message());
        }
        if (!fs::is_regular_file(manifest_path, error)) {
            if (error) {
                return std::unexpected("failed to inspect package manifest '" + manifest_path.string() + "': "
                                       + error.message());
            }
            return std::unexpected("package manifest is not a file: " + manifest_path.string());
        }
        return read_package_manifest(root, manifest_path);
    }
    if (error) {
        return std::unexpected("failed to inspect package manifest '" + manifest_path.string() + "': "
                               + error.message());
    }

    std::vector<fs::path> source_paths;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (it != end) {
        if (error) {
            return std::unexpected("failed to read package directory '" + package_path + "': " + error.message());
        }

        std::error_code entry_error;
        if (it->is_regular_file(entry_error) && it->path().extension() == ".evd") {
            source_paths.push_back(it->path());
        }
        it.increment(error);
    }
    if (error) {
        return std::unexpected("failed to read package directory '" + package_path + "': " + error.message());
    }

    std::sort(source_paths.begin(), source_paths.end(), [](const fs::path& lhs, const fs::path& rhs) {
        return lhs.generic_string() < rhs.generic_string();
    });

    std::vector<std::string> input_paths;
    input_paths.reserve(source_paths.size());
    for (const fs::path& source_path : source_paths) {
        input_paths.push_back(source_path.string());
    }
    if (input_paths.empty()) {
        return std::unexpected("package directory contains no .evd source files: " + package_path);
    }
    return input_paths;
}

std::expected<SourceFile, std::string> load_package_source(const DriverOptions& options) {
    std::vector<std::string> input_paths;
    if (options.package_path.has_value()) {
        if (!options.input_paths.empty() || !options.input_path.empty()) {
            return std::unexpected("--package may not be combined with explicit input files");
        }
        std::expected<std::vector<std::string>, std::string> discovered = discover_package_input_paths(*options.package_path);
        if (!discovered.has_value()) {
            return std::unexpected(discovered.error());
        }
        input_paths = std::move(*discovered);
    } else {
        input_paths = options.input_paths;
    }
    if (input_paths.empty() && !options.input_path.empty()) {
        input_paths.push_back(options.input_path);
    }
    if (input_paths.empty()) {
        return std::unexpected("no source input files were provided");
    }

    std::vector<SourceFile> sources;
    sources.reserve(input_paths.size());
    for (const std::string& input_path : input_paths) {
        std::expected<SourceFile, std::string> source_result = SourceFile::load(input_path);
        if (!source_result.has_value()) {
            return std::unexpected(source_result.error());
        }
        sources.push_back(std::move(*source_result));
    }
    return SourceFile::combine(std::move(sources));
}

} // namespace

int run_driver(const DriverOptions& options) {
    const std::expected<SourceFile, std::string> source_result = load_package_source(options);
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
    semantic.analyze(unit, source);
    if (diagnostics.has_errors()) {
        diagnostics.print(source, std::cerr);
        return 1;
    }

    hir::Package package = hir::lower(unit);
    if (options.dump_hir) {
        std::cout << hir::dump(package);
    }

    std::optional<hir::Package> backend_hir_package;
    std::optional<mir::Package> mir_package;
    if (options.dump_mir || has_native_emit_request(options)) {
        backend_hir_package = hir::monomorphize_for_backend(package);
        mir_package = mir::lower(*backend_hir_package);
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
        const std::expected<void, std::string> emitted = backend::emit_artifact(*backend_hir_package,
                                                                                *mir_package,
                                                                                *emit_options);
        if (!emitted.has_value()) {
            std::cerr << emitted.error() << '\n';
            return 1;
        }
    }

    return 0;
}

} // namespace evident
