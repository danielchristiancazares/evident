#include "evident/Driver.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cerr << "usage: evidc [--dump-tokens] [--dump-ast] [--dump-hir] [--dump-mir] "
                 "[--target <triple>] [--emit-stub <path>] [--emit-llvm <path>] "
                 "[--emit-asm <path>] [--emit-obj <path>] [--emit-exe <path>] <input.evd>\n";
}

} // namespace

int main(int argc, char** argv) {
    evident::DriverOptions options;
    std::vector<std::string> positional;

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
            if (index + 1 >= argc) {
                print_usage();
                return 2;
            }
            options.target_triple = std::string(argv[++index]);
        } else if (arg == "--emit-stub") {
            if (index + 1 >= argc) {
                print_usage();
                return 2;
            }
            options.emit_stub_path = std::string(argv[++index]);
        } else if (arg == "--emit-llvm") {
            if (index + 1 >= argc) {
                print_usage();
                return 2;
            }
            options.emit_llvm_path = std::string(argv[++index]);
        } else if (arg == "--emit-asm") {
            if (index + 1 >= argc) {
                print_usage();
                return 2;
            }
            options.emit_asm_path = std::string(argv[++index]);
        } else if (arg == "--emit-obj") {
            if (index + 1 >= argc) {
                print_usage();
                return 2;
            }
            options.emit_obj_path = std::string(argv[++index]);
        } else if (arg == "--emit-exe") {
            if (index + 1 >= argc) {
                print_usage();
                return 2;
            }
            options.emit_exe_path = std::string(argv[++index]);
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "unknown option: " << arg << '\n';
            print_usage();
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() != 1) {
        print_usage();
        return 2;
    }

    int backend_emit_count = 0;
    backend_emit_count += options.emit_llvm_path.has_value() ? 1 : 0;
    backend_emit_count += options.emit_asm_path.has_value() ? 1 : 0;
    backend_emit_count += options.emit_obj_path.has_value() ? 1 : 0;
    backend_emit_count += options.emit_exe_path.has_value() ? 1 : 0;
    if (backend_emit_count > 1) {
        std::cerr << "only one native emit mode may be selected per invocation\n";
        print_usage();
        return 2;
    }
    if (backend_emit_count > 0 && options.emit_stub_path.has_value()) {
        std::cerr << "--emit-stub may not be combined with native emit modes\n";
        print_usage();
        return 2;
    }

    options.input_path = positional.front();
    return evident::run_driver(options);
}
