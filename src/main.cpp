#include "evident/Driver.hpp"

#include "evident/Backend.hpp"

#include <expected>
#include <iostream>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#ifndef EVIDENT_VERSION
#define EVIDENT_VERSION "0.0.0-dev"
#endif

namespace {

void print_usage(std::ostream& out) {
    out << "usage: evidc [--dump-tokens] [--dump-ast] [--dump-hir] [--dump-mir] "
           "[--target <triple>] [--print-toolchain] [--check-toolchain] [--emit-stub <path>] "
           "[--emit-llvm <path>] [--emit-asm <path>] [--emit-obj <path>] "
           "[--emit-exe <path>] [--package <dir> | <input.evd> [input2.evd ...]]\n";
}

void print_help(std::ostream& out) {
    print_usage(out);
    out << "\n"
           "Options:\n"
           "  -h, --help                 Show this help text and exit\n"
           "      --version              Show compiler version and exit\n"
           "      --dump-tokens          Print lexer tokens\n"
           "      --dump-ast             Print parsed AST\n"
           "      --dump-hir             Print lowered HIR\n"
           "      --dump-mir             Print lowered MIR\n"
           "      --target <triple>      Set native target triple (default: x86_64-pc-windows-msvc)\n"
           "      --print-toolchain      Print selected native target and toolchain settings\n"
           "      --check-toolchain      Validate target and probe clang/lld-link versions\n"
           "      --emit-stub <path>     Write stub backend output\n"
           "      --emit-llvm <path>     Write textual LLVM IR\n"
           "      --emit-asm <path>      Emit assembly through clang\n"
           "      --emit-obj <path>      Emit object through clang\n"
           "      --emit-exe <path>      Emit executable through clang and lld\n"
           "      --package <dir>        Compile package directory sources\n";
}

enum class OptionValueParseState {
    FollowingArgumentAvailable,
    MissingArgumentReported,
};

OptionValueParseState require_option_value(int index, int argc, const std::string& option) {
    if (index + 1 < argc) {
        return OptionValueParseState::FollowingArgumentAvailable;
    }
    std::cerr << "missing argument for " << option << '\n';
    print_usage(std::cerr);
    return OptionValueParseState::MissingArgumentReported;
}

enum class ToolchainReportCompatibility {
    ReportMayRunWithoutCompilation,
    CompilationWouldAlsoRun,
};

ToolchainReportCompatibility toolchain_report_compatibility(const evident::DriverOptions& options,
                                                            const std::vector<std::string>& positional) {
    if (options.dump_tokens() == evident::DumpRequest::Requested
        || options.dump_ast() == evident::DumpRequest::Requested
        || options.dump_hir() == evident::DumpRequest::Requested
        || options.dump_mir() == evident::DumpRequest::Requested
        || options.stub_emission().kind() == evident::StubEmissionKind::WriteStub
        || options.native_artifact().kind() != evident::NativeArtifactKind::Suppressed
        || options.source_request().kind() == evident::SourceRequestKind::PackageDirectory
        || !positional.empty()) {
        return ToolchainReportCompatibility::CompilationWouldAlsoRun;
    }
    return ToolchainReportCompatibility::ReportMayRunWithoutCompilation;
}

void print_toolchain(const evident::DriverOptions& options, std::ostream& out) {
    out << "native target: " << options.target_triple() << '\n'
        << "supported native target: " << evident::backend::supported_target_triple() << '\n'
        << "clang driver: " << evident::backend::selected_toolchain_driver() << '\n'
        << "clang override env: " << evident::backend::toolchain_driver_environment_variable() << '\n'
        << "linker mode: clang -fuse-ld=lld\n";
}

} // namespace

int main(int argc, char** argv) {
    evident::DriverOptions options = evident::DriverOptions::command_line_defaults();
    std::vector<std::string> positional;
    enum class ToolchainReportRequest {
        Suppressed,
        Print,
        Check,
        Conflicting,
    };
    ToolchainReportRequest toolchain_report = ToolchainReportRequest::Suppressed;

    enum class NativeEmitSelectionState {
        NotSelected,
        SingleMode,
        ConflictingModes,
    };
    NativeEmitSelectionState native_emit_selection = NativeEmitSelectionState::NotSelected;

    auto request_native_artifact = [&](evident::NativeArtifactRequest request) {
        if (options.native_artifact().kind() == evident::NativeArtifactKind::Suppressed) {
            options.use_native_artifact(std::move(request));
            native_emit_selection = NativeEmitSelectionState::SingleMode;
            return;
        }
        if (options.native_artifact().kind() != request.kind()) {
            native_emit_selection = NativeEmitSelectionState::ConflictingModes;
        }
        options.use_native_artifact(std::move(request));
    };

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--dump-tokens") {
            options.request_token_dump();
        } else if (arg == "--dump-ast") {
            options.request_ast_dump();
        } else if (arg == "--dump-hir") {
            options.request_hir_dump();
        } else if (arg == "--dump-mir") {
            options.request_mir_dump();
        } else if (arg == "--target") {
            if (require_option_value(index, argc, arg) == OptionValueParseState::MissingArgumentReported) {
                return 2;
            }
            options.use_target_triple(std::string(argv[++index]));
        } else if (arg == "--print-toolchain") {
            if (toolchain_report == ToolchainReportRequest::Check) {
                toolchain_report = ToolchainReportRequest::Conflicting;
            } else if (toolchain_report == ToolchainReportRequest::Suppressed) {
                toolchain_report = ToolchainReportRequest::Print;
            }
        } else if (arg == "--check-toolchain") {
            if (toolchain_report == ToolchainReportRequest::Print) {
                toolchain_report = ToolchainReportRequest::Conflicting;
            } else if (toolchain_report == ToolchainReportRequest::Suppressed) {
                toolchain_report = ToolchainReportRequest::Check;
            }
        } else if (arg == "--package") {
            if (require_option_value(index, argc, arg) == OptionValueParseState::MissingArgumentReported) {
                return 2;
            }
            options.use_source_request(evident::SourceRequest::package_directory(std::string(argv[++index])));
        } else if (arg == "--emit-stub") {
            if (require_option_value(index, argc, arg) == OptionValueParseState::MissingArgumentReported) {
                return 2;
            }
            options.use_stub_emission(evident::StubEmissionRequest::write_to(std::string(argv[++index])));
        } else if (arg == "--emit-llvm") {
            if (require_option_value(index, argc, arg) == OptionValueParseState::MissingArgumentReported) {
                return 2;
            }
            request_native_artifact(evident::NativeArtifactRequest::llvm_ir(std::string(argv[++index])));
        } else if (arg == "--emit-asm") {
            if (require_option_value(index, argc, arg) == OptionValueParseState::MissingArgumentReported) {
                return 2;
            }
            request_native_artifact(evident::NativeArtifactRequest::assembly(std::string(argv[++index])));
        } else if (arg == "--emit-obj") {
            if (require_option_value(index, argc, arg) == OptionValueParseState::MissingArgumentReported) {
                return 2;
            }
            request_native_artifact(evident::NativeArtifactRequest::object(std::string(argv[++index])));
        } else if (arg == "--emit-exe") {
            if (require_option_value(index, argc, arg) == OptionValueParseState::MissingArgumentReported) {
                return 2;
            }
            request_native_artifact(evident::NativeArtifactRequest::executable(std::string(argv[++index])));
        } else if (arg == "-h" || arg == "--help") {
            print_help(std::cout);
            return 0;
        } else if (arg == "--version") {
            std::cout << "evidc " << EVIDENT_VERSION << '\n';
            return 0;
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "unknown option: " << arg << '\n';
            print_usage(std::cerr);
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (toolchain_report == ToolchainReportRequest::Conflicting) {
        std::cerr << "only one toolchain reporting mode may be selected per invocation\n";
        print_usage(std::cerr);
        return 2;
    }

    if (toolchain_report == ToolchainReportRequest::Print) {
        if (toolchain_report_compatibility(options, positional)
            == ToolchainReportCompatibility::CompilationWouldAlsoRun) {
            std::cerr << "--print-toolchain may not be combined with input files, package directories, dump modes, or emit modes\n";
            print_usage(std::cerr);
            return 2;
        }
        print_toolchain(options, std::cout);
        return 0;
    }

    if (toolchain_report == ToolchainReportRequest::Check) {
        if (toolchain_report_compatibility(options, positional)
            == ToolchainReportCompatibility::CompilationWouldAlsoRun) {
            std::cerr << "--check-toolchain may not be combined with input files, package directories, dump modes, or emit modes\n";
            print_usage(std::cerr);
            return 2;
        }
        if (options.target_triple() != evident::backend::supported_target_triple()) {
            std::cerr << "toolchain check failed:\n"
                      << "backend currently supports only target '" << evident::backend::supported_target_triple()
                      << "' (selected target: '" << options.target_triple() << "')\n";
            return 1;
        }
        print_toolchain(options, std::cout);
        const std::expected<std::string, std::string> version = evident::backend::probe_toolchain_driver_version();
        if (!version.has_value()) {
            std::cerr << "toolchain check failed:\n" << version.error() << '\n';
            return 1;
        }
        const std::expected<std::string, std::string> linker_version = evident::backend::probe_linker_driver_version();
        if (!linker_version.has_value()) {
            std::cerr << "toolchain check failed:\n" << linker_version.error() << '\n';
            return 1;
        }
        std::cout << "clang version: " << *version << '\n';
        std::cout << "lld-link version: " << *linker_version << '\n';
        return 0;
    }

    if (positional.empty() && options.source_request().kind() != evident::SourceRequestKind::PackageDirectory) {
        std::cerr << "no input file or package directory provided\n";
        print_usage(std::cerr);
        return 2;
    }
    if (!positional.empty() && options.source_request().kind() == evident::SourceRequestKind::PackageDirectory) {
        std::cerr << "--package may not be combined with explicit input files\n";
        print_usage(std::cerr);
        return 2;
    }

    if (native_emit_selection == NativeEmitSelectionState::ConflictingModes) {
        std::cerr << "only one native emit mode may be selected per invocation\n";
        print_usage(std::cerr);
        return 2;
    }
    if (options.native_artifact().kind() != evident::NativeArtifactKind::Suppressed
        && options.stub_emission().kind() == evident::StubEmissionKind::WriteStub) {
        std::cerr << "--emit-stub may not be combined with native emit modes\n";
        print_usage(std::cerr);
        return 2;
    }

    if (!positional.empty()) {
        options.use_source_request(evident::SourceRequest::explicit_files(std::move(positional)));
    }
    return evident::run_driver(options);
}
