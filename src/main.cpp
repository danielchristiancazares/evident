#include "evident/Driver.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cerr << "usage: evidc [--dump-tokens] [--dump-ast] [--dump-hir] [--emit-stub <path>] <input.evd>\n";
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
        } else if (arg == "--emit-stub") {
            if (index + 1 >= argc) {
                print_usage();
                return 2;
            }
            options.emit_stub_path = std::string(argv[++index]);
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

    options.input_path = positional.front();
    return evident::run_driver(options);
}
