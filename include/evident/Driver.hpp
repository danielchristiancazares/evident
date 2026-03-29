#pragma once

#include <optional>
#include <string>

namespace evident {

struct DriverOptions {
    std::string input_path;
    std::string target_triple = "x86_64-pc-windows-msvc";
    bool dump_tokens = false;
    bool dump_ast = false;
    bool dump_hir = false;
    bool dump_mir = false;
    std::optional<std::string> emit_stub_path;
    std::optional<std::string> emit_llvm_path;
    std::optional<std::string> emit_asm_path;
    std::optional<std::string> emit_obj_path;
    std::optional<std::string> emit_exe_path;
};

int run_driver(const DriverOptions& options);

} // namespace evident
