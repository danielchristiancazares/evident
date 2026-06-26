#include "evident/Driver.hpp"

#include "evident/Backend.hpp"

#include <expected>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#ifndef EVIDENT_VERSION
#define EVIDENT_VERSION "0.0.0-dev"
#endif

namespace {

void print_usage(std::ostream& out) {
    out << "usage: evidc [--dump-tokens] [--dump-ast] [--dump-hir] [--dump-mir] "
           "[--target <triple>] [--print-toolchain] [--check-toolchain] [--emit-stub <path>] "
           "[--emit-llvm <path>] [--emit-asm <path>] [--emit-obj <path>] "
           "[--emit-exe <path>] [--package <dir> | <input.evd> [input2.evd ...]]\n";
}

void print_help(std::ostream& out) {
    print_usage(out);
    out << "\n"
           "Options:\n"
           "  -h, --help                 Show this help text and exit\n"
           "      --version              Show compiler version and exit\n"
           "      --dump-tokens          Print lexer tokens\n"
           "      --dump-ast             Print parsed AST\n"
           "      --dump-hir             Print lowered HIR\n"
           "      --dump-mir             Print lowered MIR\n"
           "      --target <triple>      Set native target triple (default: x86_64-pc-windows-msvc)\n"
           "      --print-toolchain      Print selected native target and toolchain settings\n"
           "      --check-toolchain      Validate target and probe clang/lld-link versions\n"
           "      --emit-stub <path>     Write stub backend output\n"
           "      --emit-llvm <path>     Write textual LLVM IR\n"
           "      --emit-asm <path>      Emit assembly through clang\n"
           "      --emit-obj <path>      Emit object through clang\n"
           "      --emit-exe <path>      Emit executable through clang and lld\n"
           "      --package <dir>        Compile package directory sources\n";
}

bool require_option_value(int index, int argc, const std::string& option) {
    if (index + 1 < argc) {
        return true;
    }
    std::cerr << "missing argument for " << option << '\n';
    print_usage(std::cerr);
    return false;
}

bool has_compile_action(const evident::DriverOptions& options, const std::vector<std::string>& positional) {
    return options.dump_tokens
        || options.dump_ast
        || options.dump_hir
        || options.dump_mir
        || options.emit_stub_path.has_value()
        || options.emit_llvm_path.has_value()
        || options.emit_asm_path.has_value()
        || options.emit_obj_path.has_value()
        || options.emit_exe_path.has_value()
        || options.package_path.has_value()
        || !positional.empty();
}

void print_toolchain(const evident::DriverOptions& options, std::ostream& out) {
    out << "native target: " << options.target_triple << '\n'
        << "supported native target: " << evident::backend::supported_target_triple() << '\n'
        << "clang driver: " << evident::backend::selected_toolchain_driver() << '\n'
        << "clang override env: " << evident::backend::toolchain_driver_environment_variable() << '\n'
        << "linker mode: clang -fuse-ld=lld\n";
}

} // namespace

int main(int argc, char** argv) {
    evident::DriverOptions options;
    std::vector<std::string> positional;
    bool print_toolchain_requested = false;
    bool check_toolchain_requested = false;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--dump-tokens") {
            options.dump_tokens = true;
        } else if (arg == "--dump-ast") {
            options.dump_ast = true;
        } else if (arg == "--dump-hir") {
            options.dump_hir = true;
        } else if (arg == "--dump-mir") {
            options.dump_mir = true;
        } else if (arg == "--target") {
            if (!require_option_value(index, argc, arg)) {
                return 2;
            }
            options.target_triple = std::string(argv[++index]);
        } else if (arg == "--print-toolchain") {
            print_toolchain_requested = true;
        } else if (arg == "--check-toolchain") {
            check_toolchain_requested = true;
        } else if (arg == "--package") {
            if (!require_option_value(index, argc, arg)) {
                return 2;
            }
            options.package_path = std::string(argv[++index]);
        } else if (arg == "--emit-stub") {
            if (!require_option_value(index, argc, arg)) {
                return 2;
            }
            options.emit_stub_path = std::string(argv[++index]);
        } else if (arg == "--emit-llvm") {
            if (!require_option_value(index, argc, arg)) {
                return 2;
            }
            options.emit_llvm_path = std::string(argv[++index]);
        } else if (arg == "--emit-asm") {
            if (!require_option_value(index, argc, arg)) {
                return 2;
            }
            options.emit_asm_path = std::string(argv[++index]);
        } else if (arg == "--emit-obj") {
            if (!require_option_value(index, argc, arg)) {
                return 2;
            }
            options.emit_obj_path = std::string(argv[++index]);
        } else if (arg == "--emit-exe") {
            if (!require_option_value(index, argc, arg)) {
                return 2;
            }
            options.emit_exe_path = std::string(argv[++index]);
        } else if (arg == "-h" || arg == "--help") {
            print_help(std::cout);
            return 0;
        } else if (arg == "--version") {
            std::cout << "evidc " << EVIDENT_VERSION << '\n';
            return 0;
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "unknown option: " << arg << '\n';
            print_usage(std::cerr);
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (print_toolchain_requested && check_toolchain_requested) {
        std::cerr << "only one toolchain reporting mode may be selected per invocation\n";
        print_usage(std::cerr);
        return 2;
    }

    if (print_toolchain_requested) {
        if (has_compile_action(options, positional)) {
            std::cerr << "--print-toolchain may not be combined with input files, package directories, dump modes, or emit modes\n";
            print_usage(std::cerr);
            return 2;
        }
        print_toolchain(options, std::cout);
        return 0;
    }

    if (check_toolchain_requested) {
        if (has_compile_action(options, positional)) {
            std::cerr << "--check-toolchain may not be combined with input files, package directories, dump modes, or emit modes\n";
            print_usage(std::cerr);
            return 2;
        }
        if (options.target_triple != evident::backend::supported_target_triple()) {
            std::cerr << "toolchain check failed:\n"
                      << "backend currently supports only target '" << evident::backend::supported_target_triple()
                      << "' (selected target: '" << options.target_triple << "')\n";
            return 1;
        }
        print_toolchain(options, std::cout);
        const std::expected<std::string, std::string> version = evident::backend::probe_toolchain_driver_version();
        if (!version.has_value()) {
            std::cerr << "toolchain check failed:\n" << version.error() << '\n';
            return 1;
        }
        const std::expected<std::string, std::string> linker_version = evident::backend::probe_linker_driver_version();
        if (!linker_version.has_value()) {
            std::cerr << "toolchain check failed:\n" << linker_version.error() << '\n';
            return 1;
        }
        std::cout << "clang version: " << *version << '\n';
        std::cout << "lld-link version: " << *linker_version << '\n';
        return 0;
    }

    if (positional.empty() && !options.package_path.has_value()) {
        std::cerr << "no input file or package directory provided\n";
        print_usage(std::cerr);
        return 2;
    }
    if (!positional.empty() && options.package_path.has_value()) {
        std::cerr << "--package may not be combined with explicit input files\n";
        print_usage(std::cerr);
        return 2;
    }

    int backend_emit_count = 0;
    backend_emit_count += options.emit_llvm_path.has_value() ? 1 : 0;
    backend_emit_count += options.emit_asm_path.has_value() ? 1 : 0;
    backend_emit_count += options.emit_obj_path.has_value() ? 1 : 0;
    backend_emit_count += options.emit_exe_path.has_value() ? 1 : 0;
    if (backend_emit_count > 1) {
        std::cerr << "only one native emit mode may be selected per invocation\n";
        print_usage(std::cerr);
        return 2;
    }
    if (backend_emit_count > 0 && options.emit_stub_path.has_value()) {
        std::cerr << "--emit-stub may not be combined with native emit modes\n";
        print_usage(std::cerr);
        return 2;
    }

    if (!positional.empty()) {
        options.input_path = positional.front();
    }
    options.input_paths = std::move(positional);
    return evident::run_driver(options);
}
