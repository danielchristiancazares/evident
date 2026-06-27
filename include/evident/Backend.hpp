#pragma once

#include "evident/Hir.hpp"
#include "evident/Mir.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace evident::backend {

enum class EmitKind {
    Llvm,
    Assembly,
    Object,
    Executable,
};

enum class EntryPointEmission {
    UserFunctionsOnly,
    IncludeExecutableEntryPoint,
};

class EmitOptions final {
public:
    [[nodiscard]] static EmitOptions requested_artifact(EmitKind kind,
                                                        std::string output_path,
                                                        std::string target_triple) {
        return EmitOptions(kind, std::move(output_path), std::move(target_triple));
    }

    [[nodiscard]] EmitKind kind() const noexcept { return kind_; }
    [[nodiscard]] const std::string& output_path() const noexcept { return output_path_; }
    [[nodiscard]] const std::string& target_triple() const noexcept { return target_triple_; }

private:
    EmitKind kind_;
    std::string output_path_;
    std::string target_triple_;

    EmitOptions(EmitKind kind, std::string output_path, std::string target_triple)
        : kind_(kind),
          output_path_(std::move(output_path)),
          target_triple_(std::move(target_triple)) {}
};

struct ArtifactEmissionSucceeded final {};

[[nodiscard]] std::expected<std::string, std::string> emit_llvm_ir(const hir::Package& hir_package,
                                                                   const mir::Package& mir_package,
                                                                   const std::string& target_triple,
                                                                   EntryPointEmission entry_point_emission);

[[nodiscard]] std::expected<ArtifactEmissionSucceeded, std::string> emit_artifact(const hir::Package& hir_package,
                                                                                  const mir::Package& mir_package,
                                                                                  const EmitOptions& options);

[[nodiscard]] const char* emit_kind_name(EmitKind kind);
[[nodiscard]] std::string selected_toolchain_driver();
[[nodiscard]] std::expected<std::string, std::string> probe_toolchain_driver_version();
[[nodiscard]] std::expected<std::string, std::string> probe_linker_driver_version();
[[nodiscard]] std::string_view supported_target_triple();
[[nodiscard]] std::string_view toolchain_driver_environment_variable();

} // namespace evident::backend
