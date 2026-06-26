#pragma once

#include "evident/Hir.hpp"
#include "evident/Mir.hpp"

#include <expected>
#include <string>
#include <string_view>

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
[[nodiscard]] std::string selected_toolchain_driver();
[[nodiscard]] std::expected<std::string, std::string> probe_toolchain_driver_version();
[[nodiscard]] std::expected<std::string, std::string> probe_linker_driver_version();
[[nodiscard]] std::string_view supported_target_triple();
[[nodiscard]] std::string_view toolchain_driver_environment_variable();

} // namespace evident::backend
