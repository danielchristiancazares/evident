#pragma once

#include <string>
#include <utility>
#include <vector>

namespace evident {

enum class DumpRequest {
    Suppressed,
    Requested,
};

enum class SourceRequestKind {
    ExplicitFiles,
    PackageDirectory,
};

class SourceRequest final {
public:
    [[nodiscard]] static SourceRequest explicit_files(std::vector<std::string> paths) {
        return SourceRequest(SourceRequestKind::ExplicitFiles, std::move(paths), "");
    }

    [[nodiscard]] static SourceRequest package_directory(std::string path) {
        return SourceRequest(SourceRequestKind::PackageDirectory, {}, std::move(path));
    }

    [[nodiscard]] SourceRequestKind kind() const noexcept { return kind_; }

    template <class ExplicitFilesHandler, class PackageDirectoryHandler>
    [[nodiscard]] decltype(auto) match(ExplicitFilesHandler&& explicit_files_handler,
                                       PackageDirectoryHandler&& package_directory_handler) const {
        if (kind_ == SourceRequestKind::ExplicitFiles) {
            return std::forward<ExplicitFilesHandler>(explicit_files_handler)(explicit_paths_);
        }
        return std::forward<PackageDirectoryHandler>(package_directory_handler)(package_path_);
    }

private:
    SourceRequestKind kind_;
    std::vector<std::string> explicit_paths_;
    std::string package_path_;

    SourceRequest(SourceRequestKind kind, std::vector<std::string> explicit_paths, std::string package_path)
        : kind_(kind),
          explicit_paths_(std::move(explicit_paths)),
          package_path_(std::move(package_path)) {}
};

enum class StubEmissionKind {
    Suppressed,
    WriteStub,
};

class StubEmissionRequest final {
public:
    [[nodiscard]] static StubEmissionRequest suppressed() {
        return StubEmissionRequest(StubEmissionKind::Suppressed, "");
    }

    [[nodiscard]] static StubEmissionRequest write_to(std::string output_path) {
        return StubEmissionRequest(StubEmissionKind::WriteStub, std::move(output_path));
    }

    [[nodiscard]] StubEmissionKind kind() const noexcept { return kind_; }

    template <class SuppressedHandler, class WriteStubHandler>
    [[nodiscard]] decltype(auto) match(SuppressedHandler&& suppressed_handler,
                                       WriteStubHandler&& write_stub_handler) const {
        if (kind_ == StubEmissionKind::Suppressed) {
            return std::forward<SuppressedHandler>(suppressed_handler)();
        }
        return std::forward<WriteStubHandler>(write_stub_handler)(output_path_);
    }

private:
    StubEmissionKind kind_;
    std::string output_path_;

    StubEmissionRequest(StubEmissionKind kind, std::string output_path)
        : kind_(kind),
          output_path_(std::move(output_path)) {}
};

enum class NativeArtifactKind {
    Suppressed,
    LlvmIr,
    Assembly,
    Object,
    Executable,
};

class NativeArtifactRequest final {
public:
    [[nodiscard]] static NativeArtifactRequest suppressed() {
        return NativeArtifactRequest(NativeArtifactKind::Suppressed, "");
    }

    [[nodiscard]] static NativeArtifactRequest llvm_ir(std::string output_path) {
        return NativeArtifactRequest(NativeArtifactKind::LlvmIr, std::move(output_path));
    }

    [[nodiscard]] static NativeArtifactRequest assembly(std::string output_path) {
        return NativeArtifactRequest(NativeArtifactKind::Assembly, std::move(output_path));
    }

    [[nodiscard]] static NativeArtifactRequest object(std::string output_path) {
        return NativeArtifactRequest(NativeArtifactKind::Object, std::move(output_path));
    }

    [[nodiscard]] static NativeArtifactRequest executable(std::string output_path) {
        return NativeArtifactRequest(NativeArtifactKind::Executable, std::move(output_path));
    }

    [[nodiscard]] NativeArtifactKind kind() const noexcept { return kind_; }

    template <class SuppressedHandler,
              class LlvmIrHandler,
              class AssemblyHandler,
              class ObjectHandler,
              class ExecutableHandler>
    [[nodiscard]] decltype(auto) match(SuppressedHandler&& suppressed_handler,
                                       LlvmIrHandler&& llvm_ir_handler,
                                       AssemblyHandler&& assembly_handler,
                                       ObjectHandler&& object_handler,
                                       ExecutableHandler&& executable_handler) const {
        switch (kind_) {
        case NativeArtifactKind::Suppressed:
            return std::forward<SuppressedHandler>(suppressed_handler)();
        case NativeArtifactKind::LlvmIr:
            return std::forward<LlvmIrHandler>(llvm_ir_handler)(output_path_);
        case NativeArtifactKind::Assembly:
            return std::forward<AssemblyHandler>(assembly_handler)(output_path_);
        case NativeArtifactKind::Object:
            return std::forward<ObjectHandler>(object_handler)(output_path_);
        case NativeArtifactKind::Executable:
            return std::forward<ExecutableHandler>(executable_handler)(output_path_);
        }
        return std::forward<SuppressedHandler>(suppressed_handler)();
    }

private:
    NativeArtifactKind kind_;
    std::string output_path_;

    NativeArtifactRequest(NativeArtifactKind kind, std::string output_path)
        : kind_(kind),
          output_path_(std::move(output_path)) {}
};

class DriverOptions final {
public:
    [[nodiscard]] static DriverOptions command_line_defaults() {
        return DriverOptions(SourceRequest::explicit_files({}),
                             "x86_64-pc-windows-msvc",
                             DumpRequest::Suppressed,
                             DumpRequest::Suppressed,
                             DumpRequest::Suppressed,
                             DumpRequest::Suppressed,
                             StubEmissionRequest::suppressed(),
                             NativeArtifactRequest::suppressed());
    }

    [[nodiscard]] const SourceRequest& source_request() const noexcept { return source_request_; }
    [[nodiscard]] const std::string& target_triple() const noexcept { return target_triple_; }
    [[nodiscard]] DumpRequest dump_tokens() const noexcept { return dump_tokens_; }
    [[nodiscard]] DumpRequest dump_ast() const noexcept { return dump_ast_; }
    [[nodiscard]] DumpRequest dump_hir() const noexcept { return dump_hir_; }
    [[nodiscard]] DumpRequest dump_mir() const noexcept { return dump_mir_; }
    [[nodiscard]] const StubEmissionRequest& stub_emission() const noexcept { return stub_emission_; }
    [[nodiscard]] const NativeArtifactRequest& native_artifact() const noexcept { return native_artifact_; }

    void use_source_request(SourceRequest request) { source_request_ = std::move(request); }
    void use_target_triple(std::string target_triple) { target_triple_ = std::move(target_triple); }
    void request_token_dump() noexcept { dump_tokens_ = DumpRequest::Requested; }
    void request_ast_dump() noexcept { dump_ast_ = DumpRequest::Requested; }
    void request_hir_dump() noexcept { dump_hir_ = DumpRequest::Requested; }
    void request_mir_dump() noexcept { dump_mir_ = DumpRequest::Requested; }
    void use_stub_emission(StubEmissionRequest request) { stub_emission_ = std::move(request); }
    void use_native_artifact(NativeArtifactRequest request) { native_artifact_ = std::move(request); }

private:
    SourceRequest source_request_;
    std::string target_triple_;
    DumpRequest dump_tokens_;
    DumpRequest dump_ast_;
    DumpRequest dump_hir_;
    DumpRequest dump_mir_;
    StubEmissionRequest stub_emission_;
    NativeArtifactRequest native_artifact_;

    DriverOptions(SourceRequest source_request,
                  std::string target_triple,
                  DumpRequest dump_tokens,
                  DumpRequest dump_ast,
                  DumpRequest dump_hir,
                  DumpRequest dump_mir,
                  StubEmissionRequest stub_emission,
                  NativeArtifactRequest native_artifact)
        : source_request_(std::move(source_request)),
          target_triple_(std::move(target_triple)),
          dump_tokens_(dump_tokens),
          dump_ast_(dump_ast),
          dump_hir_(dump_hir),
          dump_mir_(dump_mir),
          stub_emission_(std::move(stub_emission)),
          native_artifact_(std::move(native_artifact)) {}
};

int run_driver(const DriverOptions& options);

} // namespace evident
