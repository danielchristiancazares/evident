#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

struct EvidText final {
    const char* data;
    std::int64_t size;
};

struct EvidTextList final {
    const EvidText* data;
    std::int64_t size;
};

struct EvidHostPath final {
    const char* text;
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

std::deque<std::string> retained_texts;

inline EvidText text_literal(const char* value) noexcept {
    return {value, static_cast<std::int64_t>(std::strlen(value))};
}

EvidText retain_text(std::string text) {
    retained_texts.push_back(std::move(text));
    const std::string& stored = retained_texts.back();
    return {stored.c_str(), static_cast<std::int64_t>(stored.size())};
}

const char* retain_cstring(std::string text) {
    retained_texts.push_back(std::move(text));
    const std::string& stored = retained_texts.back();
    return stored.c_str();
}

std::string text_to_string(EvidText text) {
    if (text.data == nullptr || text.size <= 0) {
        return {};
    }
    return std::string(text.data, text.data + text.size);
}

std::string cstring_to_string(const char* text) {
    if (text == nullptr) {
        return {};
    }
    return std::string(text);
}

std::filesystem::path path_from_cstring(const char* text) {
    return std::filesystem::u8path(cstring_to_string(text));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    return path.generic_string();
}

EvidHostPath host_path_from_path(const std::filesystem::path& path) {
    return {retain_cstring(path_to_utf8(path))};
}

void report_runtime_error(std::string_view message) {
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

#ifdef _WIN32

std::wstring widen_utf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::wstring quote_windows_arg(std::string_view arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    if (arg.find_first_of(" \t\n\v\"") == std::string_view::npos) {
        return widen_utf8(arg);
    }

    std::wstring out;
    out.push_back(L'"');
    std::size_t backslashes = 0;
    for (char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

#endif

} // namespace

extern "C" EvidCompilerEnvironment evid$bootstrap_environment$current_environment() {
    return {
        host_path_from_path(std::filesystem::path(".")),
        host_path_from_path(std::filesystem::path("bootstrap/compiler")),
        host_path_from_path(std::filesystem::path("bootstrap/compiler/evident.pkg")),
        host_path_from_path(std::filesystem::path("bootstrap/compiler/src")),
        host_path_from_path(std::filesystem::path("build/windows-x64-ninja")),
        text_literal("x86_64-pc-windows-msvc"),
        text_literal("clang"),
        text_literal("lld-link"),
        text_literal("source commit recorded in release evidence"),
    };
}

extern "C" const char* evid$bootstrap_host$resolve_path_text(const char* base, const char* relative) {
    std::filesystem::path resolved = path_from_cstring(base);
    resolved /= path_from_cstring(relative);
    resolved = resolved.lexically_normal();
    return retain_cstring(path_to_utf8(resolved));
}

extern "C" EvidText evid$bootstrap_host$read_text_file_text(const char* path_text) {
    const std::filesystem::path path = path_from_cstring(path_text);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        report_runtime_error("bootstrap host runtime failed to open text file '" + path_to_utf8(path) + "'");
        std::abort();
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return retain_text(buffer.str());
}

extern "C" std::int32_t evid$bootstrap_host$write_text_file_text(const char* path_text, EvidText contents_text) {
    const std::filesystem::path path = path_from_cstring(path_text);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) {
            report_runtime_error("bootstrap host runtime failed to create parent directories for '"
                                 + path_to_utf8(path) + "': " + directory_error.message());
            return 1;
        }
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        report_runtime_error("bootstrap host runtime failed to open text output file '" + path_to_utf8(path) + "'");
        return 1;
    }

    const std::string contents = text_to_string(contents_text);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        report_runtime_error("bootstrap host runtime failed to write text output file '" + path_to_utf8(path) + "'");
        return 1;
    }

    return 0;
}

extern "C" std::int32_t evid$bootstrap_host$spawn_tool_text(const char* working_directory_text,
                                                             const char* executable_text,
                                                             EvidTextList arguments) {
#ifdef _WIN32
    const std::wstring executable_wide = widen_utf8(cstring_to_string(executable_text));
    std::wstring command_line;
    for (std::int64_t index = 0; index < arguments.size; ++index) {
        if (index > 0) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_arg(text_to_string(arguments.data[index]));
    }

    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    const std::wstring working_directory_wide = widen_utf8(cstring_to_string(working_directory_text));

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessW(executable_wide.c_str(),
                                        mutable_command.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        0,
                                        nullptr,
                                        working_directory_wide.empty() ? nullptr : working_directory_wide.c_str(),
                                        &startup_info,
                                        &process_info);
    if (!created) {
        const DWORD error_code = GetLastError();
        report_runtime_error("bootstrap host runtime failed to launch tool '"
                             + cstring_to_string(executable_text)
                             + "' (Windows error " + std::to_string(error_code) + ")");
        return static_cast<std::int32_t>(error_code != 0 ? error_code : 1);
    }

    const DWORD wait_result = WaitForSingleObject(process_info.hProcess, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        report_runtime_error("bootstrap host runtime failed while waiting for tool '"
                             + cstring_to_string(executable_text) + "'");
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 1;
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        report_runtime_error("bootstrap host runtime failed to read exit code for tool '"
                             + cstring_to_string(executable_text) + "'");
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return 1;
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return static_cast<std::int32_t>(exit_code);
#else
    (void)working_directory_text;
    (void)executable_text;
    (void)arguments;
    return 0;
#endif
}

extern "C" std::int32_t evid$bootstrap_host$process_exit(std::int32_t code) {
    return code;
}
