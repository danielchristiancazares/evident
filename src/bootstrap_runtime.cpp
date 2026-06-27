#include <cstdint>
#include <cstring>

namespace {

struct EvidText final {
    const char* data;
    std::int64_t size;
};

struct EvidHostPath final {
    EvidText text;
};

struct EvidCompilerEnvironment final {
    EvidHostPath workspace_root;
    EvidHostPath package_root;
    EvidHostPath manifest;
    EvidHostPath source_root;
    EvidHostPath build_root;
    EvidText target_triple;
    EvidText clang_driver;
    EvidText linker_driver;
    EvidText source_commit;
};

inline EvidText text_literal(const char* value) noexcept {
    return {value, static_cast<std::int64_t>(std::strlen(value))};
}

inline EvidHostPath host_path_literal(const char* value) noexcept {
    return {text_literal(value)};
}

} // namespace

extern "C" EvidCompilerEnvironment evid$bootstrap_environment$current_environment() {
    return {
        host_path_literal("."),
        host_path_literal("bootstrap/compiler"),
        host_path_literal("bootstrap/compiler/evident.pkg"),
        host_path_literal("bootstrap/compiler/src"),
        host_path_literal("build/windows-x64-ninja"),
        text_literal("x86_64-pc-windows-msvc"),
        text_literal("clang"),
        text_literal("lld-link"),
        text_literal("source commit recorded in release evidence"),
    };
}
