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
    [[nodiscard]] const std::vector<std::string>& explicit_paths() const noexcept { return explicit_paths_; }
    [[nodiscard]] const std::string& package_path() const noexcept { return package_path_; }

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
    [[nodiscard]] const std::string& output_path() const noexcept { return output_path_; }

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
    [[nodiscard]] const std::string& output_path() const noexcept { return output_path_; }

private:
    NativeArtifactKind kind_;
    std::string output_path_;

    NativeArtifactRequest(NativeArtifactKind kind, std::string output_path)
        : kind_(kind),
          output_path_(std::move(output_path)) {}
};

struct DriverOptions {
    SourceRequest source_request = SourceRequest::explicit_files({});
    std::string target_triple = "x86_64-pc-windows-msvc";
    DumpRequest dump_tokens = DumpRequest::Suppressed;
    DumpRequest dump_ast = DumpRequest::Suppressed;
    DumpRequest dump_hir = DumpRequest::Suppressed;
    DumpRequest dump_mir = DumpRequest::Suppressed;
    StubEmissionRequest stub_emission = StubEmissionRequest::suppressed();
    NativeArtifactRequest native_artifact = NativeArtifactRequest::suppressed();
};

int run_driver(const DriverOptions& options);

} // namespace evident
