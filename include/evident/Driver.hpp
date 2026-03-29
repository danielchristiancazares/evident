#pragma once

#include <optional>
#include <string>

namespace evident {

struct DriverOptions {
    std::string input_path;
    bool dump_tokens = false;
    bool dump_ast = false;
    bool dump_hir = false;
    std::optional<std::string> emit_stub_path;
};

int run_driver(const DriverOptions& options);

} // namespace evident
