#pragma once

#include "evident/Hir.hpp"
#include "evident/Mir.hpp"

#include <expected>
#include <string>

namespace evident::backend {

enum class EmitKind {
    Llvm,
    Assembly,
    Object,
    Executable,
};

struct EmitOptions {
    EmitKind kind = EmitKind::Llvm;
    std::string output_path;
    std::string target_triple = "x86_64-pc-windows-msvc";
};

[[nodiscard]] std::expected<std::string, std::string> emit_llvm_ir(const hir::Package& hir_package,
                                                                   const mir::Package& mir_package,
                                                                   const std::string& target_triple,
                                                                   bool include_entry_wrapper);

[[nodiscard]] std::expected<void, std::string> emit_artifact(const hir::Package& hir_package,
                                                             const mir::Package& mir_package,
                                                             const EmitOptions& options);

[[nodiscard]] const char* emit_kind_name(EmitKind kind);

} // namespace evident::backend
