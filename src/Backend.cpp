#include "evident/Backend.hpp"

#include <array>
#include <bit>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef _WIN32
extern char** environ;
#endif

#ifndef EVIDENT_NATIVE_RUNTIME_LIBRARY
#define EVIDENT_NATIVE_RUNTIME_LIBRARY ""
#endif

namespace evident::backend {

namespace {

constexpr const char* kSupportedTarget = "x86_64-pc-windows-msvc";
constexpr const char* kClangEnvVar = "EVIDENT_CLANG";
constexpr const char* kLinkerDriver = "lld-link";

struct TextFileWriteSucceeded final {};

enum class ArgumentQuotingState {
    CanPassUnquoted,
    MustQuote,
};

enum class CompileTimeArgumentEncoding {
    ErasedToUnitOperand,
    CarriesRuntimeOperand,
};

CompileTimeArgumentEncoding compile_time_argument_encoding_of(const mir::Operand& operand) {
    return operand.match(
        [](mir::Operand::LocalValue) {
            return CompileTimeArgumentEncoding::CarriesRuntimeOperand;
        },
        [](mir::Operand::IntLiteralValue) {
            return CompileTimeArgumentEncoding::CarriesRuntimeOperand;
        },
        [](mir::Operand::StringLiteralValue) {
            return CompileTimeArgumentEncoding::CarriesRuntimeOperand;
        },
        [](mir::Operand::UnitValue) {
            return CompileTimeArgumentEncoding::ErasedToUnitOperand;
        });
}

std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

std::size_t element_storage_stride_for(std::size_t size, std::size_t align) {
    return std::max<std::size_t>(align_up(size, align), 1);
}

std::expected<TextFileWriteSucceeded, std::string> write_text_file(const std::string& path,
                                                                   const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return std::unexpected("failed to open text output file");
    }
    out << text;
    if (!out) {
        return std::unexpected("failed to write text output file");
    }
    return TextFileWriteSucceeded{};
}

enum class ToolchainOverrideText {
    ContainsOnlyWhitespace,
    ContainsDriverPath,
};

ToolchainOverrideText inspect_toolchain_override_text(std::string_view text) {
    for (char ch : text) {
        switch (ch) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case '\f':
        case '\v':
            break;
        default:
            return ToolchainOverrideText::ContainsDriverPath;
        }
    }
    return ToolchainOverrideText::ContainsOnlyWhitespace;
}

std::expected<std::string, std::string> toolchain_driver() {
    const char* override_path = std::getenv(kClangEnvVar);
    if (override_path != nullptr && override_path[0] != '\0') {
        const std::string_view override_text = override_path;
        if (inspect_toolchain_override_text(override_text)
            == ToolchainOverrideText::ContainsOnlyWhitespace) {
            return std::unexpected(std::string("invalid ")
                                   + kClangEnvVar
                                   + " override: set it to a non-whitespace clang executable path, "
                                     "or unset it to use clang from PATH");
        }
        return std::string(override_text);
    }
    return "clang";
}

unsigned long current_process_id() {
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

std::expected<std::filesystem::path, std::string> make_toolchain_probe_log_path() {
    std::error_code error;
    const std::filesystem::path temp_directory = std::filesystem::temp_directory_path(error);
    if (error) {
        return std::unexpected("failed to find temporary directory for toolchain probe: " + error.message());
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return temp_directory / ("evident-toolchain-probe-"
                             + std::to_string(current_process_id())
                             + "-"
                             + std::to_string(stamp)
                             + ".log");
}

std::string first_nonempty_line(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && (text[start] == '\r' || text[start] == '\n')) {
            ++start;
        }
        if (start >= text.size()) {
            break;
        }

        const std::size_t end = text.find_first_of("\r\n", start);
        const std::string_view line = end == std::string_view::npos
            ? text.substr(start)
            : text.substr(start, end - start);
        if (!line.empty()) {
            return std::string(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

std::string_view type_base_name(std::string_view name) {
    const std::size_t generic_start = name.find('<');
    return generic_start == std::string_view::npos ? name : name.substr(0, generic_start);
}

std::string trim_ascii_whitespace(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

std::expected<std::vector<std::string>, std::string> generic_type_arguments(std::string_view name) {
    const std::size_t generic_start = name.find('<');
    if (generic_start == std::string_view::npos || name.empty() || name.back() != '>') {
        return std::unexpected("backend expected generic type arguments in '" + std::string(name) + "'");
    }

    std::vector<std::string> args;
    const std::string_view args_text = name.substr(generic_start + 1, name.size() - generic_start - 2);
    std::size_t arg_start = 0;
    std::size_t nested_depth = 0;

    for (std::size_t index = 0; index < args_text.size(); ++index) {
        const char ch = args_text[index];
        if (ch == '<') {
            ++nested_depth;
            continue;
        }
        if (ch == '>') {
            if (nested_depth == 0) {
                return std::unexpected("backend found unmatched '>' in type '" + std::string(name) + "'");
            }
            --nested_depth;
            continue;
        }
        if (ch != ',' || nested_depth != 0) {
            continue;
        }

        std::string argument = trim_ascii_whitespace(args_text.substr(arg_start, index - arg_start));
        if (argument.empty()) {
            return std::unexpected("backend found an empty type argument in '" + std::string(name) + "'");
        }
        args.push_back(std::move(argument));
        arg_start = index + 1;
    }

    if (nested_depth != 0) {
        return std::unexpected("backend found unterminated nested type arguments in '" + std::string(name) + "'");
    }

    std::string argument = trim_ascii_whitespace(args_text.substr(arg_start));
    if (argument.empty()) {
        return std::unexpected("backend found an empty type argument in '" + std::string(name) + "'");
    }
    args.push_back(std::move(argument));
    return args;
}

enum class CompilerOwnedCompanionTypeMembership {
    OtherType,
    CompanionType,
};

CompilerOwnedCompanionTypeMembership compiler_owned_companion_type_membership(std::string_view name) {
    const std::string_view base_name = type_base_name(name);
    if (base_name == "ListFirstAndRest" || base_name == "MapEntry"
        || base_name == "MapFirstEntryAndRest" || base_name == "MapBoundValueAndRest") {
        return CompilerOwnedCompanionTypeMembership::CompanionType;
    }
    return CompilerOwnedCompanionTypeMembership::OtherType;
}

std::string_view function_base_name(std::string_view qualified_name) {
    const std::size_t specialization_start = qualified_name.find('$');
    return specialization_start == std::string_view::npos
        ? qualified_name
        : qualified_name.substr(0, specialization_start);
}

typesys::DisciplineMaterialization materialization_of(typesys::UseDiscipline discipline) {
    return typesys::discipline_materialization(discipline);
}

std::string sanitize_llvm_name(std::string_view text, char scope_separator) {
    std::string out;
    out.reserve(text.size() + 16);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == ':' && index + 1 < text.size() && text[index + 1] == ':') {
            out.push_back(scope_separator);
            ++index;
            continue;
        }
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (std::isalnum(ch) || ch == '_' || ch == '$' || ch == '.') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('_');
        }
    }
    return out;
}

std::string mangle_symbol_name(std::string_view qualified_name) {
    return "evid$" + sanitize_llvm_name(qualified_name, '$');
}

std::string mangle_type_name(std::string_view qualified_name) {
    return "%evid.type." + sanitize_llvm_name(qualified_name, '.');
}

std::string mangle_variant_payload_name(std::string_view qualified_name) {
    return "%evid.payload." + sanitize_llvm_name(qualified_name, '.');
}

std::string mangle_yield_name(std::string_view qualified_name) {
    return "%evid.yield." + sanitize_llvm_name(qualified_name, '.');
}

bool is_hex_digit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

unsigned hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10U + static_cast<unsigned>(ch - 'a');
    }
    return 10U + static_cast<unsigned>(ch - 'A');
}

bool is_surrogate_code_point(unsigned value) {
    return value >= 0xD800U && value <= 0xDFFFU;
}

void append_utf8_scalar(std::string& out, unsigned value) {
    if (value <= 0x7FU) {
        out.push_back(static_cast<char>(value));
        return;
    }
    if (value <= 0x7FFU) {
        out.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        return;
    }
    if (value <= 0xFFFFU) {
        out.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        return;
    }
    out.push_back(static_cast<char>(0xF0U | (value >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
}

std::expected<std::string, std::string> decode_string_literal(const std::string& lexeme) {
    if (lexeme.size() < 2 || lexeme.front() != '"' || lexeme.back() != '"') {
        return std::unexpected("invalid string literal lexeme '" + lexeme + "'");
    }

    std::string decoded;
    decoded.reserve(lexeme.size() - 2);
    for (std::size_t index = 1; index + 1 < lexeme.size(); ++index) {
        char ch = lexeme[index];
        if (ch != '\\') {
            decoded.push_back(ch);
            continue;
        }
        if (index + 1 >= lexeme.size() - 1) {
            return std::unexpected("unterminated escape in string literal '" + lexeme + "'");
        }
        const char escaped = lexeme[++index];
        switch (escaped) {
        case '\\':
            decoded.push_back('\\');
            break;
        case '"':
            decoded.push_back('"');
            break;
        case 'n':
            decoded.push_back('\n');
            break;
        case 'r':
            decoded.push_back('\r');
            break;
        case 't':
            decoded.push_back('\t');
            break;
        case '0':
            decoded.push_back('\0');
            break;
        case 'u': {
            if (index + 1 >= lexeme.size() - 1 || lexeme[index + 1] != '{') {
                return std::unexpected("invalid Unicode escape in string literal '" + lexeme + "'");
            }
            index += 2;
            unsigned value = 0;
            std::size_t digit_count = 0;
            bool too_many_digits = false;
            while (index < lexeme.size() - 1 && is_hex_digit(lexeme[index])) {
                if (digit_count < 6) {
                    value = value * 16U + hex_digit_value(lexeme[index]);
                } else {
                    too_many_digits = true;
                }
                ++digit_count;
                ++index;
            }
            if (digit_count == 0 || too_many_digits || index >= lexeme.size() - 1 || lexeme[index] != '}') {
                return std::unexpected("invalid Unicode escape in string literal '" + lexeme + "'");
            }
            if (value > 0x10FFFFU || is_surrogate_code_point(value)) {
                return std::unexpected("Unicode escape does not name a Unicode scalar value in string literal '"
                                       + lexeme + "'");
            }
            append_utf8_scalar(decoded, value);
            break;
        }
        default:
            return std::unexpected("invalid string escape in literal '" + lexeme + "'");
        }
    }
    return decoded;
}

std::string hex_byte(unsigned char value) {
    constexpr std::array<char, 16> kHex = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    };
    std::string out;
    out.push_back(kHex[(value >> 4U) & 0x0FU]);
    out.push_back(kHex[value & 0x0FU]);
    return out;
}

std::string encode_llvm_bytes(const std::string& bytes) {
    std::string out;
    out.reserve(bytes.size() * 3 + 3);
    for (unsigned char byte : bytes) {
        out.push_back('\\');
        out += hex_byte(byte);
    }
    out += "\\00";
    return out;
}

std::expected<std::string, std::string> make_storage_type(std::size_t size, std::size_t alignment) {
    if (size == 0) {
        return std::string("[0 x i8]");
    }
    switch (alignment) {
    case 1:
        if (size == 1) {
            return std::string("i8");
        }
        return "[" + std::to_string(size) + " x i8]";
    case 2:
        if (size == 2) {
            return std::string("i16");
        }
        return "{ i16, [" + std::to_string(size - 2) + " x i8] }";
    case 4:
        if (size == 4) {
            return std::string("i32");
        }
        return "{ i32, [" + std::to_string(size - 4) + " x i8] }";
    case 8:
        if (size == 8) {
            return std::string("i64");
        }
        return "{ i64, [" + std::to_string(size - 8) + " x i8] }";
    default:
        return std::unexpected("backend only supports payload alignment up to 8 bytes, got "
                               + std::to_string(alignment));
    }
}

enum class BuiltinKind {
    Int,
    Nat,
    Float,
    Char,
    Byte,
    CInt,
    CSize,
    CString,
    Text,
    NonEmptyText,
    Bytes,
    NonEmptyBytes,
    List,
    NonEmptyList,
    Map,
    NonEmptyMap,
    Unit,
    Never,
};

enum class ResolvedTypeCategory {
    BuiltinType,
    PackageTypeDeclaration,
    BackendAggregateStorage,
};

enum class CompilerOwnedFunctionLowering {
    Unsupported,
    EmptyCollection,
    WidenCollection,
    CountCollection,
    SequenceLength,
    TextCharacterAt,
    BytesByteAt,
    TextSlice,
    BytesSlice,
    RequireNonEmptySequence,
    WidenSequence,
    SequenceFirst,
    RequireNonEmptyCollection,
    ListSingle,
    MapSingle,
    ListFirstCopy,
    MapFirstEntryCopy,
    MapLookupCopy,
    MapBindNew,
    MapReplaceBound,
    MapBindOrReplace,
    MapRemoveBound,
    MapConsumeBoundValue,
    MapMerge,
    MapFromEntries,
    ListConsumeFirst,
    MapConsumeFirstEntry,
    MapEntries,
    ListPrepend,
    ListAppend,
    ListConcat,
    ForeignIntegerConversion,
    ForeignTextConversion,
};

enum class RequireNonEmptyCollectionFamily {
    List,
    Map,
};

enum class MapBindingOperation {
    BindNew,
    ReplaceBound,
    BindOrReplace,
};

enum class MapMergeCollisionPolicy {
    RejectSharedKeys,
    UseLeftBinding,
    UseRightBinding,
};

enum class MapFromEntriesDuplicatePolicy {
    RejectSharedKeys,
    UseFirstBinding,
    UseLastBinding,
};

enum class ForeignArgumentPassing {
    Direct,
    Pointer,
};

CompilerOwnedFunctionLowering compiler_owned_function_lowering(std::string_view qualified_name) {
    const std::string_view base_name = function_base_name(qualified_name);
    if (base_name == "list_empty" || base_name == "map_empty") {
        return CompilerOwnedFunctionLowering::EmptyCollection;
    }
    if (base_name == "nonempty_list_widen" || base_name == "nonempty_map_widen") {
        return CompilerOwnedFunctionLowering::WidenCollection;
    }
    if (base_name == "list_count_copy" || base_name == "nonempty_list_count_copy"
        || base_name == "map_count_copy" || base_name == "nonempty_map_count_copy") {
        return CompilerOwnedFunctionLowering::CountCollection;
    }
    if (base_name == "text_length" || base_name == "bytes_length"
        || base_name == "nonempty_text_length" || base_name == "nonempty_bytes_length") {
        return CompilerOwnedFunctionLowering::SequenceLength;
    }
    if (base_name == "text_character_at") {
        return CompilerOwnedFunctionLowering::TextCharacterAt;
    }
    if (base_name == "bytes_byte_at") {
        return CompilerOwnedFunctionLowering::BytesByteAt;
    }
    if (base_name == "text_slice") {
        return CompilerOwnedFunctionLowering::TextSlice;
    }
    if (base_name == "bytes_slice") {
        return CompilerOwnedFunctionLowering::BytesSlice;
    }
    if (base_name == "text_require_nonempty" || base_name == "bytes_require_nonempty") {
        return CompilerOwnedFunctionLowering::RequireNonEmptySequence;
    }
    if (base_name == "nonempty_text_widen" || base_name == "nonempty_bytes_widen") {
        return CompilerOwnedFunctionLowering::WidenSequence;
    }
    if (base_name == "nonempty_text_first_character" || base_name == "nonempty_bytes_first_byte") {
        return CompilerOwnedFunctionLowering::SequenceFirst;
    }
    if (base_name == "list_require_nonempty" || base_name == "map_require_nonempty") {
        return CompilerOwnedFunctionLowering::RequireNonEmptyCollection;
    }
    if (base_name == "list_single") {
        return CompilerOwnedFunctionLowering::ListSingle;
    }
    if (base_name == "map_single") {
        return CompilerOwnedFunctionLowering::MapSingle;
    }
    if (base_name == "nonempty_list_first_copy") {
        return CompilerOwnedFunctionLowering::ListFirstCopy;
    }
    if (base_name == "nonempty_map_first_entry_copy") {
        return CompilerOwnedFunctionLowering::MapFirstEntryCopy;
    }
    if (base_name == "map_lookup_copy" || base_name == "nonempty_map_lookup_copy") {
        return CompilerOwnedFunctionLowering::MapLookupCopy;
    }
    if (base_name == "map_bind_new" || base_name == "nonempty_map_bind_new") {
        return CompilerOwnedFunctionLowering::MapBindNew;
    }
    if (base_name == "map_replace_bound" || base_name == "nonempty_map_replace_bound") {
        return CompilerOwnedFunctionLowering::MapReplaceBound;
    }
    if (base_name == "map_bind_or_replace" || base_name == "nonempty_map_bind_or_replace") {
        return CompilerOwnedFunctionLowering::MapBindOrReplace;
    }
    if (base_name == "map_remove_bound" || base_name == "nonempty_map_remove_bound") {
        return CompilerOwnedFunctionLowering::MapRemoveBound;
    }
    if (base_name == "map_consume_bound_value" || base_name == "nonempty_map_consume_bound_value") {
        return CompilerOwnedFunctionLowering::MapConsumeBoundValue;
    }
    if (base_name == "map_merge_rejecting_shared_keys"
        || base_name == "map_merge_left_nonempty_rejecting_shared_keys"
        || base_name == "map_merge_right_nonempty_rejecting_shared_keys"
        || base_name == "nonempty_map_merge_rejecting_shared_keys"
        || base_name == "map_merge_using_left_bindings_for_shared_keys"
        || base_name == "map_merge_left_nonempty_using_left_bindings_for_shared_keys"
        || base_name == "map_merge_right_nonempty_using_left_bindings_for_shared_keys"
        || base_name == "nonempty_map_merge_using_left_bindings_for_shared_keys"
        || base_name == "map_merge_using_right_bindings_for_shared_keys"
        || base_name == "map_merge_left_nonempty_using_right_bindings_for_shared_keys"
        || base_name == "map_merge_right_nonempty_using_right_bindings_for_shared_keys"
        || base_name == "nonempty_map_merge_using_right_bindings_for_shared_keys") {
        return CompilerOwnedFunctionLowering::MapMerge;
    }
    if (base_name == "map_from_entries_rejecting_shared_keys"
        || base_name == "nonempty_map_from_entries_rejecting_shared_keys"
        || base_name == "map_from_entries_using_first_bindings"
        || base_name == "nonempty_map_from_entries_using_first_bindings"
        || base_name == "map_from_entries_using_last_bindings"
        || base_name == "nonempty_map_from_entries_using_last_bindings") {
        return CompilerOwnedFunctionLowering::MapFromEntries;
    }
    if (base_name == "nonempty_list_consume_first") {
        return CompilerOwnedFunctionLowering::ListConsumeFirst;
    }
    if (base_name == "nonempty_map_consume_first_entry") {
        return CompilerOwnedFunctionLowering::MapConsumeFirstEntry;
    }
    if (base_name == "map_entries_copy" || base_name == "nonempty_map_entries_copy"
        || base_name == "map_consume_entries" || base_name == "nonempty_map_consume_entries") {
        return CompilerOwnedFunctionLowering::MapEntries;
    }
    if (base_name == "list_prepend") {
        return CompilerOwnedFunctionLowering::ListPrepend;
    }
    if (base_name == "list_append") {
        return CompilerOwnedFunctionLowering::ListAppend;
    }
    if (base_name == "list_concat" || base_name == "nonempty_list_concat_left"
        || base_name == "nonempty_list_concat_right" || base_name == "nonempty_list_concat") {
        return CompilerOwnedFunctionLowering::ListConcat;
    }
    if (base_name == "cint_to_int" || base_name == "cint_require_nat"
        || base_name == "csize_to_int" || base_name == "csize_to_nat"
        || base_name == "int_to_cint" || base_name == "nat_to_cint"
        || base_name == "int_to_csize" || base_name == "nat_to_csize") {
        return CompilerOwnedFunctionLowering::ForeignIntegerConversion;
    }
    if (base_name == "cstring_payload_bytes" || base_name == "cstring_payload_text"
        || base_name == "text_to_cstring" || base_name == "bytes_to_cstring") {
        return CompilerOwnedFunctionLowering::ForeignTextConversion;
    }
    return CompilerOwnedFunctionLowering::Unsupported;
}

class ResolvedTypeIdentity final {
public:
    [[nodiscard]] static ResolvedTypeIdentity builtin_type(BuiltinKind kind) {
        return ResolvedTypeIdentity(ResolvedTypeCategory::BuiltinType, hir::TypeId{}, kind);
    }

    [[nodiscard]] static ResolvedTypeIdentity package_type(hir::TypeId type_id) {
        return ResolvedTypeIdentity(ResolvedTypeCategory::PackageTypeDeclaration, type_id, BuiltinKind{});
    }

    [[nodiscard]] static ResolvedTypeIdentity backend_aggregate_storage() {
        return ResolvedTypeIdentity(ResolvedTypeCategory::BackendAggregateStorage, hir::TypeId{}, BuiltinKind{});
    }

    [[nodiscard]] ResolvedTypeCategory category() const noexcept { return category_; }
    [[nodiscard]] hir::TypeId package_type_id() const noexcept { return package_type_id_; }
    [[nodiscard]] BuiltinKind builtin_kind() const noexcept { return builtin_kind_; }

private:
    ResolvedTypeCategory category_;
    hir::TypeId package_type_id_;
    BuiltinKind builtin_kind_;

    ResolvedTypeIdentity(ResolvedTypeCategory category, hir::TypeId package_type_id, BuiltinKind builtin_kind)
        : category_(category), package_type_id_(package_type_id), builtin_kind_(builtin_kind) {}
};

enum class RuntimeStorageShape {
    Aggregate,
    Scalar,
};

enum class MaterializationState {
    RuntimeValue,
    NeverValue,
};

enum class PayloadStorageState {
    TagOnly,
    CarriesPayload,
};

class FieldLayout final {
public:
    [[nodiscard]] static FieldLayout resolved_field(std::string name,
                                                    std::string source_name,
                                                    std::string llvm_type,
                                                    std::size_t index,
                                                    std::size_t size,
                                                    std::size_t align,
                                                    RuntimeStorageShape storage_shape,
                                                    MaterializationState materialization,
                                                    ResolvedTypeIdentity identity);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& source_name() const noexcept { return source_name_; }
    [[nodiscard]] const std::string& llvm_type() const noexcept { return llvm_type_; }
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t align() const noexcept { return align_; }
    [[nodiscard]] RuntimeStorageShape storage_shape() const noexcept { return storage_shape_; }
    [[nodiscard]] MaterializationState materialization() const noexcept { return materialization_; }
    [[nodiscard]] ResolvedTypeIdentity identity() const noexcept { return identity_; }

private:
    std::string name_;
    std::string source_name_;
    std::string llvm_type_;
    std::size_t index_;
    std::size_t size_;
    std::size_t align_;
    RuntimeStorageShape storage_shape_;
    MaterializationState materialization_;
    ResolvedTypeIdentity identity_;

    FieldLayout(std::string name,
                std::string source_name,
                std::string llvm_type,
                std::size_t index,
                std::size_t size,
                std::size_t align,
                RuntimeStorageShape storage_shape,
                MaterializationState materialization,
                ResolvedTypeIdentity identity);
};

struct VariantLayout {
    hir::VariantId id = 0;
    std::string payload_llvm_type;
    std::string payload_definition;
    std::vector<FieldLayout> fields;
    std::unordered_map<std::string, std::size_t> field_indices;
    std::size_t payload_size = 0;
    std::size_t payload_align = 1;
    std::uint32_t tag_value = 0;
};

struct TypeLayout {
    std::string source_name;
    std::string llvm_type;
    std::string definition;
    hir::TypeKind kind = hir::TypeKind::Record;
    std::vector<FieldLayout> fields;
    std::unordered_map<std::string, std::size_t> field_indices;
    std::unordered_map<hir::VariantId, VariantLayout> variants;
    std::size_t size = 0;
    std::size_t align = 1;
    PayloadStorageState payload_state = PayloadStorageState::TagOnly;
    std::string payload_storage_type;
    std::size_t payload_size = 0;
    std::size_t payload_align = 1;
    std::size_t payload_field_index = 0;
};

class ResolvedType final {
public:
    [[nodiscard]] static ResolvedType runtime_value(std::string source_name,
                                                    std::string llvm_type,
                                                    std::size_t size,
                                                    std::size_t align,
                                                    RuntimeStorageShape storage_shape,
                                                    ResolvedTypeIdentity identity);
    [[nodiscard]] static ResolvedType never_value(std::string source_name,
                                                  std::string llvm_type,
                                                  std::size_t size,
                                                  std::size_t align,
                                                  RuntimeStorageShape storage_shape,
                                                  ResolvedTypeIdentity identity);

    [[nodiscard]] const std::string& source_name() const noexcept { return source_name_; }
    [[nodiscard]] const std::string& llvm_type() const noexcept { return llvm_type_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t align() const noexcept { return align_; }
    [[nodiscard]] RuntimeStorageShape storage_shape() const noexcept { return storage_shape_; }
    [[nodiscard]] MaterializationState materialization() const noexcept { return materialization_; }
    [[nodiscard]] ResolvedTypeIdentity identity() const noexcept { return identity_; }

private:
    std::string source_name_;
    std::string llvm_type_;
    std::size_t size_;
    std::size_t align_;
    RuntimeStorageShape storage_shape_;
    MaterializationState materialization_;
    ResolvedTypeIdentity identity_;

    ResolvedType(std::string source_name,
                 std::string llvm_type,
                 std::size_t size,
                 std::size_t align,
                 RuntimeStorageShape storage_shape,
                 MaterializationState materialization,
                 ResolvedTypeIdentity identity);
};

struct YieldLayout {
    std::string llvm_type;
    std::string definition;
    std::string payload_storage_type;
    std::size_t payload_size = 0;
    std::size_t payload_align = 1;
    std::size_t payload_field_index = 0;
};

class BackendModel {
public:
    explicit BackendModel(const hir::Package& package);

    [[nodiscard]] const hir::FunctionDecl& function(hir::FunctionId id) const;
    [[nodiscard]] const hir::TypeDecl& type(hir::TypeId id) const;
    [[nodiscard]] const hir::VariantDecl& variant(hir::VariantId id) const;
    [[nodiscard]] const std::string& function_symbol(hir::FunctionId id) const;

    [[nodiscard]] std::expected<ResolvedType, std::string> resolve_type_name(const std::string& name);
    [[nodiscard]] std::expected<const TypeLayout*, std::string> ensure_type_layout(hir::TypeId id);
    [[nodiscard]] std::expected<const TypeLayout*, std::string> ensure_backend_aggregate_layout(
        const std::string& name);
    [[nodiscard]] std::expected<const YieldLayout*, std::string> ensure_yield_layout(hir::FunctionId id);
    [[nodiscard]] std::expected<const hir::FunctionDecl*, std::string> validate_entry_main() const;

    [[nodiscard]] const std::vector<std::string>& type_definitions() const;
    [[nodiscard]] const std::vector<std::string>& yield_definitions() const;

private:
    [[nodiscard]] std::expected<TypeLayout, std::string> build_type_layout(const hir::TypeDecl& type_decl);
    [[nodiscard]] std::expected<TypeLayout, std::string> build_record_layout(const hir::TypeDecl& type_decl);
    [[nodiscard]] std::expected<TypeLayout, std::string> build_tagged_union_layout(const hir::TypeDecl& type_decl);
    [[nodiscard]] std::expected<TypeLayout, std::string> build_permit_layout(const hir::TypeDecl& type_decl);
    [[nodiscard]] std::expected<TypeLayout, std::string> build_compiler_owned_companion_layout(
        const std::string& name);

    const hir::Package& package_;
    std::unordered_map<std::string, hir::TypeId> type_ids_by_name_;
    std::unordered_map<hir::TypeId, TypeLayout> type_layouts_;
    std::vector<int> visit_state_;
    std::vector<std::string> type_definition_order_;
    std::unordered_map<std::string, TypeLayout> backend_aggregate_layouts_;
    std::unordered_map<std::string, int> backend_aggregate_visit_state_;
    std::unordered_map<hir::FunctionId, YieldLayout> yield_layouts_;
    std::vector<std::string> yield_definition_order_;
    std::unordered_map<hir::FunctionId, std::string> function_symbols_;
};

struct StringGlobal {
    std::string name;
    std::string bytes;
};

class ModuleEmitter;
class FunctionEmitter;

struct BackendStepSucceeded final {};

using BackendStepResult = std::expected<BackendStepSucceeded, std::string>;

enum class TemporaryInputCleanup {
    RemoveTemporaryInput,
    PreserveTemporaryInput,
};

struct ToolchainCommandFailure final {
    std::string message;
    TemporaryInputCleanup temporary_input_cleanup;
};

using ToolchainCommandResult = std::expected<BackendStepSucceeded, ToolchainCommandFailure>;

enum class YieldReturnKind {
    Success,
    Failure,
};

enum class RuntimeHelper {
    Malloc,
    Memcpy,
    Memcmp,
    Strlen,
    Utf8,
    Nat,
};

enum class BlockVisitState {
    AwaitingFirstVisit,
    AlreadyVisited,
};

class FunctionEmitter {
public:
    FunctionEmitter(ModuleEmitter& module,
                    BackendModel& model,
                    const hir::FunctionDecl& hir_function,
                    const mir::Function& mir_function);

    [[nodiscard]] std::expected<std::string, std::string> emit();

private:
    struct TypedValue {
        ResolvedType type;
        std::string value;
    };

    [[nodiscard]] BackendStepResult prepare_locals();
    [[nodiscard]] std::expected<std::vector<ResolvedType>, std::string> resolve_compiler_owned_parameter_types();
    [[nodiscard]] std::expected<std::string, std::string> emit_compiler_owned_function(const ResolvedType& return_type);
    [[nodiscard]] BackendStepResult emit_statement(const mir::Statement& statement);
    [[nodiscard]] BackendStepResult emit_assign_use(mir::LocalId dest, const mir::Operand& operand);
    [[nodiscard]] BackendStepResult emit_assign_call(mir::LocalId dest, mir::Rvalue::CallValue value);
    [[nodiscard]] BackendStepResult emit_assign_construct(mir::LocalId dest,
                                                          mir::Rvalue::ConstructNamedTypeValue value);
    [[nodiscard]] BackendStepResult emit_assign_construct(mir::LocalId dest,
                                                          mir::Rvalue::ConstructNamedVariantValue value);
    [[nodiscard]] BackendStepResult emit_assign_project_field(mir::LocalId dest,
                                                              mir::Rvalue::ProjectNamedTypeFieldValue value);
    [[nodiscard]] BackendStepResult emit_assign_project_field(
        mir::LocalId dest,
        mir::Rvalue::ProjectNamedVariantPayloadFieldValue value);
    [[nodiscard]] BackendStepResult emit_assign_project_list_element(
        mir::LocalId dest,
        mir::Rvalue::ProjectListElementValue value);
    [[nodiscard]] BackendStepResult emit_assign_add_nat(mir::LocalId dest,
                                                        mir::Rvalue::AddNatValue value);
    [[nodiscard]] BackendStepResult emit_terminator(const mir::Terminator& terminator);
    [[nodiscard]] BackendStepResult emit_switch_variant(mir::Terminator::SwitchVariantValue terminator);
    [[nodiscard]] BackendStepResult emit_branch_list_element(mir::Terminator::BranchListElementValue terminator);
    [[nodiscard]] BackendStepResult emit_invoke(mir::Terminator::InvokeValue terminator);
    [[nodiscard]] BackendStepResult emit_function_return(const mir::Operand& operand);
    [[nodiscard]] BackendStepResult emit_function_fail(const mir::Operand& operand);
    [[nodiscard]] BackendStepResult emit_success_or_failure_return(const mir::Operand& operand,
                                                                   YieldReturnKind return_kind);
    [[nodiscard]] BackendStepResult emit_call_argument(std::vector<std::string>& args,
                                                       const hir::FunctionDecl& callee,
                                                       std::size_t index,
                                                       const mir::Operand& operand);
    [[nodiscard]] std::expected<TypedValue, std::string> materialize_operand(const mir::Operand& operand,
                                                                             const ResolvedType& expected_type);
    [[nodiscard]] std::expected<TypedValue, std::string> make_nat_literal_value(const ResolvedType& type,
                                                                                std::string_view lexeme);
    [[nodiscard]] std::expected<std::string, std::string> convert_nat_to_u64(const std::string& value);
    [[nodiscard]] std::expected<std::string, std::string> local_slot(mir::LocalId local_id) const;
    [[nodiscard]] std::expected<const ResolvedType*, std::string> local_type(mir::LocalId local_id) const;
    [[nodiscard]] std::expected<const TypeLayout*, std::string> user_layout_for_local(mir::LocalId local_id);
    [[nodiscard]] BackendStepResult store_typed_value(const std::string& slot_ptr,
                                                      const ResolvedType& type,
                                                      const std::string& value);
    [[nodiscard]] std::expected<std::string, std::string> load_from_slot(const std::string& slot_ptr,
                                                                         const ResolvedType& type);
    [[nodiscard]] BackendStepResult store_string_literal(const std::string& slot_ptr,
                                                         const ResolvedType& type,
                                                         const std::string& lexeme);
    [[nodiscard]] std::expected<TypedValue, std::string> make_string_literal_value(const ResolvedType& type,
                                                                                   const std::string& lexeme);
    [[nodiscard]] std::expected<std::string, std::string> insert_value(const std::string& aggregate_type,
                                                                       const std::string& aggregate_value,
                                                                       const std::string& element_type,
                                                                       const std::string& element_value,
                                                                       std::size_t field_index);
    [[nodiscard]] std::expected<std::string, std::string> extract_value(const std::string& aggregate_type,
                                                                         const std::string& aggregate_value,
                                                                         std::size_t field_index);
    [[nodiscard]] std::expected<std::string, std::string> pack_value_for_storage(const TypedValue& value,
                                                                                 const std::string& storage_type,
                                                                                 std::size_t storage_size);
    [[nodiscard]] std::expected<TypedValue, std::string> unpack_value_from_storage(const std::string& storage_value,
                                                                                    const std::string& storage_type,
                                                                                    const ResolvedType& expected_type);
    [[nodiscard]] std::string zero_value(const ResolvedType& type) const;
    [[nodiscard]] std::string next_temp(std::string_view prefix = "t");
    [[nodiscard]] std::string next_aux_block(std::string_view prefix = "trap");
    [[nodiscard]] static std::string block_name(mir::BlockId id);

    void emit_instruction(std::ostringstream& out, const std::string& text) const;
    void emit_block_lines(std::ostringstream& out, const std::vector<std::string>& lines) const;
    void append_line(const std::string& text);

    ModuleEmitter& module_;
    BackendModel& model_;
    const hir::FunctionDecl& hir_function_;
    const mir::Function& mir_function_;
    std::unordered_map<mir::LocalId, std::string> local_slots_;
    std::unordered_map<mir::LocalId, ResolvedType> local_types_;
    std::vector<std::vector<std::string>> block_preludes_;
    std::vector<std::string> current_block_lines_;
    std::vector<std::string> extra_blocks_;
    std::size_t temp_counter_ = 0;
    std::size_t aux_block_counter_ = 0;
};

class ModuleEmitter {
public:
    ModuleEmitter(BackendModel& model,
                  const hir::Package& hir_package,
                  const mir::Package& mir_package,
                  std::string target_triple,
                  EntryPointEmission entry_point_emission)
        : model_(model),
          hir_package_(hir_package),
          mir_package_(mir_package),
          target_triple_(std::move(target_triple)),
          entry_point_emission_(entry_point_emission) {}

    [[nodiscard]] std::expected<std::string, std::string> emit();
    [[nodiscard]] std::expected<StringGlobal, std::string> intern_string(const std::string& lexeme);
    [[nodiscard]] StringGlobal intern_bytes(std::string bytes);
    void require_runtime_helper(RuntimeHelper helper);

private:
    [[nodiscard]] std::string emit_entry_wrapper(const hir::FunctionDecl& source_main) const;

    BackendModel& model_;
    const hir::Package& hir_package_;
    const mir::Package& mir_package_;
    std::string target_triple_;
    EntryPointEmission entry_point_emission_ = EntryPointEmission::UserFunctionsOnly;
    std::vector<StringGlobal> string_globals_;
    std::unordered_map<std::string, std::size_t> string_indices_;
    std::vector<RuntimeHelper> runtime_helpers_;
};

[[nodiscard]] ResolvedType resolved_type_from_field(const FieldLayout& field);
[[nodiscard]] ResolvedType resolved_aggregate_type(std::string source_name,
                                                   std::string llvm_type,
                                                   std::size_t size,
                                                   std::size_t align);
[[nodiscard]] BackendStepResult validate_backend_package(BackendModel& model,
                                                         const hir::Package& hir_package,
                                                         const mir::Package& mir_package,
                                                         EntryPointEmission entry_point_emission);

FieldLayout FieldLayout::resolved_field(std::string name,
                                        std::string source_name,
                                        std::string llvm_type,
                                        std::size_t index,
                                        std::size_t size,
                                        std::size_t align,
                                        RuntimeStorageShape storage_shape,
                                        MaterializationState materialization,
                                        ResolvedTypeIdentity identity) {
    return FieldLayout(std::move(name),
                       std::move(source_name),
                       std::move(llvm_type),
                       index,
                       size,
                       align,
                       storage_shape,
                       materialization,
                       identity);
}

FieldLayout::FieldLayout(std::string name,
                         std::string source_name,
                         std::string llvm_type,
                         std::size_t index,
                         std::size_t size,
                         std::size_t align,
                         RuntimeStorageShape storage_shape,
                         MaterializationState materialization,
                         ResolvedTypeIdentity identity)
    : name_(std::move(name)),
      source_name_(std::move(source_name)),
      llvm_type_(std::move(llvm_type)),
      index_(index),
      size_(size),
      align_(align),
      storage_shape_(storage_shape),
      materialization_(materialization),
      identity_(identity) {}

ResolvedType ResolvedType::runtime_value(std::string source_name,
                                         std::string llvm_type,
                                         std::size_t size,
                                         std::size_t align,
                                         RuntimeStorageShape storage_shape,
                                         ResolvedTypeIdentity identity) {
    return ResolvedType(std::move(source_name),
                        std::move(llvm_type),
                        size,
                        align,
                        storage_shape,
                        MaterializationState::RuntimeValue,
                        identity);
}

ResolvedType ResolvedType::never_value(std::string source_name,
                                       std::string llvm_type,
                                       std::size_t size,
                                       std::size_t align,
                                       RuntimeStorageShape storage_shape,
                                       ResolvedTypeIdentity identity) {
    return ResolvedType(std::move(source_name),
                        std::move(llvm_type),
                        size,
                        align,
                        storage_shape,
                        MaterializationState::NeverValue,
                        identity);
}

ResolvedType::ResolvedType(std::string source_name,
                           std::string llvm_type,
                           std::size_t size,
                           std::size_t align,
                           RuntimeStorageShape storage_shape,
                           MaterializationState materialization,
                           ResolvedTypeIdentity identity)
    : source_name_(std::move(source_name)),
      llvm_type_(std::move(llvm_type)),
      size_(size),
      align_(align),
      storage_shape_(storage_shape),
      materialization_(materialization),
      identity_(identity) {}

bool is_float_literal_lexeme(std::string_view lexeme) {
    return lexeme.find_first_of(".eE") != std::string_view::npos;
}

bool is_builtin_float_type(const ResolvedType& type) {
    return type.identity().category() == ResolvedTypeCategory::BuiltinType
        && type.identity().builtin_kind() == BuiltinKind::Float;
}

bool is_builtin_nat_type(const ResolvedType& type) {
    return type.identity().category() == ResolvedTypeCategory::BuiltinType
        && type.identity().builtin_kind() == BuiltinKind::Nat;
}

bool is_integer_literal_destination(const ResolvedType& type) {
    if (is_builtin_nat_type(type)) {
        return true;
    }
    return type.storage_shape() == RuntimeStorageShape::Scalar
        && (type.llvm_type() == "i64" || type.llvm_type() == "i32" || type.llvm_type() == "i8");
}

enum class DecimalLiteralShape {
    PlainDecimalDigits,
    NotPlainDecimalDigits,
};

DecimalLiteralShape classify_decimal_literal_shape(std::string_view lexeme) {
    if (lexeme.empty()) {
        return DecimalLiteralShape::NotPlainDecimalDigits;
    }
    for (const char digit : lexeme) {
        if (digit < '0' || digit > '9') {
            return DecimalLiteralShape::NotPlainDecimalDigits;
        }
    }
    return DecimalLiteralShape::PlainDecimalDigits;
}

enum class NativeIntegerLiteralFit {
    WithinNativeCell,
    ExceedsNativeCell,
};

// Largest non-negative decimal magnitude (inclusive) representable in the
// destination's native integer cell without truncating any bits. The native
// emission path lowers fixed-width integers into LLVM scalars, so a
// literal wider than the cell cannot be materialized without silently dropping
// high bits. `Nat` is represented separately as an arbitrary-precision runtime
// carrier and is not limited by this table.
std::string_view native_integer_cell_capacity(std::string_view llvm_type) {
    if (llvm_type == "i8") {
        return "255";
    }
    if (llvm_type == "i32") {
        return "4294967295";
    }
    return "18446744073709551615";
}

NativeIntegerLiteralFit classify_native_integer_literal_fit(std::string_view decimal_digits,
                                                            std::string_view cell_capacity) {
    while (decimal_digits.size() > 1 && decimal_digits.front() == '0') {
        decimal_digits.remove_prefix(1);
    }
    if (decimal_digits.size() != cell_capacity.size()) {
        return decimal_digits.size() < cell_capacity.size() ? NativeIntegerLiteralFit::WithinNativeCell
                                                            : NativeIntegerLiteralFit::ExceedsNativeCell;
    }
    return decimal_digits <= cell_capacity ? NativeIntegerLiteralFit::WithinNativeCell
                                           : NativeIntegerLiteralFit::ExceedsNativeCell;
}

std::string canonical_nat_literal_text(std::string_view lexeme) {
    while (lexeme.size() > 1 && lexeme.front() == '0') {
        lexeme.remove_prefix(1);
    }
    return std::string(lexeme);
}

std::expected<std::string, std::string> format_float_literal_for_llvm(std::string_view lexeme) {
    const std::string text(lexeme);
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value)) {
        return std::unexpected("backend received invalid Float literal '" + text + "'");
    }
    if (value == 0.0) {
        return "0.000000e+00";
    }
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << bits;
    return out.str();
}

std::expected<std::string, std::string> materialize_number_literal_text(std::string_view lexeme,
                                                                        const ResolvedType& expected_type,
                                                                        std::string_view context) {
    if (is_float_literal_lexeme(lexeme)) {
        if (!is_builtin_float_type(expected_type)) {
            return std::unexpected("backend only supports float literals in Float destinations"
                                   + std::string(context.empty() ? "" : " for ") + std::string(context)
                                   + ", got '" + expected_type.source_name() + "'");
        }
        return format_float_literal_for_llvm(lexeme);
    }
    if (!is_integer_literal_destination(expected_type)) {
        return std::unexpected("backend only supports integer literals in integer destinations"
                               + std::string(context.empty() ? "" : " for ") + std::string(context)
                               + ", got '" + expected_type.source_name() + "'");
    }
    if (is_builtin_nat_type(expected_type)) {
        if (classify_decimal_literal_shape(lexeme) != DecimalLiteralShape::PlainDecimalDigits) {
            return std::unexpected("backend received invalid Nat literal '" + std::string(lexeme) + "'");
        }
        return std::string(lexeme);
    }
    if (classify_decimal_literal_shape(lexeme) == DecimalLiteralShape::PlainDecimalDigits
        && classify_native_integer_literal_fit(lexeme, native_integer_cell_capacity(expected_type.llvm_type()))
               == NativeIntegerLiteralFit::ExceedsNativeCell) {
        return std::unexpected("backend integer literal '" + std::string(lexeme)
                               + "' exceeds the native range of '" + expected_type.source_name() + "'");
    }
    return std::string(lexeme);
}

BackendModel::BackendModel(const hir::Package& package)
    : package_(package), visit_state_(package.types.size(), 0) {
    for (const hir::TypeDecl& type : package_.types) {
        type_ids_by_name_.emplace(type.qualified_name, type.id);
    }
    for (const hir::FunctionDecl& function_decl : package_.functions) {
        function_symbols_.emplace(function_decl.id, mangle_symbol_name(function_decl.qualified_name));
    }
}

const hir::FunctionDecl& BackendModel::function(hir::FunctionId id) const {
    return hir::lookup_function(package_, id);
}

const hir::TypeDecl& BackendModel::type(hir::TypeId id) const {
    return hir::lookup_type(package_, id);
}

const hir::VariantDecl& BackendModel::variant(hir::VariantId id) const {
    return hir::lookup_variant(package_, id);
}

const std::string& BackendModel::function_symbol(hir::FunctionId id) const {
    return function_symbols_.at(id);
}

const std::vector<std::string>& BackendModel::type_definitions() const {
    return type_definition_order_;
}

const std::vector<std::string>& BackendModel::yield_definitions() const {
    return yield_definition_order_;
}

std::expected<ResolvedType, std::string> BackendModel::resolve_type_name(const std::string& name) {
    const auto runtime_builtin =
        [&](std::string llvm_type,
            std::size_t size,
            std::size_t align,
            RuntimeStorageShape storage_shape,
            BuiltinKind kind) -> ResolvedType {
        return ResolvedType::runtime_value(name,
                                           std::move(llvm_type),
                                           size,
                                           align,
                                           storage_shape,
                                           ResolvedTypeIdentity::builtin_type(kind));
    };

    if (name == "Int") {
        return runtime_builtin("i64", 8, 8, RuntimeStorageShape::Scalar, BuiltinKind::Int);
    }
    if (name == "Nat") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::Nat);
    }
    if (name == "Float") {
        return runtime_builtin("double", 8, 8, RuntimeStorageShape::Scalar, BuiltinKind::Float);
    }
    if (name == "Char") {
        return runtime_builtin("i32", 4, 4, RuntimeStorageShape::Scalar, BuiltinKind::Char);
    }
    if (name == "Byte") {
        return runtime_builtin("i8", 1, 1, RuntimeStorageShape::Scalar, BuiltinKind::Byte);
    }
    if (name == "CInt") {
        return runtime_builtin("i32", 4, 4, RuntimeStorageShape::Scalar, BuiltinKind::CInt);
    }
    if (name == "CSize") {
        return runtime_builtin("i64", 8, 8, RuntimeStorageShape::Scalar, BuiltinKind::CSize);
    }
    if (name == "CString") {
        return runtime_builtin("ptr", 8, 8, RuntimeStorageShape::Aggregate, BuiltinKind::CString);
    }
    if (name == "Text") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::Text);
    }
    if (name == "NonEmptyText") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::NonEmptyText);
    }
    if (name == "Bytes") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::Bytes);
    }
    if (name == "NonEmptyBytes") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::NonEmptyBytes);
    }
    if (type_base_name(name) == "List") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::List);
    }
    if (type_base_name(name) == "NonEmptyList") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::NonEmptyList);
    }
    if (type_base_name(name) == "Map") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::Map);
    }
    if (type_base_name(name) == "NonEmptyMap") {
        return runtime_builtin("{ ptr, i64 }", 16, 8, RuntimeStorageShape::Aggregate, BuiltinKind::NonEmptyMap);
    }
    if (name == "Unit") {
        return runtime_builtin("i8", 1, 1, RuntimeStorageShape::Scalar, BuiltinKind::Unit);
    }
    if (name == "Never") {
        return ResolvedType::never_value(name,
                                         "i8",
                                         1,
                                         1,
                                         RuntimeStorageShape::Scalar,
                                         ResolvedTypeIdentity::builtin_type(BuiltinKind::Never));
    }
    if (compiler_owned_companion_type_membership(name)
        == CompilerOwnedCompanionTypeMembership::CompanionType) {
        const std::expected<const TypeLayout*, std::string> layout = ensure_backend_aggregate_layout(name);
        if (!layout.has_value()) {
            return std::unexpected(layout.error());
        }
        return ResolvedType::runtime_value(
            name,
            (*layout)->llvm_type,
            (*layout)->size,
            (*layout)->align,
            RuntimeStorageShape::Aggregate,
            ResolvedTypeIdentity::backend_aggregate_storage());
    }

    const auto type_it = type_ids_by_name_.find(name);
    if (type_it == type_ids_by_name_.end()) {
        return std::unexpected("backend could not resolve type '" + name + "'");
    }

    const std::expected<const TypeLayout*, std::string> layout = ensure_type_layout(type_it->second);
    if (!layout.has_value()) {
        return std::unexpected(layout.error());
    }
    return ResolvedType::runtime_value(
        name,
        (*layout)->llvm_type,
        (*layout)->size,
        (*layout)->align,
        RuntimeStorageShape::Aggregate,
        ResolvedTypeIdentity::package_type(type_it->second));
}

std::expected<const TypeLayout*, std::string> BackendModel::ensure_type_layout(hir::TypeId id) {
    if (const auto it = type_layouts_.find(id); it != type_layouts_.end()) {
        return &it->second;
    }
    if (visit_state_.at(id) == 1) {
        return std::unexpected("recursive native layout is not supported yet for '"
                               + hir::lookup_type(package_, id).qualified_name + "'");
    }

    visit_state_[id] = 1;
    std::expected<TypeLayout, std::string> layout = build_type_layout(hir::lookup_type(package_, id));
    if (!layout.has_value()) {
        visit_state_[id] = 0;
        return std::unexpected(layout.error());
    }
    type_layouts_.emplace(id, std::move(*layout));
    visit_state_[id] = 2;
    return &type_layouts_.at(id);
}

std::expected<const TypeLayout*, std::string> BackendModel::ensure_backend_aggregate_layout(
    const std::string& name) {
    if (const auto it = backend_aggregate_layouts_.find(name); it != backend_aggregate_layouts_.end()) {
        return &it->second;
    }

    const int state = backend_aggregate_visit_state_[name];
    if (state == 1) {
        return std::unexpected("recursive backend aggregate layout is not supported for '" + name + "'");
    }

    backend_aggregate_visit_state_[name] = 1;
    std::expected<TypeLayout, std::string> layout = build_compiler_owned_companion_layout(name);
    if (!layout.has_value()) {
        backend_aggregate_visit_state_[name] = 0;
        return std::unexpected(layout.error());
    }
    backend_aggregate_layouts_.emplace(name, std::move(*layout));
    backend_aggregate_visit_state_[name] = 2;
    return &backend_aggregate_layouts_.at(name);
}

std::expected<const YieldLayout*, std::string> BackendModel::ensure_yield_layout(hir::FunctionId id) {
    if (const auto it = yield_layouts_.find(id); it != yield_layouts_.end()) {
        return &it->second;
    }

    const hir::FunctionDecl& function_decl = function(id);
    if (function_decl.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
        return std::unexpected("internal backend error: function '" + function_decl.qualified_name
                               + "' does not declare fails");
    }
    if (!function_decl.generics.empty()) {
        return std::unexpected("backend does not yet support generic function '"
                               + function_decl.qualified_name + "'");
    }

    const std::expected<ResolvedType, std::string> success_type = resolve_type_name(function_decl.return_type.text);
    if (!success_type.has_value()) {
        return std::unexpected(success_type.error());
    }
    const std::expected<ResolvedType, std::string> failure_type = resolve_type_name(
        hir::lookup_type(package_, function_decl.failure.reason_type_id()).qualified_name);
    if (!failure_type.has_value()) {
        return std::unexpected(failure_type.error());
    }

    const std::size_t payload_align = std::max(success_type->align(), failure_type->align());
    const std::size_t payload_size = std::max(success_type->size(), failure_type->size());
    const std::expected<std::string, std::string> storage_type = make_storage_type(payload_size, payload_align);
    if (!storage_type.has_value()) {
        return std::unexpected(storage_type.error());
    }

    YieldLayout layout;
    layout.llvm_type = mangle_yield_name(function_decl.qualified_name);
    layout.payload_storage_type = *storage_type;
    layout.payload_size = payload_size;
    layout.payload_align = payload_align;

    std::ostringstream definition;
    definition << layout.llvm_type << " = type { i8";
    if (payload_size > 0) {
        const std::size_t padding = align_up(1, payload_align) - 1;
        if (padding > 0) {
            definition << ", [" << padding << " x i8]";
        }
        layout.payload_field_index = padding > 0 ? 2 : 1;
        definition << ", " << layout.payload_storage_type;
    }
    definition << " }";
    layout.definition = definition.str();

    yield_definition_order_.push_back(layout.definition);
    yield_layouts_.emplace(id, std::move(layout));
    return &yield_layouts_.at(id);
}

std::expected<const hir::FunctionDecl*, std::string> BackendModel::validate_entry_main() const {
    const hir::FunctionDecl* main_function = nullptr;
    for (const hir::FunctionDecl& function_decl : package_.functions) {
        if (function_decl.qualified_name == "main" || function_decl.qualified_name.ends_with("::main")) {
            main_function = &function_decl;
            break;
        }
    }

    if (main_function == nullptr) {
        return std::unexpected("`--emit-exe` requires a public `fn main() -> Int` in a module (e.g. `domain module ... { public fn main() ... }`)");
    }
    if (main_function->visibility != ast::Visibility::Public) {
        return std::unexpected("entry `main` must be declared `public`");
    }
    if (main_function->implementation == ast::FunctionImplementation::ForeignImport || main_function->body == nullptr) {
        return std::unexpected("entry `main` must have a function body");
    }
    if (!main_function->generics.empty()) {
        return std::unexpected("entry `main` may not be generic");
    }
    if (!main_function->params.empty()) {
        return std::unexpected("entry `main` may not take parameters");
    }
    if (main_function->failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
        return std::unexpected("entry `main` may not use `fails`");
    }
    if (main_function->return_type.text != "Int") {
        return std::unexpected("entry `main` must return `Int`");
    }
    return main_function;
}

std::expected<TypeLayout, std::string> BackendModel::build_type_layout(const hir::TypeDecl& type_decl) {
    if (!type_decl.generics.empty()) {
        return std::unexpected("backend does not yet support generic type '" + type_decl.qualified_name + "'");
    }

    switch (type_decl.kind) {
    case hir::TypeKind::Record:
    case hir::TypeKind::Proof:
    case hir::TypeKind::Phase:
        return build_record_layout(type_decl);
    case hir::TypeKind::State:
    case hir::TypeKind::Reason:
        return build_tagged_union_layout(type_decl);
    case hir::TypeKind::Permit:
        return build_permit_layout(type_decl);
    }
    return std::unexpected("backend could not lower type '" + type_decl.qualified_name + "'");
}

std::expected<TypeLayout, std::string> BackendModel::build_record_layout(const hir::TypeDecl& type_decl) {
    TypeLayout layout;
    layout.source_name = type_decl.qualified_name;
    layout.llvm_type = mangle_type_name(type_decl.qualified_name);
    layout.kind = type_decl.kind;

    std::size_t offset = 0;
    std::size_t record_align = 1;
    std::vector<std::string> field_types;
    field_types.reserve(type_decl.fields.size());

    for (const hir::FieldDecl& field : type_decl.fields) {
        const std::expected<ResolvedType, std::string> field_type = resolve_type_name(field.type.text);
        if (!field_type.has_value()) {
            return std::unexpected("while lowering field '" + field.name + "' of '"
                                   + type_decl.qualified_name + "': " + field_type.error());
        }
        if (field_type->materialization() == MaterializationState::NeverValue) {
            return std::unexpected("backend does not support materialized field type `Never` in '"
                                   + type_decl.qualified_name + "'");
        }

        offset = align_up(offset, field_type->align());
        record_align = std::max(record_align, field_type->align());
        layout.field_indices.emplace(field.name, layout.fields.size());
        layout.fields.push_back(FieldLayout::resolved_field(
            field.name,
            field.type.text,
            field_type->llvm_type(),
            layout.fields.size(),
            field_type->size(),
            field_type->align(),
            field_type->storage_shape(),
            field_type->materialization(),
            field_type->identity()));
        field_types.push_back(field_type->llvm_type());
        offset += field_type->size();
    }

    layout.size = align_up(offset, record_align);
    layout.align = record_align;

    std::ostringstream definition;
    definition << layout.llvm_type << " = type ";
    if (field_types.empty()) {
        definition << "{}";
    } else {
        definition << "{ ";
        for (std::size_t index = 0; index < field_types.size(); ++index) {
            if (index > 0) {
                definition << ", ";
            }
            definition << field_types[index];
        }
        definition << " }";
    }
    layout.definition = definition.str();
    type_definition_order_.push_back(layout.definition);
    return layout;
}

std::expected<TypeLayout, std::string> BackendModel::build_compiler_owned_companion_layout(
    const std::string& name) {
    if (compiler_owned_companion_type_membership(name)
        != CompilerOwnedCompanionTypeMembership::CompanionType) {
        return std::unexpected("backend could not lower compiler-owned companion type '" + name + "'");
    }

    const std::expected<std::vector<std::string>, std::string> args = generic_type_arguments(name);
    if (!args.has_value()) {
        return std::unexpected(args.error());
    }

    const std::string_view base_name = type_base_name(name);
    std::vector<std::pair<std::string, std::string>> field_specs;
    if (base_name == "ListFirstAndRest") {
        if (args->size() != 1) {
            return std::unexpected("compiler-owned companion type '" + name + "' expected 1 type argument");
        }
        field_specs.emplace_back("first", (*args)[0]);
        field_specs.emplace_back("rest", "List<" + (*args)[0] + ">");
    } else {
        if (args->size() != 2) {
            return std::unexpected("compiler-owned companion type '" + name + "' expected 2 type arguments");
        }
        if (base_name == "MapEntry") {
            field_specs.emplace_back("key", (*args)[0]);
            field_specs.emplace_back("value", (*args)[1]);
        } else if (base_name == "MapFirstEntryAndRest") {
            field_specs.emplace_back("first", "MapEntry<" + (*args)[0] + ", " + (*args)[1] + ">");
            field_specs.emplace_back("rest", "Map<" + (*args)[0] + ", " + (*args)[1] + ">");
        } else {
            field_specs.emplace_back("value", (*args)[1]);
            field_specs.emplace_back("rest", "Map<" + (*args)[0] + ", " + (*args)[1] + ">");
        }
    }

    TypeLayout layout;
    layout.source_name = name;
    layout.llvm_type = mangle_type_name(name);
    layout.kind = hir::TypeKind::Record;

    std::size_t offset = 0;
    std::size_t record_align = 1;
    std::vector<std::string> field_types;
    field_types.reserve(field_specs.size());

    for (const auto& [field_name, field_source_name] : field_specs) {
        const std::expected<ResolvedType, std::string> field_type = resolve_type_name(field_source_name);
        if (!field_type.has_value()) {
            return std::unexpected("while lowering field '" + field_name + "' of '" + name + "': "
                                   + field_type.error());
        }
        if (field_type->materialization() == MaterializationState::NeverValue) {
            return std::unexpected("backend does not support materialized field type `Never` in '"
                                   + name + "'");
        }

        offset = align_up(offset, field_type->align());
        record_align = std::max(record_align, field_type->align());
        layout.field_indices.emplace(field_name, layout.fields.size());
        layout.fields.push_back(FieldLayout::resolved_field(
            field_name,
            field_source_name,
            field_type->llvm_type(),
            layout.fields.size(),
            field_type->size(),
            field_type->align(),
            field_type->storage_shape(),
            field_type->materialization(),
            field_type->identity()));
        field_types.push_back(field_type->llvm_type());
        offset += field_type->size();
    }

    layout.size = align_up(offset, record_align);
    layout.align = record_align;

    std::ostringstream definition;
    definition << layout.llvm_type << " = type { ";
    for (std::size_t index = 0; index < field_types.size(); ++index) {
        if (index > 0) {
            definition << ", ";
        }
        definition << field_types[index];
    }
    definition << " }";
    layout.definition = definition.str();
    type_definition_order_.push_back(layout.definition);
    return layout;
}

std::expected<TypeLayout, std::string> BackendModel::build_permit_layout(const hir::TypeDecl& type_decl) {
    TypeLayout layout;
    layout.source_name = type_decl.qualified_name;
    layout.llvm_type = mangle_type_name(type_decl.qualified_name);
    layout.kind = type_decl.kind;
    layout.size = 1;
    layout.align = 1;
    layout.definition = layout.llvm_type + " = type { i8 }";
    type_definition_order_.push_back(layout.definition);
    return layout;
}

std::expected<TypeLayout, std::string> BackendModel::build_tagged_union_layout(const hir::TypeDecl& type_decl) {
    TypeLayout layout;
    layout.source_name = type_decl.qualified_name;
    layout.llvm_type = mangle_type_name(type_decl.qualified_name);
    layout.kind = type_decl.kind;

    std::vector<std::string> payload_definitions;
    std::size_t max_payload_size = 0;
    std::size_t max_payload_align = 1;

    for (hir::VariantId variant_id : type_decl.variants) {
        const hir::VariantDecl& variant_decl = variant(variant_id);
        VariantLayout variant_layout;
        variant_layout.id = variant_decl.id;
        variant_layout.payload_llvm_type = mangle_variant_payload_name(variant_decl.qualified_name);
        variant_layout.tag_value = static_cast<std::uint32_t>(variant_decl.id);

        std::size_t offset = 0;
        std::size_t payload_align = 1;
        std::vector<std::string> field_types;

        for (const hir::FieldDecl& field : variant_decl.fields) {
            const std::expected<ResolvedType, std::string> field_type = resolve_type_name(field.type.text);
            if (!field_type.has_value()) {
                return std::unexpected(field_type.error());
            }
            if (field_type->materialization() == MaterializationState::NeverValue) {
                return std::unexpected("backend does not support materialized field type `Never` in '"
                                       + variant_decl.qualified_name + "'");
            }

            offset = align_up(offset, field_type->align());
            payload_align = std::max(payload_align, field_type->align());
            variant_layout.field_indices.emplace(field.name, variant_layout.fields.size());
            variant_layout.fields.push_back(FieldLayout::resolved_field(
                field.name,
                field.type.text,
                field_type->llvm_type(),
                variant_layout.fields.size(),
                field_type->size(),
                field_type->align(),
                field_type->storage_shape(),
                field_type->materialization(),
                field_type->identity()));
            field_types.push_back(field_type->llvm_type());
            offset += field_type->size();
        }

        variant_layout.payload_size = align_up(offset, payload_align);
        variant_layout.payload_align = payload_align;
        if (variant_layout.fields.empty()) {
            variant_layout.payload_definition = variant_layout.payload_llvm_type + " = type {}";
        } else {
            std::ostringstream payload_def;
            payload_def << variant_layout.payload_llvm_type << " = type { ";
            for (std::size_t index = 0; index < field_types.size(); ++index) {
                if (index > 0) {
                    payload_def << ", ";
                }
                payload_def << field_types[index];
            }
            payload_def << " }";
            variant_layout.payload_definition = payload_def.str();
        }

        payload_definitions.push_back(variant_layout.payload_definition);
        max_payload_size = std::max(max_payload_size, variant_layout.payload_size);
        max_payload_align = std::max(max_payload_align, variant_layout.payload_align);
        layout.variants.emplace(variant_layout.id, std::move(variant_layout));
    }

    layout.size = 4;
    layout.align = 4;

    std::ostringstream definition;
    definition << layout.llvm_type << " = type { i32";
    if (max_payload_size > 0) {
        const std::expected<std::string, std::string> storage_type = make_storage_type(max_payload_size, max_payload_align);
        if (!storage_type.has_value()) {
            return std::unexpected(storage_type.error());
        }
        const std::size_t padding = align_up(4, max_payload_align) - 4;
        layout.payload_state = PayloadStorageState::CarriesPayload;
        layout.payload_storage_type = *storage_type;
        layout.payload_size = max_payload_size;
        layout.payload_align = max_payload_align;
        layout.payload_field_index = padding > 0 ? 2 : 1;
        if (padding > 0) {
            definition << ", [" << padding << " x i8]";
        }
        definition << ", " << layout.payload_storage_type;
        layout.align = std::max<std::size_t>(4, max_payload_align);
        layout.size = align_up(4 + padding + max_payload_size, layout.align);
    }
    definition << " }";
    layout.definition = definition.str();

    for (const std::string& payload_definition : payload_definitions) {
        type_definition_order_.push_back(payload_definition);
    }
    type_definition_order_.push_back(layout.definition);
    return layout;
}

ResolvedType resolved_type_from_field(const FieldLayout& field) {
    if (field.materialization() == MaterializationState::NeverValue) {
        return ResolvedType::never_value(field.source_name(),
                                         field.llvm_type(),
                                         field.size(),
                                         field.align(),
                                         field.storage_shape(),
                                         field.identity());
    }
    return ResolvedType::runtime_value(field.source_name(),
                                       field.llvm_type(),
                                       field.size(),
                                       field.align(),
                                       field.storage_shape(),
                                       field.identity());
}

ResolvedType resolved_aggregate_type(std::string source_name,
                                     std::string llvm_type,
                                     std::size_t size,
                                     std::size_t align) {
    return ResolvedType::runtime_value(
        std::move(source_name),
        std::move(llvm_type),
        size,
        align,
        RuntimeStorageShape::Aggregate,
        ResolvedTypeIdentity::backend_aggregate_storage());
}

BackendStepResult validate_backend_package(BackendModel& model,
                                           const hir::Package& hir_package,
                                           const mir::Package& mir_package,
                                           EntryPointEmission entry_point_emission) {
    if (entry_point_emission == EntryPointEmission::IncludeExecutableEntryPoint) {
        const std::expected<const hir::FunctionDecl*, std::string> validated_main = model.validate_entry_main();
        if (!validated_main.has_value()) {
            return std::unexpected(validated_main.error());
        }
    }

    auto require_materializable_type = [](const ResolvedType& type, const std::string& context)
        -> BackendStepResult {
        if (type.materialization() == MaterializationState::NeverValue) {
            return std::unexpected("backend does not yet support materialized `Never` in " + context);
        }
        return BackendStepSucceeded{};
    };

    auto resolve_materializable_type = [&](const std::string& type_name,
                                           const std::string& context) -> std::expected<ResolvedType, std::string> {
        const std::expected<ResolvedType, std::string> resolved = model.resolve_type_name(type_name);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        if (const BackendStepResult valid = require_materializable_type(*resolved, context);
            !valid.has_value()) {
            return std::unexpected(valid.error());
        }
        return *resolved;
    };

    for (const hir::TypeDecl& type_decl : hir_package.types) {
        const std::expected<const TypeLayout*, std::string> layout = model.ensure_type_layout(type_decl.id);
        if (!layout.has_value()) {
            return std::unexpected(layout.error());
        }
    }

    for (const hir::FunctionDecl& function_decl : hir_package.functions) {
        const std::string signature_context = "signature of function '" + function_decl.qualified_name + "'";
        if (!function_decl.generics.empty()) {
            return std::unexpected("backend does not yet support generic function '"
                                   + function_decl.qualified_name + "'");
        }
        if (const std::expected<ResolvedType, std::string> return_type = resolve_materializable_type(
                function_decl.return_type.text,
                signature_context);
            !return_type.has_value()) {
            return std::unexpected(return_type.error());
        }
        for (const hir::Parameter& param : function_decl.params) {
            if (materialization_of(param.type.discipline) == typesys::DisciplineMaterialization::CompileTimeOnly) {
                continue;
            }
            const std::expected<ResolvedType, std::string> param_type = resolve_materializable_type(
                param.type.text,
                "parameter '" + param.name + "' of function '" + function_decl.qualified_name + "'");
            if (!param_type.has_value()) {
                return std::unexpected(param_type.error());
            }
        }
        if (function_decl.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
            const std::expected<const YieldLayout*, std::string> yield_layout = model.ensure_yield_layout(function_decl.id);
            if (!yield_layout.has_value()) {
                return std::unexpected(yield_layout.error());
            }
            const hir::TypeDecl& reason_type = model.type(function_decl.failure.reason_type_id());
            const std::expected<ResolvedType, std::string> resolved_reason = resolve_materializable_type(
                reason_type.qualified_name,
                "yield reason of function '" + function_decl.qualified_name + "'");
            if (!resolved_reason.has_value()) {
                return std::unexpected(resolved_reason.error());
            }
        }
    }

    if (mir_package.function_count() != hir_package.functions.size()) {
        return std::unexpected("internal backend error: HIR/MIR function count mismatch");
    }

    for (const mir::Function& mir_function : mir_package.functions()) {
        if (mir_function.function_id() >= hir_package.functions.size()) {
            return std::unexpected("internal backend error: MIR function id out of range");
        }

        const hir::FunctionDecl& hir_function = hir::lookup_function(hir_package, mir_function.function_id());
        if (mir_function.implementation() != hir_function.implementation) {
            return std::unexpected("internal backend error: function implementation mode mismatch for function '"
                                   + hir_function.qualified_name + "'");
        }

        std::unordered_map<mir::LocalId, ResolvedType> local_types;
        local_types.reserve(mir_function.locals().size());
        std::unordered_map<mir::LocalId, typesys::UseDiscipline> local_disciplines;
        local_disciplines.reserve(mir_function.locals().size());
        for (const mir::Local& local : mir_function.locals()) {
            const std::expected<ResolvedType, std::string> resolved =
                materialization_of(local.discipline()) == typesys::DisciplineMaterialization::CompileTimeOnly
                ? model.resolve_type_name(local.type_name())
                : resolve_materializable_type(
                    local.type_name(),
                    "local '" + local.name() + "' in function '" + hir_function.qualified_name + "'");
            if (!resolved.has_value()) {
                return std::unexpected(resolved.error());
            }
            local_types.emplace(local.id(), *resolved);
            local_disciplines.emplace(local.id(), local.discipline());
        }

        auto local_type = [&](mir::LocalId local_id, const std::string& context)
            -> std::expected<const ResolvedType*, std::string> {
            const auto it = local_types.find(local_id);
            if (it == local_types.end()) {
                return std::unexpected("internal backend error: unknown local %" + std::to_string(local_id)
                                       + " in " + context);
            }
            return &it->second;
        };

        auto validate_operand = [&](const mir::Operand& operand,
                                    const ResolvedType& expected_type,
                                    const std::string& context) -> BackendStepResult {
            return operand.match(
                [&](mir::Operand::LocalValue local) -> BackendStepResult {
                    const auto discipline_it = local_disciplines.find(local.local_id);
                    if (discipline_it != local_disciplines.end()
                        && materialization_of(discipline_it->second)
                            == typesys::DisciplineMaterialization::CompileTimeOnly) {
                        return std::unexpected("internal backend error: compile-time-only local %"
                                               + std::to_string(local.local_id) + " used in " + context);
                    }
                    const std::expected<const ResolvedType*, std::string> actual_type = local_type(local.local_id, context);
                    if (!actual_type.has_value()) {
                        return std::unexpected(actual_type.error());
                    }
                    if ((*actual_type)->source_name() != expected_type.source_name()) {
                        return std::unexpected("backend type mismatch in " + context + ": expected '"
                                               + expected_type.source_name() + "', got '" + (*actual_type)->source_name() + "'");
                    }
                    return BackendStepSucceeded{};
                },
                [&](mir::Operand::IntLiteralValue literal) -> BackendStepResult {
                    if (const std::expected<std::string, std::string> materialized =
                            materialize_number_literal_text(literal.text, expected_type, context);
                        !materialized.has_value()) {
                        return std::unexpected(materialized.error());
                    }
                    return BackendStepSucceeded{};
                },
                [&](mir::Operand::StringLiteralValue) -> BackendStepResult {
                    if (expected_type.identity().category() != ResolvedTypeCategory::BuiltinType
                        || (expected_type.identity().builtin_kind() != BuiltinKind::Text
                            && expected_type.identity().builtin_kind() != BuiltinKind::NonEmptyText
                            && expected_type.identity().builtin_kind() != BuiltinKind::Bytes
                            && expected_type.identity().builtin_kind() != BuiltinKind::NonEmptyBytes
                            && expected_type.identity().builtin_kind() != BuiltinKind::CString)) {
                        return std::unexpected("backend only supports string literals for Text/NonEmptyText/Bytes/"
                                               "NonEmptyBytes/CString values in "
                                               + context + ", got '" + expected_type.source_name() + "'");
                    }
                    return BackendStepSucceeded{};
                },
                [&](mir::Operand::UnitValue) -> BackendStepResult {
                    if (expected_type.identity().category() != ResolvedTypeCategory::BuiltinType
                        || expected_type.identity().builtin_kind() != BuiltinKind::Unit) {
                        return std::unexpected("backend expected Unit in " + context + ", got '"
                                               + expected_type.source_name() + "'");
                    }
                    return BackendStepSucceeded{};
                });
        };

        auto aggregate_layout_for_type = [&](const ResolvedType& type,
                                             const std::string& context)
            -> std::expected<const TypeLayout*, std::string> {
            if (type.identity().category() == ResolvedTypeCategory::PackageTypeDeclaration) {
                return model.ensure_type_layout(type.identity().package_type_id());
            }
            if (type.identity().category() == ResolvedTypeCategory::BackendAggregateStorage) {
                return model.ensure_backend_aggregate_layout(type.source_name());
            }
            return std::unexpected("backend expected an aggregate base for projection in " + context
                                   + ", got '" + type.source_name() + "'");
        };

        auto validate_block_target = [&](mir::BlockId block_id, const std::string& context)
            -> BackendStepResult {
            if (block_id >= mir_function.blocks().size()) {
                return std::unexpected("internal backend error: target block %" + std::to_string(block_id)
                                       + " out of range in " + context);
            }
            return BackendStepSucceeded{};
        };

        if (hir_function.implementation == ast::FunctionImplementation::ForeignImport) {
            if (!mir_function.blocks().empty()) {
                return std::unexpected("internal backend error: foreign function '" + hir_function.qualified_name
                                       + "' unexpectedly has MIR blocks");
            }
            continue;
        }

        std::vector<BlockVisitState> block_visit_states(
            mir_function.blocks().size(),
            BlockVisitState::AwaitingFirstVisit);
        for (const mir::BasicBlock& block : mir_function.blocks()) {
            if (block.id() >= mir_function.blocks().size()) {
                return std::unexpected("internal backend error: block id out of range in function '"
                                       + hir_function.qualified_name + "'");
            }
            if (block_visit_states[block.id()] == BlockVisitState::AlreadyVisited) {
                return std::unexpected("internal backend error: duplicate block id %" + std::to_string(block.id())
                                       + " in function '" + hir_function.qualified_name + "'");
            }
            block_visit_states[block.id()] = BlockVisitState::AlreadyVisited;

            for (const mir::Statement& statement : block.statements()) {
                const mir::LocalId statement_dest = statement.dest_local();
                const mir::Rvalue& statement_value = statement.rvalue();
                const std::string statement_context = "function '" + hir_function.qualified_name + "'";
                const std::expected<const ResolvedType*, std::string> dest_type
                    = local_type(statement_dest, statement_context);
                if (!dest_type.has_value()) {
                    return std::unexpected(dest_type.error());
                }
                const auto dest_discipline = local_disciplines.find(statement_dest);
                if (dest_discipline != local_disciplines.end()
                    && materialization_of(dest_discipline->second) == typesys::DisciplineMaterialization::CompileTimeOnly) {
                    return std::unexpected("internal backend error: compile-time-only local %"
                                           + std::to_string(statement_dest)
                                           + " cannot be assigned in function '" + hir_function.qualified_name + "'");
                }

                const BackendStepResult statement_valid = statement_value.match(
                    [&](mir::Rvalue::UseValue value) -> BackendStepResult {
                    if (const BackendStepResult valid = validate_operand(
                            value.operand,
                            *(*dest_type),
                            "assignment to local %" + std::to_string(statement_dest)
                                + " in function '" + hir_function.qualified_name + "'");
                        !valid.has_value()) {
                        return std::unexpected(valid.error());
                    }
                    return BackendStepSucceeded{};
                    },
                    [&](mir::Rvalue::CallValue value) -> BackendStepResult {
                    if (value.function_id >= hir_package.functions.size()) {
                        return std::unexpected("internal backend error: call target id out of range in function '"
                                               + hir_function.qualified_name + "'");
                    }
                    const hir::FunctionDecl& callee = hir::lookup_function(hir_package, value.function_id);
                    if (callee.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
                        return std::unexpected("backend does not support lowering a yielding call as a plain assignment in '"
                                               + hir_function.qualified_name + "'");
                    }
                    if (value.args.size() != callee.params.size()) {
                        return std::unexpected("internal backend error: call arity mismatch for '"
                                               + callee.qualified_name + "'");
                    }
                    const std::expected<ResolvedType, std::string> return_type = resolve_materializable_type(
                        callee.return_type.text,
                        "return type of callee '" + callee.qualified_name + "'");
                    if (!return_type.has_value()) {
                        return std::unexpected(return_type.error());
                    }
                    if (return_type->source_name() != (*dest_type)->source_name()) {
                        return std::unexpected("backend type mismatch for call result of '" + callee.qualified_name
                                               + "': expected '" + (*dest_type)->source_name() + "', got '"
                                               + return_type->source_name() + "'");
                    }
                    for (std::size_t index = 0; index < value.args.size(); ++index) {
                        if (materialization_of(callee.params[index].type.discipline)
                            == typesys::DisciplineMaterialization::CompileTimeOnly) {
                            if (compile_time_argument_encoding_of(value.args[index])
                                != CompileTimeArgumentEncoding::ErasedToUnitOperand) {
                                return std::unexpected("internal backend error: compile-time-only parameter "
                                                       + std::to_string(index) + " of callee '"
                                                       + callee.qualified_name + "' was not erased");
                            }
                            continue;
                        }
                        const std::expected<ResolvedType, std::string> param_type = resolve_materializable_type(
                            callee.params[index].type.text,
                            "parameter " + std::to_string(index) + " of callee '" + callee.qualified_name + "'");
                        if (!param_type.has_value()) {
                            return std::unexpected(param_type.error());
                        }
                        const BackendStepResult valid = validate_operand(
                            value.args[index],
                            *param_type,
                            "argument " + std::to_string(index) + " of call to '" + callee.qualified_name + "'");
                        if (!valid.has_value()) {
                            return std::unexpected(valid.error());
                        }
                    }
                    return BackendStepSucceeded{};
                    },
                    [&](mir::Rvalue::ConstructNamedTypeValue value) -> BackendStepResult {
                    if ((*dest_type)->identity().category() != ResolvedTypeCategory::PackageTypeDeclaration) {
                        return std::unexpected("backend only supports constructing user-defined values, got '"
                                               + (*dest_type)->source_name() + "'");
                    }
                    const std::expected<const TypeLayout*, std::string> owner_layout
                        = model.ensure_type_layout(value.owner_type_id);
                    if (!owner_layout.has_value()) {
                        return std::unexpected(owner_layout.error());
                    }
                    for (const mir::FieldValue& field : value.fields) {
                        const auto field_it = (*owner_layout)->field_indices.find(field.field_name());
                        if (field_it == (*owner_layout)->field_indices.end()) {
                            return std::unexpected("backend could not resolve field '" + field.field_name()
                                                   + "' in '" + value.qualified_name + "'");
                        }
                        const ResolvedType field_type = resolved_type_from_field((*owner_layout)->fields[field_it->second]);
                        const BackendStepResult valid = validate_operand(
                            field.operand(),
                            field_type,
                            "field '" + field.field_name() + "' of '" + value.qualified_name + "'");
                        if (!valid.has_value()) {
                            return std::unexpected(valid.error());
                        }
                    }
                    return BackendStepSucceeded{};
                    },
                    [&](mir::Rvalue::ConstructNamedVariantValue value) -> BackendStepResult {
                    if ((*dest_type)->identity().category() != ResolvedTypeCategory::PackageTypeDeclaration) {
                        return std::unexpected("backend only supports constructing user-defined values, got '"
                                               + (*dest_type)->source_name() + "'");
                    }
                    const std::expected<const TypeLayout*, std::string> owner_layout
                        = model.ensure_type_layout(value.owner_type_id);
                    if (!owner_layout.has_value()) {
                        return std::unexpected(owner_layout.error());
                    }
                    const auto variant_it = (*owner_layout)->variants.find(value.variant_id);
                    if (variant_it == (*owner_layout)->variants.end()) {
                        return std::unexpected("backend could not resolve variant layout for '"
                                               + value.qualified_name + "'");
                    }
                    const VariantLayout& variant_layout = variant_it->second;
                    for (const mir::FieldValue& field : value.fields) {
                        const auto field_it = variant_layout.field_indices.find(field.field_name());
                        if (field_it == variant_layout.field_indices.end()) {
                            return std::unexpected("backend could not resolve variant field '" + field.field_name()
                                                   + "' in '" + value.qualified_name + "'");
                        }
                        const ResolvedType field_type = resolved_type_from_field(variant_layout.fields[field_it->second]);
                        const BackendStepResult valid = validate_operand(
                            field.operand(),
                            field_type,
                            "field '" + field.field_name() + "' of '" + value.qualified_name + "'");
                        if (!valid.has_value()) {
                            return std::unexpected(valid.error());
                        }
                    }
                    return BackendStepSucceeded{};
                    },
                    [&](mir::Rvalue::ProjectNamedTypeFieldValue value) -> BackendStepResult {
                    const std::expected<const ResolvedType*, std::string> base_type
                        = local_type(value.base_local, statement_context);
                    if (!base_type.has_value()) {
                        return std::unexpected(base_type.error());
                    }
                    const std::expected<const TypeLayout*, std::string> owner_layout = aggregate_layout_for_type(
                        *(*base_type),
                        "function '" + hir_function.qualified_name + "'");
                    if (!owner_layout.has_value()) {
                        return std::unexpected(owner_layout.error());
                    }
                    const auto field_it = (*owner_layout)->field_indices.find(value.field_name);
                    if (field_it == (*owner_layout)->field_indices.end()) {
                        return std::unexpected("backend could not resolve projected field '"
                                               + value.field_name + "'");
                    }
                    const ResolvedType field_type = resolved_type_from_field((*owner_layout)->fields[field_it->second]);
                    if (field_type.source_name() != (*dest_type)->source_name()) {
                        return std::unexpected("backend type mismatch for projected field '" + value.field_name
                                               + "': expected '" + (*dest_type)->source_name() + "', got '"
                                               + field_type.source_name() + "'");
                    }
                    return BackendStepSucceeded{};
                    },
                    [&](mir::Rvalue::ProjectNamedVariantPayloadFieldValue value) -> BackendStepResult {
                    const std::expected<const ResolvedType*, std::string> base_type
                        = local_type(value.base_local, statement_context);
                    if (!base_type.has_value()) {
                        return std::unexpected(base_type.error());
                    }
                    if ((*base_type)->identity().category() != ResolvedTypeCategory::PackageTypeDeclaration) {
                        return std::unexpected("backend expected a user-defined base for projection in function '"
                                               + hir_function.qualified_name + "'");
                    }
                    const std::expected<const TypeLayout*, std::string> owner_layout
                        = model.ensure_type_layout(value.projection_owner_type_id);
                    if (!owner_layout.has_value()) {
                        return std::unexpected(owner_layout.error());
                    }
                    const auto variant_it = (*owner_layout)->variants.find(value.variant_id);
                    if (variant_it == (*owner_layout)->variants.end()) {
                        return std::unexpected("backend could not resolve projection variant for field '"
                                               + value.field_name + "'");
                    }
                    const auto field_it = variant_it->second.field_indices.find(value.field_name);
                    if (field_it == variant_it->second.field_indices.end()) {
                        return std::unexpected("backend could not resolve projected field '"
                                               + value.field_name + "'");
                    }
                    const ResolvedType field_type
                        = resolved_type_from_field(variant_it->second.fields[field_it->second]);
                    if (field_type.source_name() != (*dest_type)->source_name()) {
                        return std::unexpected("backend type mismatch for projected field '" + value.field_name
                                               + "': expected '" + (*dest_type)->source_name() + "', got '"
                                               + field_type.source_name() + "'");
                    }
                    return BackendStepSucceeded{};
                    },
                    [&](mir::Rvalue::ProjectListElementValue value) -> BackendStepResult {
                    const std::expected<const ResolvedType*, std::string> list_type
                        = local_type(value.list_local, statement_context);
                    if (!list_type.has_value()) {
                        return std::unexpected(list_type.error());
                    }
                    if ((*list_type)->identity().category() != ResolvedTypeCategory::BuiltinType
                        || ((*list_type)->identity().builtin_kind() != BuiltinKind::List
                            && (*list_type)->identity().builtin_kind() != BuiltinKind::NonEmptyList)) {
                        return std::unexpected("backend expected List<T> or NonEmptyList<T> for traversal in function '"
                                               + hir_function.qualified_name + "', got '"
                                               + (*list_type)->source_name() + "'");
                    }
                    const std::expected<const ResolvedType*, std::string> index_type
                        = local_type(value.index_local, statement_context);
                    if (!index_type.has_value()) {
                        return std::unexpected(index_type.error());
                    }
                    if ((*index_type)->identity().category() != ResolvedTypeCategory::BuiltinType
                        || (*index_type)->identity().builtin_kind() != BuiltinKind::Nat) {
                        return std::unexpected("backend expected Nat traversal index in function '"
                                               + hir_function.qualified_name + "', got '"
                                               + (*index_type)->source_name() + "'");
                    }
                    return BackendStepSucceeded{};
                    },
                    [&](mir::Rvalue::AddNatValue value) -> BackendStepResult {
                    if ((*dest_type)->identity().category() != ResolvedTypeCategory::BuiltinType
                        || (*dest_type)->identity().builtin_kind() != BuiltinKind::Nat) {
                        return std::unexpected("backend expected Nat destination for addition in function '"
                                               + hir_function.qualified_name + "', got '"
                                               + (*dest_type)->source_name() + "'");
                    }
                    if (const BackendStepResult valid = validate_operand(
                            value.lhs,
                            *(*dest_type),
                            "left operand of Nat addition in function '" + hir_function.qualified_name + "'");
                        !valid.has_value()) {
                        return std::unexpected(valid.error());
                    }
                    if (const BackendStepResult valid = validate_operand(
                            value.rhs,
                            *(*dest_type),
                            "right operand of Nat addition in function '" + hir_function.qualified_name + "'");
                        !valid.has_value()) {
                        return std::unexpected(valid.error());
                    }
                    return BackendStepSucceeded{};
                    });
                if (!statement_valid.has_value()) {
                    return std::unexpected(statement_valid.error());
                }
            }

            const mir::Terminator& terminator = block.terminator();
            const BackendStepResult terminator_valid = terminator.match(
                [&](mir::Terminator::ReturnValue terminator) -> BackendStepResult {
                const std::expected<ResolvedType, std::string> return_type = resolve_materializable_type(
                    hir_function.return_type.text,
                    "return type of function '" + hir_function.qualified_name + "'");
                if (!return_type.has_value()) {
                    return std::unexpected(return_type.error());
                }
                const BackendStepResult valid = validate_operand(
                    terminator.value,
                    *return_type,
                    "return from function '" + hir_function.qualified_name + "'");
                if (!valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                return BackendStepSucceeded{};
                },
                [&](mir::Terminator::FailValue terminator) -> BackendStepResult {
                if (hir_function.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
                    return std::unexpected("internal backend error: `fail` terminator in non-yielding function '"
                                           + hir_function.qualified_name + "'");
                }
                const hir::TypeDecl& reason_type = model.type(hir_function.failure.reason_type_id());
                const std::expected<ResolvedType, std::string> resolved_reason = resolve_materializable_type(
                    reason_type.qualified_name,
                    "fail reason of function '" + hir_function.qualified_name + "'");
                if (!resolved_reason.has_value()) {
                    return std::unexpected(resolved_reason.error());
                }
                const BackendStepResult valid = validate_operand(
                    terminator.reason,
                    *resolved_reason,
                    "fail from function '" + hir_function.qualified_name + "'");
                if (!valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                return BackendStepSucceeded{};
                },
                [&](mir::Terminator::GotoValue terminator) -> BackendStepResult {
                const BackendStepResult valid = validate_block_target(
                    terminator.target_block,
                    "function '" + hir_function.qualified_name + "'");
                if (!valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                return BackendStepSucceeded{};
                },
                [&](mir::Terminator::SwitchVariantValue terminator) -> BackendStepResult {
                const std::expected<const ResolvedType*, std::string> scrutinee_type
                    = local_type(terminator.scrutinee_local, "switch in function '" + hir_function.qualified_name + "'");
                if (!scrutinee_type.has_value()) {
                    return std::unexpected(scrutinee_type.error());
                }
                if ((*scrutinee_type)->identity().category() != ResolvedTypeCategory::PackageTypeDeclaration) {
                    return std::unexpected("backend expected a user-defined scrutinee for variant switch in function '"
                                           + hir_function.qualified_name + "'");
                }
                const std::expected<const TypeLayout*, std::string> owner_layout
                    = model.ensure_type_layout((*scrutinee_type)->identity().package_type_id());
                if (!owner_layout.has_value()) {
                    return std::unexpected(owner_layout.error());
                }
                for (const mir::SwitchEdge& edge : terminator.edges) {
                    if ((*owner_layout)->variants.find(edge.variant_id()) == (*owner_layout)->variants.end()) {
                        return std::unexpected("backend could not resolve switched variant id "
                                               + std::to_string(edge.variant_id()) + " in function '"
                                               + hir_function.qualified_name + "'");
                    }
                    const BackendStepResult valid = validate_block_target(
                        edge.target_block(),
                        "switch in function '" + hir_function.qualified_name + "'");
                    if (!valid.has_value()) {
                        return std::unexpected(valid.error());
                    }
                }
                return BackendStepSucceeded{};
                },
                [&](mir::Terminator::BranchListElementValue terminator) -> BackendStepResult {
                const std::expected<const ResolvedType*, std::string> list_type
                    = local_type(terminator.list_local, "traverse source in function '" + hir_function.qualified_name + "'");
                if (!list_type.has_value()) {
                    return std::unexpected(list_type.error());
                }
                if ((*list_type)->identity().category() != ResolvedTypeCategory::BuiltinType
                    || ((*list_type)->identity().builtin_kind() != BuiltinKind::List
                        && (*list_type)->identity().builtin_kind() != BuiltinKind::NonEmptyList)) {
                    return std::unexpected("backend expected List<T> or NonEmptyList<T> for traversal branch in function '"
                                           + hir_function.qualified_name + "', got '"
                                           + (*list_type)->source_name() + "'");
                }
                const std::expected<const ResolvedType*, std::string> index_type
                    = local_type(terminator.index_local, "traverse index in function '" + hir_function.qualified_name + "'");
                if (!index_type.has_value()) {
                    return std::unexpected(index_type.error());
                }
                if ((*index_type)->identity().category() != ResolvedTypeCategory::BuiltinType
                    || (*index_type)->identity().builtin_kind() != BuiltinKind::Nat) {
                    return std::unexpected("backend expected Nat traversal index in function '"
                                           + hir_function.qualified_name + "', got '"
                                           + (*index_type)->source_name() + "'");
                }
                if (const BackendStepResult valid = validate_block_target(
                        terminator.element_block,
                        "traverse element in function '" + hir_function.qualified_name + "'");
                    !valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                if (const BackendStepResult valid = validate_block_target(
                        terminator.empty_block,
                        "traverse done in function '" + hir_function.qualified_name + "'");
                    !valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                return BackendStepSucceeded{};
                },
                [&](mir::Terminator::InvokeValue terminator) -> BackendStepResult {
                if (terminator.function_id >= hir_package.functions.size()) {
                    return std::unexpected("internal backend error: invoke target id out of range in function '"
                                           + hir_function.qualified_name + "'");
                }
                const hir::FunctionDecl& callee = hir::lookup_function(hir_package, terminator.function_id);
                if (callee.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
                    return std::unexpected("internal backend error: invoke used on non-yielding function '"
                                           + callee.qualified_name + "'");
                }
                if (terminator.args.size() != callee.params.size()) {
                    return std::unexpected("internal backend error: invoke arity mismatch for '"
                                           + callee.qualified_name + "'");
                }
                for (std::size_t index = 0; index < terminator.args.size(); ++index) {
                    if (materialization_of(callee.params[index].type.discipline)
                        == typesys::DisciplineMaterialization::CompileTimeOnly) {
                        if (compile_time_argument_encoding_of(terminator.args[index])
                            != CompileTimeArgumentEncoding::ErasedToUnitOperand) {
                            return std::unexpected("internal backend error: compile-time-only parameter "
                                                   + std::to_string(index) + " of callee '"
                                                   + callee.qualified_name + "' was not erased");
                        }
                        continue;
                    }
                    const std::expected<ResolvedType, std::string> param_type = resolve_materializable_type(
                        callee.params[index].type.text,
                        "parameter " + std::to_string(index) + " of callee '" + callee.qualified_name + "'");
                    if (!param_type.has_value()) {
                        return std::unexpected(param_type.error());
                    }
                    const BackendStepResult valid = validate_operand(
                        terminator.args[index],
                        *param_type,
                        "argument " + std::to_string(index) + " of invoke '" + callee.qualified_name + "'");
                    if (!valid.has_value()) {
                        return std::unexpected(valid.error());
                    }
                }
                const std::expected<ResolvedType, std::string> success_type = resolve_materializable_type(
                    callee.return_type.text,
                    "success result of invoke '" + callee.qualified_name + "'");
                if (!success_type.has_value()) {
                    return std::unexpected(success_type.error());
                }
                const hir::TypeDecl& callee_reason = model.type(callee.failure.reason_type_id());
                const std::expected<ResolvedType, std::string> failure_type = resolve_materializable_type(
                    callee_reason.qualified_name,
                    "failure result of invoke '" + callee.qualified_name + "'");
                if (!failure_type.has_value()) {
                    return std::unexpected(failure_type.error());
                }
                const std::expected<const ResolvedType*, std::string> success_local_type
                    = local_type(terminator.success_local, "invoke success in function '" + hir_function.qualified_name + "'");
                if (!success_local_type.has_value()) {
                    return std::unexpected(success_local_type.error());
                }
                const std::expected<const ResolvedType*, std::string> failure_local_type
                    = local_type(terminator.failure_local, "invoke failure in function '" + hir_function.qualified_name + "'");
                if (!failure_local_type.has_value()) {
                    return std::unexpected(failure_local_type.error());
                }
                if ((*success_local_type)->source_name() != success_type->source_name()) {
                    return std::unexpected("backend type mismatch for invoke success local in function '"
                                           + hir_function.qualified_name + "': expected '" + success_type->source_name()
                                           + "', got '" + (*success_local_type)->source_name() + "'");
                }
                if ((*failure_local_type)->source_name() != failure_type->source_name()) {
                    return std::unexpected("backend type mismatch for invoke failure local in function '"
                                           + hir_function.qualified_name + "': expected '" + failure_type->source_name()
                                           + "', got '" + (*failure_local_type)->source_name() + "'");
                }
                if (const BackendStepResult valid = validate_block_target(
                        terminator.success_block,
                        "invoke success in function '" + hir_function.qualified_name + "'");
                    !valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                if (const BackendStepResult valid = validate_block_target(
                        terminator.failure_block,
                        "invoke failure in function '" + hir_function.qualified_name + "'");
                    !valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                return BackendStepSucceeded{};
                },
                [&](mir::Terminator::UnreachableValue) -> BackendStepResult {
                    return BackendStepSucceeded{};
                });
            if (!terminator_valid.has_value()) {
                return std::unexpected(terminator_valid.error());
            }
        }
    }

    return BackendStepSucceeded{};
}

std::string emit_nat_runtime_helper_definitions() {
    std::ostringstream out;
    out << "define { ptr, i64 } @evid.nat.from.u64(i64 %value) {\n";
    out << "entry:\n";
    out << "  %buffer = call ptr @malloc(i64 20)\n";
    out << "  %is.zero = icmp eq i64 %value, 0\n";
    out << "  br i1 %is.zero, label %zero, label %loop\n";
    out << "zero:\n";
    out << "  store i8 48, ptr %buffer\n";
    out << "  %zero.ptr = insertvalue { ptr, i64 } zeroinitializer, ptr %buffer, 0\n";
    out << "  %zero.value = insertvalue { ptr, i64 } %zero.ptr, i64 1, 1\n";
    out << "  ret { ptr, i64 } %zero.value\n";
    out << "loop:\n";
    out << "  %current = phi i64 [ %value, %entry ], [ %next.value, %loop ]\n";
    out << "  %pos = phi i64 [ 20, %entry ], [ %next.pos, %loop ]\n";
    out << "  %digit = urem i64 %current, 10\n";
    out << "  %digit8 = trunc i64 %digit to i8\n";
    out << "  %char = add i8 %digit8, 48\n";
    out << "  %next.pos = sub i64 %pos, 1\n";
    out << "  %slot = getelementptr i8, ptr %buffer, i64 %next.pos\n";
    out << "  store i8 %char, ptr %slot\n";
    out << "  %next.value = udiv i64 %current, 10\n";
    out << "  %done = icmp eq i64 %next.value, 0\n";
    out << "  br i1 %done, label %done.block, label %loop\n";
    out << "done.block:\n";
    out << "  %start.ptr = getelementptr i8, ptr %buffer, i64 %next.pos\n";
    out << "  %length = sub i64 20, %next.pos\n";
    out << "  %result.ptr = insertvalue { ptr, i64 } zeroinitializer, ptr %start.ptr, 0\n";
    out << "  %result.value = insertvalue { ptr, i64 } %result.ptr, i64 %length, 1\n";
    out << "  ret { ptr, i64 } %result.value\n";
    out << "}\n\n";

    out << "define i64 @evid.nat.to.u64({ ptr, i64 } %value) {\n";
    out << "entry:\n";
    out << "  %data = extractvalue { ptr, i64 } %value, 0\n";
    out << "  %length = extractvalue { ptr, i64 } %value, 1\n";
    out << "  %empty = icmp eq i64 %length, 0\n";
    out << "  br i1 %empty, label %zero, label %loop\n";
    out << "zero:\n";
    out << "  ret i64 0\n";
    out << "loop:\n";
    out << "  %index = phi i64 [ 0, %entry ], [ %next.index, %advance ]\n";
    out << "  %acc = phi i64 [ 0, %entry ], [ %next.acc, %advance ]\n";
    out << "  %at.end = icmp eq i64 %index, %length\n";
    out << "  br i1 %at.end, label %done, label %digit.block\n";
    out << "digit.block:\n";
    out << "  %digit.ptr = getelementptr i8, ptr %data, i64 %index\n";
    out << "  %digit.char = load i8, ptr %digit.ptr\n";
    out << "  %digit.raw = sub i8 %digit.char, 48\n";
    out << "  %digit = zext i8 %digit.raw to i64\n";
    out << "  %over.high = icmp ugt i64 %acc, 1844674407370955161\n";
    out << "  %over.edge = icmp eq i64 %acc, 1844674407370955161\n";
    out << "  %over.digit = icmp ugt i64 %digit, 5\n";
    out << "  %over.edge.digit = and i1 %over.edge, %over.digit\n";
    out << "  %overflow = or i1 %over.high, %over.edge.digit\n";
    out << "  br i1 %overflow, label %saturated, label %advance\n";
    out << "advance:\n";
    out << "  %scaled = mul i64 %acc, 10\n";
    out << "  %next.acc = add i64 %scaled, %digit\n";
    out << "  %next.index = add i64 %index, 1\n";
    out << "  br label %loop\n";
    out << "saturated:\n";
    out << "  ret i64 -1\n";
    out << "done:\n";
    out << "  ret i64 %acc\n";
    out << "}\n\n";

    out << "define i1 @evid.nat.fits.u64({ ptr, i64 } %value) {\n";
    out << "entry:\n";
    out << "  %data = extractvalue { ptr, i64 } %value, 0\n";
    out << "  %length = extractvalue { ptr, i64 } %value, 1\n";
    out << "  %empty = icmp eq i64 %length, 0\n";
    out << "  br i1 %empty, label %fits, label %loop\n";
    out << "loop:\n";
    out << "  %index = phi i64 [ 0, %entry ], [ %next.index, %advance ]\n";
    out << "  %acc = phi i64 [ 0, %entry ], [ %next.acc, %advance ]\n";
    out << "  %at.end = icmp eq i64 %index, %length\n";
    out << "  br i1 %at.end, label %fits, label %digit.block\n";
    out << "digit.block:\n";
    out << "  %digit.ptr = getelementptr i8, ptr %data, i64 %index\n";
    out << "  %digit.char = load i8, ptr %digit.ptr\n";
    out << "  %digit.raw = sub i8 %digit.char, 48\n";
    out << "  %digit = zext i8 %digit.raw to i64\n";
    out << "  %over.high = icmp ugt i64 %acc, 1844674407370955161\n";
    out << "  %over.edge = icmp eq i64 %acc, 1844674407370955161\n";
    out << "  %over.digit = icmp ugt i64 %digit, 5\n";
    out << "  %over.edge.digit = and i1 %over.edge, %over.digit\n";
    out << "  %overflow = or i1 %over.high, %over.edge.digit\n";
    out << "  br i1 %overflow, label %too.large, label %advance\n";
    out << "advance:\n";
    out << "  %scaled = mul i64 %acc, 10\n";
    out << "  %next.acc = add i64 %scaled, %digit\n";
    out << "  %next.index = add i64 %index, 1\n";
    out << "  br label %loop\n";
    out << "too.large:\n";
    out << "  ret i1 false\n";
    out << "fits:\n";
    out << "  ret i1 true\n";
    out << "}\n\n";

    out << "define { ptr, i64 } @evid.nat.add({ ptr, i64 } %lhs, { ptr, i64 } %rhs) {\n";
    out << "entry:\n";
    out << "  %lhs.data = extractvalue { ptr, i64 } %lhs, 0\n";
    out << "  %lhs.length = extractvalue { ptr, i64 } %lhs, 1\n";
    out << "  %rhs.data = extractvalue { ptr, i64 } %rhs, 0\n";
    out << "  %rhs.length = extractvalue { ptr, i64 } %rhs, 1\n";
    out << "  %lhs.last = sub i64 %lhs.length, 1\n";
    out << "  %rhs.last = sub i64 %rhs.length, 1\n";
    out << "  %lhs.longer = icmp ugt i64 %lhs.length, %rhs.length\n";
    out << "  %max.length = select i1 %lhs.longer, i64 %lhs.length, i64 %rhs.length\n";
    out << "  %raw.length = add i64 %max.length, 1\n";
    out << "  %buffer = call ptr @malloc(i64 %raw.length)\n";
    out << "  %out.last = sub i64 %raw.length, 1\n";
    out << "  br label %loop\n";
    out << "loop:\n";
    out << "  %lhs.index = phi i64 [ %lhs.last, %entry ], [ %next.lhs.index, %store.digit ]\n";
    out << "  %rhs.index = phi i64 [ %rhs.last, %entry ], [ %next.rhs.index, %store.digit ]\n";
    out << "  %out.index = phi i64 [ %out.last, %entry ], [ %next.out.index, %store.digit ]\n";
    out << "  %carry = phi i64 [ 0, %entry ], [ %next.carry, %store.digit ]\n";
    out << "  %lhs.active = icmp sge i64 %lhs.index, 0\n";
    out << "  br i1 %lhs.active, label %lhs.load, label %lhs.zero\n";
    out << "lhs.load:\n";
    out << "  %lhs.ptr = getelementptr i8, ptr %lhs.data, i64 %lhs.index\n";
    out << "  %lhs.char = load i8, ptr %lhs.ptr\n";
    out << "  %lhs.raw = sub i8 %lhs.char, 48\n";
    out << "  %lhs.loaded = zext i8 %lhs.raw to i64\n";
    out << "  br label %lhs.ready\n";
    out << "lhs.zero:\n";
    out << "  br label %lhs.ready\n";
    out << "lhs.ready:\n";
    out << "  %lhs.digit = phi i64 [ %lhs.loaded, %lhs.load ], [ 0, %lhs.zero ]\n";
    out << "  %rhs.active = icmp sge i64 %rhs.index, 0\n";
    out << "  br i1 %rhs.active, label %rhs.load, label %rhs.zero\n";
    out << "rhs.load:\n";
    out << "  %rhs.ptr = getelementptr i8, ptr %rhs.data, i64 %rhs.index\n";
    out << "  %rhs.char = load i8, ptr %rhs.ptr\n";
    out << "  %rhs.raw = sub i8 %rhs.char, 48\n";
    out << "  %rhs.loaded = zext i8 %rhs.raw to i64\n";
    out << "  br label %rhs.ready\n";
    out << "rhs.zero:\n";
    out << "  br label %rhs.ready\n";
    out << "rhs.ready:\n";
    out << "  %rhs.digit = phi i64 [ %rhs.loaded, %rhs.load ], [ 0, %rhs.zero ]\n";
    out << "  %partial = add i64 %lhs.digit, %rhs.digit\n";
    out << "  %total = add i64 %partial, %carry\n";
    out << "  %digit = urem i64 %total, 10\n";
    out << "  %next.carry = udiv i64 %total, 10\n";
    out << "  %digit8 = trunc i64 %digit to i8\n";
    out << "  %char = add i8 %digit8, 48\n";
    out << "  %out.ptr = getelementptr i8, ptr %buffer, i64 %out.index\n";
    out << "  store i8 %char, ptr %out.ptr\n";
    out << "  %next.lhs.index = sub i64 %lhs.index, 1\n";
    out << "  %next.rhs.index = sub i64 %rhs.index, 1\n";
    out << "  %next.out.index = sub i64 %out.index, 1\n";
    out << "  %lhs.more = icmp sge i64 %next.lhs.index, 0\n";
    out << "  %rhs.more = icmp sge i64 %next.rhs.index, 0\n";
    out << "  %digits.more = or i1 %lhs.more, %rhs.more\n";
    out << "  %carry.more = icmp ne i64 %next.carry, 0\n";
    out << "  %more = or i1 %digits.more, %carry.more\n";
    out << "  br i1 %more, label %store.digit, label %done\n";
    out << "store.digit:\n";
    out << "  br label %loop\n";
    out << "done:\n";
    out << "  %result.ptr = getelementptr i8, ptr %buffer, i64 %out.index\n";
    out << "  %result.length = sub i64 %raw.length, %out.index\n";
    out << "  %with.ptr = insertvalue { ptr, i64 } zeroinitializer, ptr %result.ptr, 0\n";
    out << "  %result = insertvalue { ptr, i64 } %with.ptr, i64 %result.length, 1\n";
    out << "  ret { ptr, i64 } %result\n";
    out << "}\n\n";

    out << "define i32 @evid.nat.compare({ ptr, i64 } %lhs, { ptr, i64 } %rhs) {\n";
    out << "entry:\n";
    out << "  %lhs.data = extractvalue { ptr, i64 } %lhs, 0\n";
    out << "  %lhs.length = extractvalue { ptr, i64 } %lhs, 1\n";
    out << "  %rhs.data = extractvalue { ptr, i64 } %rhs, 0\n";
    out << "  %rhs.length = extractvalue { ptr, i64 } %rhs, 1\n";
    out << "  %lhs.shorter = icmp ult i64 %lhs.length, %rhs.length\n";
    out << "  br i1 %lhs.shorter, label %less, label %check.longer\n";
    out << "check.longer:\n";
    out << "  %lhs.longer = icmp ugt i64 %lhs.length, %rhs.length\n";
    out << "  br i1 %lhs.longer, label %greater, label %scan\n";
    out << "scan:\n";
    out << "  %index = phi i64 [ 0, %check.longer ], [ %next.index, %equal.byte ]\n";
    out << "  %at.end = icmp eq i64 %index, %lhs.length\n";
    out << "  br i1 %at.end, label %equal, label %compare.byte\n";
    out << "compare.byte:\n";
    out << "  %lhs.ptr = getelementptr i8, ptr %lhs.data, i64 %index\n";
    out << "  %rhs.ptr = getelementptr i8, ptr %rhs.data, i64 %index\n";
    out << "  %lhs.byte = load i8, ptr %lhs.ptr\n";
    out << "  %rhs.byte = load i8, ptr %rhs.ptr\n";
    out << "  %byte.equal = icmp eq i8 %lhs.byte, %rhs.byte\n";
    out << "  br i1 %byte.equal, label %equal.byte, label %order.byte\n";
    out << "equal.byte:\n";
    out << "  %next.index = add i64 %index, 1\n";
    out << "  br label %scan\n";
    out << "order.byte:\n";
    out << "  %byte.less = icmp ult i8 %lhs.byte, %rhs.byte\n";
    out << "  br i1 %byte.less, label %less, label %greater\n";
    out << "less:\n";
    out << "  ret i32 -1\n";
    out << "equal:\n";
    out << "  ret i32 0\n";
    out << "greater:\n";
    out << "  ret i32 1\n";
    out << "}\n";
    return out.str();
}

std::string emit_utf8_runtime_helper_definitions() {
    std::ostringstream out;
    out << "define i1 @evid.utf8.is.valid(ptr %data, i64 %length) {\n";
    out << "entry:\n";
    out << "  br label %loop\n";
    out << "loop:\n";
    out << "  %index = phi i64 [ 0, %entry ], [ %next1, %advance1 ], [ %next2, %advance2 ], [ %next3, %advance3 ], [ %next4, %advance4 ]\n";
    out << "  %at.end = icmp eq i64 %index, %length\n";
    out << "  br i1 %at.end, label %valid, label %load\n";
    out << "load:\n";
    out << "  %ptr = getelementptr i8, ptr %data, i64 %index\n";
    out << "  %lead = load i8, ptr %ptr\n";
    out << "  %lead32 = zext i8 %lead to i32\n";
    out << "  %ascii = icmp ult i32 %lead32, 128\n";
    out << "  br i1 %ascii, label %advance1, label %class2\n";
    out << "class2:\n";
    out << "  %two.min = icmp uge i32 %lead32, 194\n";
    out << "  %two.max = icmp ule i32 %lead32, 223\n";
    out << "  %is.two = and i1 %two.min, %two.max\n";
    out << "  br i1 %is.two, label %check2.bounds, label %class3\n";
    out << "check2.bounds:\n";
    out << "  %end2 = add i64 %index, 1\n";
    out << "  %has2 = icmp ult i64 %end2, %length\n";
    out << "  br i1 %has2, label %check2.byte, label %invalid\n";
    out << "check2.byte:\n";
    out << "  %b1.ptr = getelementptr i8, ptr %data, i64 %end2\n";
    out << "  %b1 = load i8, ptr %b1.ptr\n";
    out << "  %b1.32 = zext i8 %b1 to i32\n";
    out << "  %b1.min = icmp uge i32 %b1.32, 128\n";
    out << "  %b1.max = icmp ule i32 %b1.32, 191\n";
    out << "  %b1.ok = and i1 %b1.min, %b1.max\n";
    out << "  br i1 %b1.ok, label %advance2, label %invalid\n";
    out << "class3:\n";
    out << "  %three.min = icmp uge i32 %lead32, 224\n";
    out << "  %three.max = icmp ule i32 %lead32, 239\n";
    out << "  %is.three = and i1 %three.min, %three.max\n";
    out << "  br i1 %is.three, label %check3.bounds, label %class4\n";
    out << "check3.bounds:\n";
    out << "  %end3 = add i64 %index, 2\n";
    out << "  %has3 = icmp ult i64 %end3, %length\n";
    out << "  br i1 %has3, label %check3.bytes, label %invalid\n";
    out << "check3.bytes:\n";
    out << "  %b31.index = add i64 %index, 1\n";
    out << "  %b32.index = add i64 %index, 2\n";
    out << "  %b31.ptr = getelementptr i8, ptr %data, i64 %b31.index\n";
    out << "  %b32.ptr = getelementptr i8, ptr %data, i64 %b32.index\n";
    out << "  %b31 = load i8, ptr %b31.ptr\n";
    out << "  %b32 = load i8, ptr %b32.ptr\n";
    out << "  %b31.32 = zext i8 %b31 to i32\n";
    out << "  %b32.32 = zext i8 %b32 to i32\n";
    out << "  %b31.cont.min = icmp uge i32 %b31.32, 128\n";
    out << "  %b31.cont.max = icmp ule i32 %b31.32, 191\n";
    out << "  %b31.cont = and i1 %b31.cont.min, %b31.cont.max\n";
    out << "  %b32.cont.min = icmp uge i32 %b32.32, 128\n";
    out << "  %b32.cont.max = icmp ule i32 %b32.32, 191\n";
    out << "  %b32.cont = and i1 %b32.cont.min, %b32.cont.max\n";
    out << "  %is.e0 = icmp eq i32 %lead32, 224\n";
    out << "  %e0.second = icmp uge i32 %b31.32, 160\n";
    out << "  %not.e0 = xor i1 %is.e0, true\n";
    out << "  %e0.ok = or i1 %not.e0, %e0.second\n";
    out << "  %is.ed = icmp eq i32 %lead32, 237\n";
    out << "  %ed.second = icmp ule i32 %b31.32, 159\n";
    out << "  %not.ed = xor i1 %is.ed, true\n";
    out << "  %ed.ok = or i1 %not.ed, %ed.second\n";
    out << "  %first3.ok0 = and i1 %b31.cont, %e0.ok\n";
    out << "  %first3.ok = and i1 %first3.ok0, %ed.ok\n";
    out << "  %three.ok = and i1 %first3.ok, %b32.cont\n";
    out << "  br i1 %three.ok, label %advance3, label %invalid\n";
    out << "class4:\n";
    out << "  %four.min = icmp uge i32 %lead32, 240\n";
    out << "  %four.max = icmp ule i32 %lead32, 244\n";
    out << "  %is.four = and i1 %four.min, %four.max\n";
    out << "  br i1 %is.four, label %check4.bounds, label %invalid\n";
    out << "check4.bounds:\n";
    out << "  %end4 = add i64 %index, 3\n";
    out << "  %has4 = icmp ult i64 %end4, %length\n";
    out << "  br i1 %has4, label %check4.bytes, label %invalid\n";
    out << "check4.bytes:\n";
    out << "  %b41.index = add i64 %index, 1\n";
    out << "  %b42.index = add i64 %index, 2\n";
    out << "  %b43.index = add i64 %index, 3\n";
    out << "  %b41.ptr = getelementptr i8, ptr %data, i64 %b41.index\n";
    out << "  %b42.ptr = getelementptr i8, ptr %data, i64 %b42.index\n";
    out << "  %b43.ptr = getelementptr i8, ptr %data, i64 %b43.index\n";
    out << "  %b41 = load i8, ptr %b41.ptr\n";
    out << "  %b42 = load i8, ptr %b42.ptr\n";
    out << "  %b43 = load i8, ptr %b43.ptr\n";
    out << "  %b41.32 = zext i8 %b41 to i32\n";
    out << "  %b42.32 = zext i8 %b42 to i32\n";
    out << "  %b43.32 = zext i8 %b43 to i32\n";
    out << "  %b41.cont.min = icmp uge i32 %b41.32, 128\n";
    out << "  %b41.cont.max = icmp ule i32 %b41.32, 191\n";
    out << "  %b41.cont = and i1 %b41.cont.min, %b41.cont.max\n";
    out << "  %b42.cont.min = icmp uge i32 %b42.32, 128\n";
    out << "  %b42.cont.max = icmp ule i32 %b42.32, 191\n";
    out << "  %b42.cont = and i1 %b42.cont.min, %b42.cont.max\n";
    out << "  %b43.cont.min = icmp uge i32 %b43.32, 128\n";
    out << "  %b43.cont.max = icmp ule i32 %b43.32, 191\n";
    out << "  %b43.cont = and i1 %b43.cont.min, %b43.cont.max\n";
    out << "  %is.f0 = icmp eq i32 %lead32, 240\n";
    out << "  %f0.second = icmp uge i32 %b41.32, 144\n";
    out << "  %not.f0 = xor i1 %is.f0, true\n";
    out << "  %f0.ok = or i1 %not.f0, %f0.second\n";
    out << "  %is.f4 = icmp eq i32 %lead32, 244\n";
    out << "  %f4.second = icmp ule i32 %b41.32, 143\n";
    out << "  %not.f4 = xor i1 %is.f4, true\n";
    out << "  %f4.ok = or i1 %not.f4, %f4.second\n";
    out << "  %first4.ok0 = and i1 %b41.cont, %f0.ok\n";
    out << "  %first4.ok = and i1 %first4.ok0, %f4.ok\n";
    out << "  %tail4.ok0 = and i1 %b42.cont, %b43.cont\n";
    out << "  %four.ok = and i1 %first4.ok, %tail4.ok0\n";
    out << "  br i1 %four.ok, label %advance4, label %invalid\n";
    out << "advance1:\n";
    out << "  %next1 = add i64 %index, 1\n";
    out << "  br label %loop\n";
    out << "advance2:\n";
    out << "  %next2 = add i64 %index, 2\n";
    out << "  br label %loop\n";
    out << "advance3:\n";
    out << "  %next3 = add i64 %index, 3\n";
    out << "  br label %loop\n";
    out << "advance4:\n";
    out << "  %next4 = add i64 %index, 4\n";
    out << "  br label %loop\n";
    out << "invalid:\n";
    out << "  ret i1 false\n";
    out << "valid:\n";
    out << "  ret i1 true\n";
    out << "}\n";
    return out.str();
}

std::expected<std::string, std::string> ModuleEmitter::emit() {
    const hir::FunctionDecl* entry_main = nullptr;
    if (entry_point_emission_ == EntryPointEmission::IncludeExecutableEntryPoint) {
        const std::expected<const hir::FunctionDecl*, std::string> validated_main = model_.validate_entry_main();
        if (!validated_main.has_value()) {
            return std::unexpected(validated_main.error());
        }
        entry_main = *validated_main;
    }

    std::vector<std::string> function_chunks;
    function_chunks.reserve(mir_package_.function_count() + (entry_main != nullptr ? 1 : 0));
    for (const mir::Function& mir_function : mir_package_.functions()) {
        const hir::FunctionDecl& hir_function = hir::lookup_function(hir_package_, mir_function.function_id());
        if (!hir_function.generics.empty()) {
            return std::unexpected("backend does not yet support generic function '"
                                   + hir_function.qualified_name + "'");
        }

        FunctionEmitter emitter(*this, model_, hir_function, mir_function);
        std::expected<std::string, std::string> function_ir = emitter.emit();
        if (!function_ir.has_value()) {
            return std::unexpected(function_ir.error());
        }
        function_chunks.push_back(std::move(*function_ir));
    }

    if (entry_main != nullptr) {
        function_chunks.push_back(emit_entry_wrapper(*entry_main));
    }

    std::ostringstream out;
    out << "; Generated by evidc\n";
    out << "target triple = \"" << target_triple_ << "\"\n\n";

    for (const std::string& definition : model_.type_definitions()) {
        out << definition << '\n';
    }
    if (!model_.type_definitions().empty()) {
        out << '\n';
    }

    for (const std::string& definition : model_.yield_definitions()) {
        out << definition << '\n';
    }
    if (!model_.yield_definitions().empty()) {
        out << '\n';
    }

    for (const StringGlobal& global : string_globals_) {
        out << '@' << global.name << " = private unnamed_addr constant ["
            << (global.bytes.size() + 1) << " x i8] c\"" << encode_llvm_bytes(global.bytes)
            << "\", align 1\n";
    }
    if (!string_globals_.empty()) {
        out << '\n';
    }

    for (const RuntimeHelper helper : runtime_helpers_) {
        switch (helper) {
        case RuntimeHelper::Malloc:
            out << "declare ptr @malloc(i64)\n";
            break;
        case RuntimeHelper::Memcpy:
            out << "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)\n";
            break;
        case RuntimeHelper::Memcmp:
            out << "declare i32 @memcmp(ptr, ptr, i64)\n";
            break;
        case RuntimeHelper::Strlen:
            out << "declare i64 @strlen(ptr)\n";
            break;
        case RuntimeHelper::Utf8:
            out << emit_utf8_runtime_helper_definitions();
            break;
        case RuntimeHelper::Nat:
            out << emit_nat_runtime_helper_definitions();
            break;
        }
    }
    if (!runtime_helpers_.empty()) {
        out << '\n';
    }

    for (std::size_t index = 0; index < function_chunks.size(); ++index) {
        const std::string& function_chunk = function_chunks[index];
        out << function_chunk;
        if (!function_chunk.empty() && function_chunk.back() != '\n') {
            out << '\n';
        }
        if (index + 1 < function_chunks.size()) {
            out << '\n';
        }
    }

    return out.str();
}

std::expected<StringGlobal, std::string> ModuleEmitter::intern_string(const std::string& lexeme) {
    const std::expected<std::string, std::string> decoded = decode_string_literal(lexeme);
    if (!decoded.has_value()) {
        return std::unexpected(decoded.error());
    }
    return intern_bytes(*decoded);
}

StringGlobal ModuleEmitter::intern_bytes(std::string bytes) {
    if (const auto it = string_indices_.find(bytes); it != string_indices_.end()) {
        return string_globals_.at(it->second);
    }

    const std::size_t index = string_globals_.size();
    string_indices_.emplace(bytes, index);
    string_globals_.push_back(StringGlobal{"evid.str." + std::to_string(index), std::move(bytes)});
    return string_globals_.back();
}

void ModuleEmitter::require_runtime_helper(RuntimeHelper helper) {
    if (helper == RuntimeHelper::Nat) {
        require_runtime_helper(RuntimeHelper::Malloc);
    }
    if (std::find(runtime_helpers_.begin(), runtime_helpers_.end(), helper) != runtime_helpers_.end()) {
        return;
    }
    runtime_helpers_.push_back(helper);
}

std::string ModuleEmitter::emit_entry_wrapper(const hir::FunctionDecl& source_main) const {
    std::ostringstream out;
    out << "define i32 @main() {\n";
    out << "entry:\n";
    out << "  %entry.result = call i64 @" << model_.function_symbol(source_main.id) << "()\n";
    out << "  %entry.exit = trunc i64 %entry.result to i32\n";
    out << "  ret i32 %entry.exit\n";
    out << "}\n";
    return out.str();
}

FunctionEmitter::FunctionEmitter(ModuleEmitter& module,
                                 BackendModel& model,
                                 const hir::FunctionDecl& hir_function,
                                 const mir::Function& mir_function)
    : module_(module),
      model_(model),
      hir_function_(hir_function),
      mir_function_(mir_function),
      block_preludes_(mir_function.blocks().size()) {}

BackendStepResult FunctionEmitter::prepare_locals() {
    for (const mir::Local& local : mir_function_.locals()) {
        const std::expected<ResolvedType, std::string> resolved = model_.resolve_type_name(local.type_name());
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        local_types_.emplace(local.id(), *resolved);
        if (materialization_of(local.discipline()) != typesys::DisciplineMaterialization::CompileTimeOnly) {
            local_slots_.emplace(local.id(), "%slot" + std::to_string(local.id()));
        }
    }
    return BackendStepSucceeded{};
}

std::string FunctionEmitter::next_temp(std::string_view prefix) {
    return "%" + std::string(prefix) + std::to_string(temp_counter_++);
}

std::string FunctionEmitter::next_aux_block(std::string_view prefix) {
    return "bb." + std::string(prefix) + "." + std::to_string(aux_block_counter_++);
}

std::string FunctionEmitter::block_name(mir::BlockId id) {
    return "bb" + std::to_string(id);
}

void FunctionEmitter::emit_instruction(std::ostringstream& out, const std::string& text) const {
    out << "  " << text << '\n';
}

void FunctionEmitter::emit_block_lines(std::ostringstream& out, const std::vector<std::string>& lines) const {
    for (const std::string& line : lines) {
        emit_instruction(out, line);
    }
}

void FunctionEmitter::append_line(const std::string& text) {
    current_block_lines_.push_back(text);
}

std::expected<std::string, std::string> FunctionEmitter::local_slot(mir::LocalId local_id) const {
    if (const auto it = local_slots_.find(local_id); it != local_slots_.end()) {
        return it->second;
    }
    for (const mir::Local& local : mir_function_.locals()) {
        if (local.id() == local_id
            && materialization_of(local.discipline()) == typesys::DisciplineMaterialization::CompileTimeOnly) {
            return std::unexpected("internal backend error: compile-time-only local %"
                                   + std::to_string(local_id) + " has no runtime storage");
        }
    }
    return std::unexpected("internal backend error: unknown local %" + std::to_string(local_id));
}

std::expected<const ResolvedType*, std::string> FunctionEmitter::local_type(mir::LocalId local_id) const {
    if (const auto it = local_types_.find(local_id); it != local_types_.end()) {
        return &it->second;
    }
    return std::unexpected("internal backend error: unknown local type for %" + std::to_string(local_id));
}

std::expected<const TypeLayout*, std::string> FunctionEmitter::user_layout_for_local(mir::LocalId local_id) {
    const std::expected<const ResolvedType*, std::string> resolved = local_type(local_id);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }
    if ((*resolved)->identity().category() == ResolvedTypeCategory::PackageTypeDeclaration) {
        return model_.ensure_type_layout((*resolved)->identity().package_type_id());
    }
    if ((*resolved)->identity().category() == ResolvedTypeCategory::BackendAggregateStorage) {
        return model_.ensure_backend_aggregate_layout((*resolved)->source_name());
    }
    return std::unexpected("backend expected an aggregate type for local %" + std::to_string(local_id)
                           + ", got '" + (*resolved)->source_name() + "'");
}

BackendStepResult FunctionEmitter::store_typed_value(const std::string& slot_ptr,
                                                     const ResolvedType& type,
                                                     const std::string& value) {
    if (type.materialization() == MaterializationState::NeverValue) {
        return std::unexpected("backend attempted to materialize `Never`");
    }
    append_line("store " + type.llvm_type() + " " + value + ", ptr " + slot_ptr);
    return BackendStepSucceeded{};
}

std::expected<std::string, std::string> FunctionEmitter::load_from_slot(const std::string& slot_ptr,
                                                                        const ResolvedType& type) {
    if (type.materialization() == MaterializationState::NeverValue) {
        return std::unexpected("backend attempted to load a `Never` value");
    }
    const std::string value = next_temp("load");
    append_line(value + " = load " + type.llvm_type() + ", ptr " + slot_ptr);
    return value;
}

BackendStepResult FunctionEmitter::store_string_literal(const std::string& slot_ptr,
                                                        const ResolvedType& type,
                                                        const std::string& lexeme) {
    const std::expected<TypedValue, std::string> literal_value = make_string_literal_value(type, lexeme);
    if (!literal_value.has_value()) {
        return std::unexpected(literal_value.error());
    }
    return store_typed_value(slot_ptr, literal_value->type, literal_value->value);
}

std::expected<FunctionEmitter::TypedValue, std::string> FunctionEmitter::make_string_literal_value(
    const ResolvedType& type,
    const std::string& lexeme) {
    if (type.identity().category() != ResolvedTypeCategory::BuiltinType
        || (type.identity().builtin_kind() != BuiltinKind::Text
            && type.identity().builtin_kind() != BuiltinKind::NonEmptyText
            && type.identity().builtin_kind() != BuiltinKind::Bytes
            && type.identity().builtin_kind() != BuiltinKind::NonEmptyBytes
            && type.identity().builtin_kind() != BuiltinKind::CString)) {
        return std::unexpected("backend expected Text/NonEmptyText/Bytes/NonEmptyBytes/CString storage for string "
                               "literal, got '" + type.source_name() + "'");
    }

    const std::expected<StringGlobal, std::string> global = module_.intern_string(lexeme);
    if (!global.has_value()) {
        return std::unexpected(global.error());
    }
    if (type.identity().category() == ResolvedTypeCategory::BuiltinType
        && type.identity().builtin_kind() == BuiltinKind::CString
        && global->bytes.find('\0') != std::string::npos) {
        return std::unexpected("backend received CString literal containing U+0000");
    }

    const std::string ptr_value = next_temp("strptr");
    append_line(ptr_value + " = getelementptr inbounds ["
                + std::to_string(global->bytes.size() + 1) + " x i8], ptr @" + global->name
                + ", i64 0, i64 0");

    if (type.identity().category() == ResolvedTypeCategory::BuiltinType
        && type.identity().builtin_kind() == BuiltinKind::CString) {
        return TypedValue{type, ptr_value};
    }

    const std::expected<std::string, std::string> with_ptr = insert_value(
        type.llvm_type(),
        "zeroinitializer",
        "ptr",
        ptr_value,
        0);
    if (!with_ptr.has_value()) {
        return std::unexpected(with_ptr.error());
    }
    const std::expected<std::string, std::string> with_len = insert_value(
        type.llvm_type(),
        *with_ptr,
        "i64",
        std::to_string(global->bytes.size()),
        1);
    if (!with_len.has_value()) {
        return std::unexpected(with_len.error());
    }
    return TypedValue{type, *with_len};
}

std::expected<FunctionEmitter::TypedValue, std::string> FunctionEmitter::make_nat_literal_value(
    const ResolvedType& type,
    std::string_view lexeme) {
    if (!is_builtin_nat_type(type)) {
        return std::unexpected("backend expected Nat storage for Nat literal, got '" + type.source_name() + "'");
    }
    if (classify_decimal_literal_shape(lexeme) != DecimalLiteralShape::PlainDecimalDigits) {
        return std::unexpected("backend received invalid Nat literal '" + std::string(lexeme) + "'");
    }

    module_.require_runtime_helper(RuntimeHelper::Nat);
    const StringGlobal global = module_.intern_bytes(canonical_nat_literal_text(lexeme));
    const std::string ptr_value = next_temp("natptr");
    append_line(ptr_value + " = getelementptr inbounds ["
                + std::to_string(global.bytes.size() + 1) + " x i8], ptr @" + global.name
                + ", i64 0, i64 0");

    const std::expected<std::string, std::string> with_ptr = insert_value(
        type.llvm_type(),
        "zeroinitializer",
        "ptr",
        ptr_value,
        0);
    if (!with_ptr.has_value()) {
        return std::unexpected(with_ptr.error());
    }
    const std::expected<std::string, std::string> with_len = insert_value(
        type.llvm_type(),
        *with_ptr,
        "i64",
        std::to_string(global.bytes.size()),
        1);
    if (!with_len.has_value()) {
        return std::unexpected(with_len.error());
    }
    return TypedValue{type, *with_len};
}

std::expected<std::string, std::string> FunctionEmitter::convert_nat_to_u64(const std::string& value) {
    module_.require_runtime_helper(RuntimeHelper::Nat);
    const std::string converted = next_temp("nat.u64");
    append_line(converted + " = call i64 @evid.nat.to.u64({ ptr, i64 } " + value + ")");
    return converted;
}

std::expected<std::string, std::string> FunctionEmitter::insert_value(const std::string& aggregate_type,
                                                                      const std::string& aggregate_value,
                                                                      const std::string& element_type,
                                                                      const std::string& element_value,
                                                                      std::size_t field_index) {
    const std::string inserted = next_temp("ins");
    append_line(inserted + " = insertvalue " + aggregate_type + " " + aggregate_value + ", "
                + element_type + " " + element_value + ", " + std::to_string(field_index));
    return inserted;
}

std::expected<std::string, std::string> FunctionEmitter::extract_value(const std::string& aggregate_type,
                                                                       const std::string& aggregate_value,
                                                                       std::size_t field_index) {
    const std::string extracted = next_temp("ext");
    append_line(extracted + " = extractvalue " + aggregate_type + " " + aggregate_value + ", "
                + std::to_string(field_index));
    return extracted;
}

std::expected<std::string, std::string> FunctionEmitter::pack_value_for_storage(const TypedValue& value,
                                                                                const std::string& storage_type,
                                                                                std::size_t storage_size) {
    if (value.type.materialization() == MaterializationState::NeverValue) {
        return std::unexpected("backend attempted to materialize `Never`");
    }
    if (storage_size == 0 || value.type.size() == 0) {
        return std::string("zeroinitializer");
    }
    if (value.type.llvm_type() == storage_type) {
        return value.value;
    }

    const std::string storage_slot = next_temp("pack.slot");
    append_line(storage_slot + " = alloca " + storage_type);
    append_line("store " + storage_type + " zeroinitializer, ptr " + storage_slot);
    append_line("store " + value.type.llvm_type() + " " + value.value + ", ptr " + storage_slot);

    const std::string packed = next_temp("pack");
    append_line(packed + " = load " + storage_type + ", ptr " + storage_slot);
    return packed;
}

std::expected<FunctionEmitter::TypedValue, std::string> FunctionEmitter::unpack_value_from_storage(
    const std::string& storage_value,
    const std::string& storage_type,
    const ResolvedType& expected_type) {
    if (expected_type.materialization() == MaterializationState::NeverValue) {
        return std::unexpected("backend attempted to materialize `Never`");
    }
    if (expected_type.size() == 0) {
        return TypedValue{expected_type, zero_value(expected_type)};
    }
    if (expected_type.llvm_type() == storage_type) {
        return TypedValue{expected_type, storage_value};
    }

    const std::string storage_slot = next_temp("unpack.slot");
    append_line(storage_slot + " = alloca " + storage_type);
    append_line("store " + storage_type + " " + storage_value + ", ptr " + storage_slot);
    const std::string unpacked = next_temp("unpack");
    append_line(unpacked + " = load " + expected_type.llvm_type() + ", ptr " + storage_slot);
    return TypedValue{expected_type, unpacked};
}

std::string FunctionEmitter::zero_value(const ResolvedType& type) const {
    if (type.storage_shape() != RuntimeStorageShape::Scalar) {
        return "zeroinitializer";
    }
    if (type.identity().category() == ResolvedTypeCategory::BuiltinType
        && type.identity().builtin_kind() == BuiltinKind::Float) {
        return "0.000000e+00";
    }
    return "0";
}

std::expected<std::vector<ResolvedType>, std::string> FunctionEmitter::resolve_compiler_owned_parameter_types() {
    std::vector<ResolvedType> param_types;
    param_types.reserve(hir_function_.params.size());
    for (const hir::Parameter& param : hir_function_.params) {
        if (materialization_of(param.type.discipline) == typesys::DisciplineMaterialization::CompileTimeOnly) {
            return std::unexpected("internal backend error: compiler-owned function '"
                                   + hir_function_.qualified_name + "' has compile-time-only parameter '"
                                   + param.name + "'");
        }
        const std::expected<ResolvedType, std::string> param_type = model_.resolve_type_name(param.type.text);
        if (!param_type.has_value()) {
            return std::unexpected(param_type.error());
        }
        param_types.push_back(*param_type);
    }
    return param_types;
}

std::expected<std::string, std::string> FunctionEmitter::emit_compiler_owned_function(
    const ResolvedType& return_type) {
    const CompilerOwnedFunctionLowering lowering = compiler_owned_function_lowering(hir_function_.qualified_name);
    if (lowering == CompilerOwnedFunctionLowering::Unsupported) {
        return std::unexpected("backend does not yet support compiler-owned function '"
                               + hir_function_.qualified_name + "'");
    }

    const std::expected<std::vector<ResolvedType>, std::string> param_types_result =
        resolve_compiler_owned_parameter_types();
    if (!param_types_result.has_value()) {
        return std::unexpected(param_types_result.error());
    }
    const std::vector<ResolvedType>& param_types = *param_types_result;
    const auto has_builtin_kind = [](const ResolvedType& type, BuiltinKind kind) {
        return type.identity().category() == ResolvedTypeCategory::BuiltinType
            && type.identity().builtin_kind() == kind;
    };
    const auto element_storage_stride = [](const ResolvedType& type) {
        return std::max<std::size_t>(align_up(type.size(), type.align()), 1);
    };
    const auto resolve_collection_element_type =
        [&](const hir::TypeRef& collection_type,
            std::string_view context) -> std::expected<ResolvedType, std::string> {
        if (collection_type.args.size() != 1) {
            return std::unexpected("internal backend error: " + std::string(context)
                                   + " expected one collection element type argument");
        }
        const std::expected<ResolvedType, std::string> element_type =
            model_.resolve_type_name(collection_type.args[0].text);
        if (!element_type.has_value()) {
            return std::unexpected(element_type.error());
        }
        if (element_type->materialization() == MaterializationState::NeverValue) {
            return std::unexpected("internal backend error: " + std::string(context) + " stores `Never`");
        }
        return *element_type;
    };

    const std::string linkage = hir_function_.visibility == ast::Visibility::Public ? "" : "internal ";
    std::ostringstream out;
    switch (lowering) {
    case CompilerOwnedFunctionLowering::EmptyCollection:
        if (!param_types.empty()) {
            return std::unexpected("internal backend error: empty collection function '"
                                   + hir_function_.qualified_name + "' unexpectedly has parameters");
        }
        if (!has_builtin_kind(return_type, BuiltinKind::List) && !has_builtin_kind(return_type, BuiltinKind::Map)) {
            return std::unexpected("internal backend error: empty collection function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "() {\n";
        out << "entry:\n";
        out << "  ret " << return_type.llvm_type() << " " << zero_value(return_type) << "\n";
        out << "}\n";
        return out.str();

    case CompilerOwnedFunctionLowering::WidenCollection:
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: collection widen function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!((has_builtin_kind(return_type, BuiltinKind::List)
               && has_builtin_kind(param_types[0], BuiltinKind::NonEmptyList))
              || (has_builtin_kind(return_type, BuiltinKind::Map)
                  && has_builtin_kind(param_types[0], BuiltinKind::NonEmptyMap)))) {
            return std::unexpected("internal backend error: collection widen function '"
                                   + hir_function_.qualified_name + "' has unsupported signature");
        }
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  ret " << return_type.llvm_type() << " %arg0\n";
        out << "}\n";
        return out.str();

    case CompilerOwnedFunctionLowering::CountCollection:
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: collection count function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!has_builtin_kind(return_type, BuiltinKind::Nat)) {
            return std::unexpected("internal backend error: collection count function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }
        if (!has_builtin_kind(param_types[0], BuiltinKind::List)
            && !has_builtin_kind(param_types[0], BuiltinKind::NonEmptyList)
            && !has_builtin_kind(param_types[0], BuiltinKind::Map)
            && !has_builtin_kind(param_types[0], BuiltinKind::NonEmptyMap)) {
            return std::unexpected("internal backend error: collection count function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        module_.require_runtime_helper(RuntimeHelper::Nat);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %count1 = call " << return_type.llvm_type() << " @evid.nat.from.u64(i64 %count0)\n";
        out << "  ret " << return_type.llvm_type() << " %count1\n";
        out << "}\n";
        return out.str();

    case CompilerOwnedFunctionLowering::SequenceLength: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: sequence length function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!has_builtin_kind(return_type, BuiltinKind::Nat)) {
            return std::unexpected("internal backend error: sequence length function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }
        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const BuiltinKind expected_kind = base_name == "text_length" ? BuiltinKind::Text
            : base_name == "nonempty_text_length"                 ? BuiltinKind::NonEmptyText
            : base_name == "nonempty_bytes_length"                ? BuiltinKind::NonEmptyBytes
                                                                  : BuiltinKind::Bytes;
        const std::string_view expected_name = base_name == "text_length" ? "Text"
            : base_name == "nonempty_text_length"                  ? "NonEmptyText"
            : base_name == "nonempty_bytes_length"                 ? "NonEmptyBytes"
                                                                   : "Bytes";
        if (!has_builtin_kind(param_types[0], expected_kind)) {
            return std::unexpected("internal backend error: sequence length function '"
                                   + hir_function_.qualified_name + "' expected parameter type '"
                                   + std::string(expected_name) + "', got '"
                                   + param_types[0].source_name() + "'");
        }
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        module_.require_runtime_helper(RuntimeHelper::Nat);
        out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %byte_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        if (expected_kind == BuiltinKind::Bytes || expected_kind == BuiltinKind::NonEmptyBytes) {
            out << "  %byte_count1 = call " << return_type.llvm_type() << " @evid.nat.from.u64(i64 %byte_count0)\n";
            out << "  ret " << return_type.llvm_type() << " %byte_count1\n";
            out << "}\n";
            return out.str();
        }
        out << "  br label %scan\n";
        out << "\n";
        out << "scan:\n";
        out << "  %byte_index0 = phi i64 [ 0, %entry ], [ %next_byte0, %advance ]\n";
        out << "  %scalar_count0 = phi i64 [ 0, %entry ], [ %next_scalar0, %advance ]\n";
        out << "  %at_end0 = icmp eq i64 %byte_index0, %byte_count0\n";
        out << "  br i1 %at_end0, label %done, label %advance\n";
        out << "\n";
        out << "advance:\n";
        out << "  %scalar_ptr0 = getelementptr i8, ptr %data0, i64 %byte_index0\n";
        out << "  %lead0 = load i8, ptr %scalar_ptr0\n";
        out << "  %lead32_0 = zext i8 %lead0 to i32\n";
        out << "  %is_ascii0 = icmp ult i32 %lead32_0, 128\n";
        out << "  %is_two0 = icmp ult i32 %lead32_0, 224\n";
        out << "  %is_three0 = icmp ult i32 %lead32_0, 240\n";
        out << "  %three_or_four0 = select i1 %is_three0, i64 3, i64 4\n";
        out << "  %non_ascii_len0 = select i1 %is_two0, i64 2, i64 %three_or_four0\n";
        out << "  %scalar_len0 = select i1 %is_ascii0, i64 1, i64 %non_ascii_len0\n";
        out << "  %next_byte0 = add i64 %byte_index0, %scalar_len0\n";
        out << "  %next_scalar0 = add i64 %scalar_count0, 1\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "done:\n";
        out << "  %scalar_count1 = call " << return_type.llvm_type() << " @evid.nat.from.u64(i64 %scalar_count0)\n";
        out << "  ret " << return_type.llvm_type() << " %scalar_count1\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::TextCharacterAt:
    case CompilerOwnedFunctionLowering::BytesByteAt:
    case CompilerOwnedFunctionLowering::TextSlice:
    case CompilerOwnedFunctionLowering::BytesSlice: {
        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const std::size_t expected_param_count =
            (base_name == "text_slice" || base_name == "bytes_slice") ? 3 : 2;
        if (param_types.size() != expected_param_count) {
            return std::unexpected("internal backend error: sequence bounds function '"
                                   + hir_function_.qualified_name + "' expected "
                                   + std::to_string(expected_param_count) + " parameters");
        }

        const BuiltinKind expected_sequence_kind =
            (base_name == "text_character_at" || base_name == "text_slice") ? BuiltinKind::Text : BuiltinKind::Bytes;
        const std::string_view expected_sequence_name =
            expected_sequence_kind == BuiltinKind::Text ? "Text" : "Bytes";
        if (!has_builtin_kind(param_types[0], expected_sequence_kind)) {
            return std::unexpected("internal backend error: sequence bounds function '"
                                   + hir_function_.qualified_name + "' expected first parameter type '"
                                   + std::string(expected_sequence_name) + "', got '"
                                   + param_types[0].source_name() + "'");
        }
        for (std::size_t index = 1; index < param_types.size(); ++index) {
            if (!has_builtin_kind(param_types[index], BuiltinKind::Nat)) {
                return std::unexpected("internal backend error: sequence bounds function '"
                                       + hir_function_.qualified_name + "' expected Nat parameter "
                                       + std::to_string(index) + ", got '"
                                       + param_types[index].source_name() + "'");
            }
        }
        if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
            return std::unexpected("internal backend error: sequence bounds function '"
                                   + hir_function_.qualified_name + "' does not declare fails");
        }

        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        const BuiltinKind expected_success_kind =
            base_name == "text_character_at" ? BuiltinKind::Char
            : base_name == "bytes_byte_at"  ? BuiltinKind::Byte
            : expected_sequence_kind;
        if (!has_builtin_kind(*success_type, expected_success_kind)) {
            return std::unexpected("internal backend error: sequence bounds function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "'");
        }

        const std::expected<const YieldLayout*, std::string> yield_layout_result =
            model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout_result.has_value()) {
            return std::unexpected(yield_layout_result.error());
        }
        const YieldLayout& yield_layout = **yield_layout_result;
        if (return_type.llvm_type() != yield_layout.llvm_type) {
            return std::unexpected("internal backend error: sequence bounds function '"
                                   + hir_function_.qualified_name + "' has mismatched yield return type");
        }

        const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
        const std::string_view expected_reason_name =
            expected_sequence_kind == BuiltinKind::Text ? "TextBoundsFailure" : "BytesBoundsFailure";
        if (reason_decl.qualified_name != expected_reason_name) {
            return std::unexpected("internal backend error: sequence bounds function '"
                                   + hir_function_.qualified_name + "' fails with '"
                                   + reason_decl.qualified_name + "'");
        }
        const std::expected<ResolvedType, std::string> failure_type =
            model_.resolve_type_name(reason_decl.qualified_name);
        if (!failure_type.has_value()) {
            return std::unexpected(failure_type.error());
        }
        const std::expected<const TypeLayout*, std::string> failure_layout_result =
            model_.ensure_type_layout(reason_decl.id);
        if (!failure_layout_result.has_value()) {
            return std::unexpected(failure_layout_result.error());
        }
        const TypeLayout& failure_layout = **failure_layout_result;
        const std::string_view failure_variant_name =
            base_name == "text_character_at" ? "RequestedCharacterIndexOutOfRange"
            : base_name == "bytes_byte_at"  ? "RequestedByteIndexOutOfRange"
            : base_name == "text_slice"     ? "RequestedTextSliceOutOfRange"
                                             : "RequestedBytesSliceOutOfRange";
        const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
            [&]() -> std::expected<std::uint32_t, std::string> {
            for (const hir::VariantId variant_id : reason_decl.variants) {
                const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                if (variant_decl.name != failure_variant_name) {
                    continue;
                }
                const auto layout_it = failure_layout.variants.find(variant_id);
                if (layout_it == failure_layout.variants.end()) {
                    return std::unexpected("internal backend error: missing layout for failure variant '"
                                           + variant_decl.qualified_name + "'");
                }
                return layout_it->second.tag_value;
            }
            return std::unexpected("internal backend error: sequence bounds function '"
                                   + hir_function_.qualified_name + "' could not find failure variant '"
                                   + std::string(failure_variant_name) + "'");
        }();
        if (!failure_variant_tag_result.has_value()) {
            return std::unexpected(failure_variant_tag_result.error());
        }
        const std::uint32_t failure_variant_tag = *failure_variant_tag_result;

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout.llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout.payload_size == 0) {
                out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout.payload_storage_type << "\n";
            out << "  store " << yield_layout.payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout.payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout.llvm_type
                << " %" << prefix << ".tag, " << yield_layout.payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout.payload_field_index << "\n";
            out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".value\n";
        };
        const auto emit_utf8_advance =
            [&](std::string_view byte_index,
                std::string_view byte_count,
                std::string_view next_byte,
                std::string_view next_scalar,
                std::string_view prefix) {
            out << "  %" << prefix << ".ptr = getelementptr i8, ptr %data0, i64 %" << byte_index << "\n";
            out << "  %" << prefix << ".lead = load i8, ptr %" << prefix << ".ptr\n";
            out << "  %" << prefix << ".lead32 = zext i8 %" << prefix << ".lead to i32\n";
            out << "  %" << prefix << ".ascii = icmp ult i32 %" << prefix << ".lead32, 128\n";
            out << "  %" << prefix << ".two = icmp ult i32 %" << prefix << ".lead32, 224\n";
            out << "  %" << prefix << ".three = icmp ult i32 %" << prefix << ".lead32, 240\n";
            out << "  %" << prefix << ".three_or_four = select i1 %" << prefix << ".three, i64 3, i64 4\n";
            out << "  %" << prefix << ".non_ascii_len = select i1 %" << prefix << ".two, i64 2, i64 %"
                << prefix << ".three_or_four\n";
            out << "  %" << prefix << ".scalar_len = select i1 %" << prefix << ".ascii, i64 1, i64 %"
                << prefix << ".non_ascii_len\n";
            out << "  %" << next_byte << " = add i64 %" << byte_index << ", %" << prefix << ".scalar_len\n";
            out << "  %" << next_scalar << " = add i64 %" << byte_count << ", 1\n";
        };
        const auto emit_failure_return = [&] {
            out << "  %reason0 = insertvalue " << failure_type->llvm_type()
                << " zeroinitializer, i32 " << failure_variant_tag << ", 0\n";
            emit_yield_return("fail", "1", *failure_type, "%reason0");
        };

        module_.require_runtime_helper(RuntimeHelper::Nat);
        out << "define " << linkage << yield_layout.llvm_type << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1";
        if (expected_param_count == 3) {
            out << ", " << param_types[2].llvm_type() << " %arg2";
        }
        out << ") {\n";
        out << "entry:\n";
        out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %byte_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %index1_0 = call i64 @evid.nat.to.u64(" << param_types[1].llvm_type() << " %arg1)\n";
        if (expected_param_count == 3) {
            out << "  %index2_0 = call i64 @evid.nat.to.u64(" << param_types[2].llvm_type() << " %arg2)\n";
        }

        if (base_name == "bytes_byte_at") {
            out << "  %in_bounds0 = icmp ult i64 %index1_0, %byte_count0\n";
            out << "  br i1 %in_bounds0, label %found, label %bounds_failure\n";
            out << "\n";
            out << "found:\n";
            out << "  %byte_ptr0 = getelementptr i8, ptr %data0, i64 %index1_0\n";
            out << "  %byte0 = load i8, ptr %byte_ptr0\n";
            emit_yield_return("ok", "0", *success_type, "%byte0");
            out << "\n";
            out << "bounds_failure:\n";
            emit_failure_return();
            out << "}\n";
            return out.str();
        }

        if (base_name == "bytes_slice") {
            out << "  %order_ok0 = icmp ule i64 %index1_0, %index2_0\n";
            out << "  %end_ok0 = icmp ule i64 %index2_0, %byte_count0\n";
            out << "  %bounds_ok0 = and i1 %order_ok0, %end_ok0\n";
            out << "  br i1 %bounds_ok0, label %slice_ok, label %bounds_failure\n";
            out << "\n";
            out << "slice_ok:\n";
            out << "  %slice_ptr0 = getelementptr i8, ptr %data0, i64 %index1_0\n";
            out << "  %slice_len0 = sub i64 %index2_0, %index1_0\n";
            out << "  %slice0 = insertvalue " << success_type->llvm_type()
                << " zeroinitializer, ptr %slice_ptr0, 0\n";
            out << "  %slice1 = insertvalue " << success_type->llvm_type()
                << " %slice0, i64 %slice_len0, 1\n";
            emit_yield_return("ok", "0", *success_type, "%slice1");
            out << "\n";
            out << "bounds_failure:\n";
            emit_failure_return();
            out << "}\n";
            return out.str();
        }

        if (base_name == "text_character_at") {
            out << "  br label %scan\n";
            out << "\n";
            out << "scan:\n";
            out << "  %byte_index0 = phi i64 [ 0, %entry ], [ %next_byte0, %advance ]\n";
            out << "  %scalar_index0 = phi i64 [ 0, %entry ], [ %next_scalar0, %advance ]\n";
            out << "  %at_end0 = icmp eq i64 %byte_index0, %byte_count0\n";
            out << "  br i1 %at_end0, label %bounds_failure, label %probe\n";
            out << "\n";
            out << "probe:\n";
            out << "  %index_match0 = icmp eq i64 %scalar_index0, %index1_0\n";
            out << "  br i1 %index_match0, label %decode, label %advance\n";
            out << "\n";
            out << "advance:\n";
            emit_utf8_advance("byte_index0", "scalar_index0", "next_byte0", "next_scalar0", "advance0");
            out << "  br label %scan\n";
            out << "\n";
            out << "decode:\n";
            out << "  %decode_ptr0 = getelementptr i8, ptr %data0, i64 %byte_index0\n";
            out << "  %decode_lead0 = load i8, ptr %decode_ptr0\n";
            out << "  %decode_lead32_0 = zext i8 %decode_lead0 to i32\n";
            out << "  %decode_ascii0 = icmp ult i32 %decode_lead32_0, 128\n";
            out << "  br i1 %decode_ascii0, label %decode_one, label %decode_more\n";
            out << "\n";
            out << "decode_one:\n";
            emit_yield_return("ok1", "0", *success_type, "%decode_lead32_0");
            out << "\n";
            out << "decode_more:\n";
            out << "  %decode_two0 = icmp ult i32 %decode_lead32_0, 224\n";
            out << "  br i1 %decode_two0, label %decode_two, label %decode_three_or_four\n";
            out << "\n";
            out << "decode_two:\n";
            out << "  %byte1_index0 = add i64 %byte_index0, 1\n";
            out << "  %byte1_ptr0 = getelementptr i8, ptr %data0, i64 %byte1_index0\n";
            out << "  %byte1_0 = load i8, ptr %byte1_ptr0\n";
            out << "  %byte1_32_0 = zext i8 %byte1_0 to i32\n";
            out << "  %two_head0 = and i32 %decode_lead32_0, 31\n";
            out << "  %two_high0 = shl i32 %two_head0, 6\n";
            out << "  %two_low0 = and i32 %byte1_32_0, 63\n";
            out << "  %scalar_two0 = or i32 %two_high0, %two_low0\n";
            emit_yield_return("ok2", "0", *success_type, "%scalar_two0");
            out << "\n";
            out << "decode_three_or_four:\n";
            out << "  %decode_three0 = icmp ult i32 %decode_lead32_0, 240\n";
            out << "  br i1 %decode_three0, label %decode_three, label %decode_four\n";
            out << "\n";
            out << "decode_three:\n";
            out << "  %three_b1_index0 = add i64 %byte_index0, 1\n";
            out << "  %three_b2_index0 = add i64 %byte_index0, 2\n";
            out << "  %three_b1_ptr0 = getelementptr i8, ptr %data0, i64 %three_b1_index0\n";
            out << "  %three_b2_ptr0 = getelementptr i8, ptr %data0, i64 %three_b2_index0\n";
            out << "  %three_b1_0 = load i8, ptr %three_b1_ptr0\n";
            out << "  %three_b2_0 = load i8, ptr %three_b2_ptr0\n";
            out << "  %three_b1_32_0 = zext i8 %three_b1_0 to i32\n";
            out << "  %three_b2_32_0 = zext i8 %three_b2_0 to i32\n";
            out << "  %three_head0 = and i32 %decode_lead32_0, 15\n";
            out << "  %three_high0 = shl i32 %three_head0, 12\n";
            out << "  %three_mid_raw0 = and i32 %three_b1_32_0, 63\n";
            out << "  %three_mid0 = shl i32 %three_mid_raw0, 6\n";
            out << "  %three_low0 = and i32 %three_b2_32_0, 63\n";
            out << "  %three_join0 = or i32 %three_high0, %three_mid0\n";
            out << "  %scalar_three0 = or i32 %three_join0, %three_low0\n";
            emit_yield_return("ok3", "0", *success_type, "%scalar_three0");
            out << "\n";
            out << "decode_four:\n";
            out << "  %four_b1_index0 = add i64 %byte_index0, 1\n";
            out << "  %four_b2_index0 = add i64 %byte_index0, 2\n";
            out << "  %four_b3_index0 = add i64 %byte_index0, 3\n";
            out << "  %four_b1_ptr0 = getelementptr i8, ptr %data0, i64 %four_b1_index0\n";
            out << "  %four_b2_ptr0 = getelementptr i8, ptr %data0, i64 %four_b2_index0\n";
            out << "  %four_b3_ptr0 = getelementptr i8, ptr %data0, i64 %four_b3_index0\n";
            out << "  %four_b1_0 = load i8, ptr %four_b1_ptr0\n";
            out << "  %four_b2_0 = load i8, ptr %four_b2_ptr0\n";
            out << "  %four_b3_0 = load i8, ptr %four_b3_ptr0\n";
            out << "  %four_b1_32_0 = zext i8 %four_b1_0 to i32\n";
            out << "  %four_b2_32_0 = zext i8 %four_b2_0 to i32\n";
            out << "  %four_b3_32_0 = zext i8 %four_b3_0 to i32\n";
            out << "  %four_head0 = and i32 %decode_lead32_0, 7\n";
            out << "  %four_high0 = shl i32 %four_head0, 18\n";
            out << "  %four_mid_high_raw0 = and i32 %four_b1_32_0, 63\n";
            out << "  %four_mid_high0 = shl i32 %four_mid_high_raw0, 12\n";
            out << "  %four_mid_low_raw0 = and i32 %four_b2_32_0, 63\n";
            out << "  %four_mid_low0 = shl i32 %four_mid_low_raw0, 6\n";
            out << "  %four_low0 = and i32 %four_b3_32_0, 63\n";
            out << "  %four_join0 = or i32 %four_high0, %four_mid_high0\n";
            out << "  %four_join1 = or i32 %four_join0, %four_mid_low0\n";
            out << "  %scalar_four0 = or i32 %four_join1, %four_low0\n";
            emit_yield_return("ok4", "0", *success_type, "%scalar_four0");
            out << "\n";
            out << "bounds_failure:\n";
            emit_failure_return();
            out << "}\n";
            return out.str();
        }

        out << "  %order_ok0 = icmp ule i64 %index1_0, %index2_0\n";
        out << "  br i1 %order_ok0, label %start_scan, label %bounds_failure\n";
        out << "\n";
        out << "start_scan:\n";
        out << "  %start_byte_index0 = phi i64 [ 0, %entry ], [ %next_start_byte0, %start_advance ]\n";
        out << "  %start_scalar_index0 = phi i64 [ 0, %entry ], [ %next_start_scalar0, %start_advance ]\n";
        out << "  %start_match0 = icmp eq i64 %start_scalar_index0, %index1_0\n";
        out << "  br i1 %start_match0, label %end_scan, label %start_check_end\n";
        out << "\n";
        out << "start_check_end:\n";
        out << "  %start_at_end0 = icmp eq i64 %start_byte_index0, %byte_count0\n";
        out << "  br i1 %start_at_end0, label %bounds_failure, label %start_advance\n";
        out << "\n";
        out << "start_advance:\n";
        emit_utf8_advance("start_byte_index0",
                          "start_scalar_index0",
                          "next_start_byte0",
                          "next_start_scalar0",
                          "start0");
        out << "  br label %start_scan\n";
        out << "\n";
        out << "end_scan:\n";
        out << "  %end_byte_index0 = phi i64 [ %start_byte_index0, %start_scan ], [ %next_end_byte0, %end_advance ]\n";
        out << "  %end_scalar_index0 = phi i64 [ %start_scalar_index0, %start_scan ], [ %next_end_scalar0, %end_advance ]\n";
        out << "  %end_match0 = icmp eq i64 %end_scalar_index0, %index2_0\n";
        out << "  br i1 %end_match0, label %slice_ok, label %end_check_end\n";
        out << "\n";
        out << "end_check_end:\n";
        out << "  %end_at_end0 = icmp eq i64 %end_byte_index0, %byte_count0\n";
        out << "  br i1 %end_at_end0, label %bounds_failure, label %end_advance\n";
        out << "\n";
        out << "end_advance:\n";
        emit_utf8_advance("end_byte_index0",
                          "end_scalar_index0",
                          "next_end_byte0",
                          "next_end_scalar0",
                          "end0");
        out << "  br label %end_scan\n";
        out << "\n";
        out << "slice_ok:\n";
        out << "  %slice_ptr0 = getelementptr i8, ptr %data0, i64 %start_byte_index0\n";
        out << "  %slice_len0 = sub i64 %end_byte_index0, %start_byte_index0\n";
        out << "  %slice0 = insertvalue " << success_type->llvm_type()
            << " zeroinitializer, ptr %slice_ptr0, 0\n";
        out << "  %slice1 = insertvalue " << success_type->llvm_type()
            << " %slice0, i64 %slice_len0, 1\n";
        emit_yield_return("ok", "0", *success_type, "%slice1");
        out << "\n";
        out << "bounds_failure:\n";
        emit_failure_return();
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::RequireNonEmptySequence: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: sequence require-nonempty function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
            return std::unexpected("internal backend error: sequence require-nonempty function '"
                                   + hir_function_.qualified_name + "' does not declare fails");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const BuiltinKind expected_param_kind = base_name == "text_require_nonempty"
            ? BuiltinKind::Text
            : BuiltinKind::Bytes;
        const BuiltinKind expected_success_kind = expected_param_kind == BuiltinKind::Text
            ? BuiltinKind::NonEmptyText
            : BuiltinKind::NonEmptyBytes;
        const std::string expected_reason_name = expected_param_kind == BuiltinKind::Text
            ? "TextCardinalityFailure"
            : "BytesCardinalityFailure";
        const std::string expected_failure_variant_name = expected_param_kind == BuiltinKind::Text
            ? "TextHadNoCharacters"
            : "BytesHadNoBytes";
        if (!has_builtin_kind(param_types[0], expected_param_kind)) {
            return std::unexpected("internal backend error: sequence require-nonempty function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        if (!has_builtin_kind(*success_type, expected_success_kind)) {
            return std::unexpected("internal backend error: sequence require-nonempty function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "'");
        }

        const std::expected<const YieldLayout*, std::string> yield_layout_result =
            model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout_result.has_value()) {
            return std::unexpected(yield_layout_result.error());
        }
        const YieldLayout& yield_layout = **yield_layout_result;
        if (return_type.llvm_type() != yield_layout.llvm_type) {
            return std::unexpected("internal backend error: sequence require-nonempty function '"
                                   + hir_function_.qualified_name + "' has mismatched yield return type");
        }

        const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
        if (reason_decl.qualified_name != expected_reason_name) {
            return std::unexpected("internal backend error: sequence require-nonempty function '"
                                   + hir_function_.qualified_name + "' fails with '"
                                   + reason_decl.qualified_name + "'");
        }
        const std::expected<ResolvedType, std::string> failure_type =
            model_.resolve_type_name(reason_decl.qualified_name);
        if (!failure_type.has_value()) {
            return std::unexpected(failure_type.error());
        }
        const std::expected<const TypeLayout*, std::string> failure_layout_result =
            model_.ensure_type_layout(reason_decl.id);
        if (!failure_layout_result.has_value()) {
            return std::unexpected(failure_layout_result.error());
        }
        const TypeLayout& failure_layout = **failure_layout_result;
        const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
            [&]() -> std::expected<std::uint32_t, std::string> {
            for (const hir::VariantId variant_id : reason_decl.variants) {
                const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                if (variant_decl.name != expected_failure_variant_name) {
                    continue;
                }
                const auto layout_it = failure_layout.variants.find(variant_id);
                if (layout_it == failure_layout.variants.end()) {
                    return std::unexpected("internal backend error: missing layout for failure variant '"
                                           + variant_decl.qualified_name + "'");
                }
                return layout_it->second.tag_value;
            }
            return std::unexpected("internal backend error: sequence require-nonempty function '"
                                   + hir_function_.qualified_name + "' could not find failure variant '"
                                   + expected_failure_variant_name + "'");
        }();
        if (!failure_variant_tag_result.has_value()) {
            return std::unexpected(failure_variant_tag_result.error());
        }

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout.llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout.payload_size == 0) {
                out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout.payload_storage_type << "\n";
            out << "  store " << yield_layout.payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout.payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout.llvm_type
                << " %" << prefix << ".tag, " << yield_layout.payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout.payload_field_index << "\n";
            out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".value\n";
        };

        out << "define " << linkage << yield_layout.llvm_type << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %is_empty0 = icmp eq i64 %count0, 0\n";
        out << "  br i1 %is_empty0, label %empty, label %nonempty\n";
        out << "\n";
        out << "nonempty:\n";
        emit_yield_return("ok", "0", *success_type, "%arg0");
        out << "\n";
        out << "empty:\n";
        out << "  %reason0 = insertvalue " << failure_type->llvm_type()
            << " zeroinitializer, i32 " << *failure_variant_tag_result << ", 0\n";
        emit_yield_return("fail", "1", *failure_type, "%reason0");
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::WidenSequence:
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: sequence widen function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!((has_builtin_kind(return_type, BuiltinKind::Text)
               && has_builtin_kind(param_types[0], BuiltinKind::NonEmptyText))
              || (has_builtin_kind(return_type, BuiltinKind::Bytes)
                  && has_builtin_kind(param_types[0], BuiltinKind::NonEmptyBytes)))) {
            return std::unexpected("internal backend error: sequence widen function '"
                                   + hir_function_.qualified_name + "' has unsupported signature");
        }
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  ret " << return_type.llvm_type() << " %arg0\n";
        out << "}\n";
        return out.str();

    case CompilerOwnedFunctionLowering::SequenceFirst: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: sequence first function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const BuiltinKind expected_param_kind = base_name == "nonempty_text_first_character"
            ? BuiltinKind::NonEmptyText
            : BuiltinKind::NonEmptyBytes;
        const BuiltinKind expected_return_kind = expected_param_kind == BuiltinKind::NonEmptyText
            ? BuiltinKind::Char
            : BuiltinKind::Byte;
        if (!has_builtin_kind(param_types[0], expected_param_kind) || !has_builtin_kind(return_type, expected_return_kind)) {
            return std::unexpected("internal backend error: sequence first function '"
                                   + hir_function_.qualified_name + "' has unsupported signature");
        }
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %lead0 = load i8, ptr %data0\n";
        if (expected_param_kind == BuiltinKind::NonEmptyBytes) {
            out << "  ret " << return_type.llvm_type() << " %lead0\n";
            out << "}\n";
            return out.str();
        }
        out << "  %lead32_0 = zext i8 %lead0 to i32\n";
        out << "  %is_ascii0 = icmp ult i32 %lead32_0, 128\n";
        out << "  br i1 %is_ascii0, label %one, label %more\n";
        out << "\n";
        out << "one:\n";
        out << "  ret " << return_type.llvm_type() << " %lead32_0\n";
        out << "\n";
        out << "more:\n";
        out << "  %is_two0 = icmp ult i32 %lead32_0, 224\n";
        out << "  br i1 %is_two0, label %two, label %three_or_four\n";
        out << "\n";
        out << "two:\n";
        out << "  %byte1_ptr0 = getelementptr i8, ptr %data0, i64 1\n";
        out << "  %byte1_0 = load i8, ptr %byte1_ptr0\n";
        out << "  %byte1_32_0 = zext i8 %byte1_0 to i32\n";
        out << "  %two_head0 = and i32 %lead32_0, 31\n";
        out << "  %two_high0 = shl i32 %two_head0, 6\n";
        out << "  %two_low0 = and i32 %byte1_32_0, 63\n";
        out << "  %scalar_two0 = or i32 %two_high0, %two_low0\n";
        out << "  ret " << return_type.llvm_type() << " %scalar_two0\n";
        out << "\n";
        out << "three_or_four:\n";
        out << "  %is_three0 = icmp ult i32 %lead32_0, 240\n";
        out << "  br i1 %is_three0, label %three, label %four\n";
        out << "\n";
        out << "three:\n";
        out << "  %three_b1_ptr0 = getelementptr i8, ptr %data0, i64 1\n";
        out << "  %three_b2_ptr0 = getelementptr i8, ptr %data0, i64 2\n";
        out << "  %three_b1_0 = load i8, ptr %three_b1_ptr0\n";
        out << "  %three_b2_0 = load i8, ptr %three_b2_ptr0\n";
        out << "  %three_b1_32_0 = zext i8 %three_b1_0 to i32\n";
        out << "  %three_b2_32_0 = zext i8 %three_b2_0 to i32\n";
        out << "  %three_head0 = and i32 %lead32_0, 15\n";
        out << "  %three_high0 = shl i32 %three_head0, 12\n";
        out << "  %three_mid_raw0 = and i32 %three_b1_32_0, 63\n";
        out << "  %three_mid0 = shl i32 %three_mid_raw0, 6\n";
        out << "  %three_low0 = and i32 %three_b2_32_0, 63\n";
        out << "  %three_join0 = or i32 %three_high0, %three_mid0\n";
        out << "  %scalar_three0 = or i32 %three_join0, %three_low0\n";
        out << "  ret " << return_type.llvm_type() << " %scalar_three0\n";
        out << "\n";
        out << "four:\n";
        out << "  %four_b1_ptr0 = getelementptr i8, ptr %data0, i64 1\n";
        out << "  %four_b2_ptr0 = getelementptr i8, ptr %data0, i64 2\n";
        out << "  %four_b3_ptr0 = getelementptr i8, ptr %data0, i64 3\n";
        out << "  %four_b1_0 = load i8, ptr %four_b1_ptr0\n";
        out << "  %four_b2_0 = load i8, ptr %four_b2_ptr0\n";
        out << "  %four_b3_0 = load i8, ptr %four_b3_ptr0\n";
        out << "  %four_b1_32_0 = zext i8 %four_b1_0 to i32\n";
        out << "  %four_b2_32_0 = zext i8 %four_b2_0 to i32\n";
        out << "  %four_b3_32_0 = zext i8 %four_b3_0 to i32\n";
        out << "  %four_head0 = and i32 %lead32_0, 7\n";
        out << "  %four_high0 = shl i32 %four_head0, 18\n";
        out << "  %four_mid_high_raw0 = and i32 %four_b1_32_0, 63\n";
        out << "  %four_mid_high0 = shl i32 %four_mid_high_raw0, 12\n";
        out << "  %four_mid_low_raw0 = and i32 %four_b2_32_0, 63\n";
        out << "  %four_mid_low0 = shl i32 %four_mid_low_raw0, 6\n";
        out << "  %four_low0 = and i32 %four_b3_32_0, 63\n";
        out << "  %four_join0 = or i32 %four_high0, %four_mid_high0\n";
        out << "  %four_join1 = or i32 %four_join0, %four_mid_low0\n";
        out << "  %scalar_four0 = or i32 %four_join1, %four_low0\n";
        out << "  ret " << return_type.llvm_type() << " %scalar_four0\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::ListSingle: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: list single function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!has_builtin_kind(return_type, BuiltinKind::NonEmptyList)) {
            return std::unexpected("internal backend error: list single function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }
        if (param_types[0].materialization() == MaterializationState::NeverValue) {
            return std::unexpected("internal backend error: list single function '"
                                   + hir_function_.qualified_name + "' stores `Never`");
        }

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        const std::size_t element_stride = element_storage_stride(param_types[0]);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %data0 = call ptr @malloc(i64 " << element_stride << ")\n";
        out << "  store " << param_types[0].llvm_type() << " %arg0, ptr %data0\n";
        out << "  %carrier0 = insertvalue " << return_type.llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %carrier1 = insertvalue " << return_type.llvm_type() << " %carrier0, i64 1, 1\n";
        out << "  ret " << return_type.llvm_type() << " %carrier1\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::MapSingle: {
        if (param_types.size() != 2) {
            return std::unexpected("internal backend error: map single function '"
                                   + hir_function_.qualified_name + "' expected two parameters");
        }
        if (!has_builtin_kind(return_type, BuiltinKind::NonEmptyMap)) {
            return std::unexpected("internal backend error: map single function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }
        if (param_types[0].materialization() == MaterializationState::NeverValue
            || param_types[1].materialization() == MaterializationState::NeverValue) {
            return std::unexpected("internal backend error: map single function '"
                                   + hir_function_.qualified_name + "' stores `Never`");
        }
        if (hir_function_.params.size() != 2) {
            return std::unexpected("internal backend error: map single function '"
                                   + hir_function_.qualified_name + "' expected two HIR parameters");
        }

        const std::string entry_source_name = "MapEntry<" + hir_function_.params[0].type.text + ", "
            + hir_function_.params[1].type.text + ">";
        const std::expected<ResolvedType, std::string> entry_type = model_.resolve_type_name(entry_source_name);
        if (!entry_type.has_value()) {
            return std::unexpected(entry_type.error());
        }
        const std::expected<const TypeLayout*, std::string> entry_layout =
            model_.ensure_backend_aggregate_layout(entry_source_name);
        if (!entry_layout.has_value()) {
            return std::unexpected(entry_layout.error());
        }
        const auto key_field_it = (*entry_layout)->field_indices.find("key");
        const auto value_field_it = (*entry_layout)->field_indices.find("value");
        if (key_field_it == (*entry_layout)->field_indices.end()
            || value_field_it == (*entry_layout)->field_indices.end()) {
            return std::unexpected("internal backend error: map single entry layout for '"
                                   + entry_source_name + "' is missing expected fields");
        }

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        const std::size_t entry_stride = element_storage_stride(*entry_type);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1) {\n";
        out << "entry:\n";
        out << "  %entry0 = insertvalue " << entry_type->llvm_type() << " zeroinitializer, "
            << param_types[0].llvm_type() << " %arg0, " << key_field_it->second << "\n";
        out << "  %entry1 = insertvalue " << entry_type->llvm_type() << " %entry0, "
            << param_types[1].llvm_type() << " %arg1, " << value_field_it->second << "\n";
        out << "  %data0 = call ptr @malloc(i64 " << entry_stride << ")\n";
        out << "  store " << entry_type->llvm_type() << " %entry1, ptr %data0\n";
        out << "  %carrier0 = insertvalue " << return_type.llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %carrier1 = insertvalue " << return_type.llvm_type() << " %carrier0, i64 1, 1\n";
        out << "  ret " << return_type.llvm_type() << " %carrier1\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::ListFirstCopy:
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: list first-copy function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!has_builtin_kind(param_types[0], BuiltinKind::NonEmptyList)) {
            return std::unexpected("internal backend error: list first-copy function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (return_type.materialization() == MaterializationState::NeverValue) {
            return std::unexpected("internal backend error: list first-copy function '"
                                   + hir_function_.qualified_name + "' returns `Never`");
        }
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %first0 = load " << return_type.llvm_type() << ", ptr %data0\n";
        out << "  ret " << return_type.llvm_type() << " %first0\n";
        out << "}\n";
        return out.str();

    case CompilerOwnedFunctionLowering::MapFirstEntryCopy:
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: map first-entry-copy function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!has_builtin_kind(param_types[0], BuiltinKind::NonEmptyMap)) {
            return std::unexpected("internal backend error: map first-entry-copy function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (return_type.identity().category() != ResolvedTypeCategory::BackendAggregateStorage
            || type_base_name(return_type.source_name()) != "MapEntry") {
            return std::unexpected("internal backend error: map first-entry-copy function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %entry0 = load " << return_type.llvm_type() << ", ptr %data0\n";
        out << "  ret " << return_type.llvm_type() << " %entry0\n";
        out << "}\n";
        return out.str();

    case CompilerOwnedFunctionLowering::MapLookupCopy: {
        if (param_types.size() != 2) {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' expected two parameters");
        }
        if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' does not declare fails");
        }
        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const BuiltinKind expected_param_kind = base_name == "nonempty_map_lookup_copy"
            ? BuiltinKind::NonEmptyMap
            : BuiltinKind::Map;
        if (!has_builtin_kind(param_types[0], expected_param_kind)) {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' has unsupported map parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (hir_function_.params.size() != 2 || hir_function_.params[0].type.args.size() != 2) {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' has malformed HIR parameters");
        }

        const std::string key_source_name = hir_function_.params[0].type.args[0].text;
        const std::string value_source_name = hir_function_.params[0].type.args[1].text;
        if (hir_function_.params[1].type.text != key_source_name) {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' has mismatched key parameter type");
        }

        const std::expected<ResolvedType, std::string> key_type = model_.resolve_type_name(key_source_name);
        if (!key_type.has_value()) {
            return std::unexpected(key_type.error());
        }
        if (key_type->identity().category() != ResolvedTypeCategory::BuiltinType) {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' uses non-builtin key type '"
                                   + key_type->source_name() + "'");
        }

        const BuiltinKind key_kind = key_type->identity().builtin_kind();
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
        case BuiltinKind::Float:
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' uses unsupported key type '"
                                   + key_type->source_name() + "'");
        }

        if (key_kind == BuiltinKind::Text || key_kind == BuiltinKind::Bytes) {
            module_.require_runtime_helper(RuntimeHelper::Memcmp);
        }
        if (key_kind == BuiltinKind::Nat) {
            module_.require_runtime_helper(RuntimeHelper::Nat);
        }

        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        if (success_type->source_name() != value_source_name) {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "' instead of map value type '"
                                   + value_source_name + "'");
        }

        const std::string entry_source_name = "MapEntry<" + key_source_name + ", " + value_source_name + ">";
        const std::expected<ResolvedType, std::string> entry_type = model_.resolve_type_name(entry_source_name);
        if (!entry_type.has_value()) {
            return std::unexpected(entry_type.error());
        }
        const std::expected<const TypeLayout*, std::string> entry_layout_result =
            model_.ensure_backend_aggregate_layout(entry_source_name);
        if (!entry_layout_result.has_value()) {
            return std::unexpected(entry_layout_result.error());
        }
        const TypeLayout& entry_layout = **entry_layout_result;
        const auto key_field_it = entry_layout.field_indices.find("key");
        const auto value_field_it = entry_layout.field_indices.find("value");
        if (key_field_it == entry_layout.field_indices.end()
            || value_field_it == entry_layout.field_indices.end()) {
            return std::unexpected("internal backend error: map lookup-copy entry layout for '"
                                   + entry_source_name + "' is missing expected fields");
        }

        const std::expected<const YieldLayout*, std::string> yield_layout_result =
            model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout_result.has_value()) {
            return std::unexpected(yield_layout_result.error());
        }
        const YieldLayout& yield_layout = **yield_layout_result;
        if (return_type.llvm_type() != yield_layout.llvm_type) {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' has mismatched yield return type");
        }

        const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
        if (reason_decl.qualified_name != "MapBindingFailure") {
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' fails with '"
                                   + reason_decl.qualified_name + "'");
        }
        const std::expected<ResolvedType, std::string> failure_type =
            model_.resolve_type_name(reason_decl.qualified_name);
        if (!failure_type.has_value()) {
            return std::unexpected(failure_type.error());
        }
        const std::expected<const TypeLayout*, std::string> failure_layout_result =
            model_.ensure_type_layout(reason_decl.id);
        if (!failure_layout_result.has_value()) {
            return std::unexpected(failure_layout_result.error());
        }
        const TypeLayout& failure_layout = **failure_layout_result;

        const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
            [&]() -> std::expected<std::uint32_t, std::string> {
            for (const hir::VariantId variant_id : reason_decl.variants) {
                const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                if (variant_decl.name != "RequestedKeyHadNoBinding") {
                    continue;
                }
                const auto layout_it = failure_layout.variants.find(variant_id);
                if (layout_it == failure_layout.variants.end()) {
                    return std::unexpected("internal backend error: missing layout for failure variant '"
                                           + variant_decl.qualified_name + "'");
                }
                return layout_it->second.tag_value;
            }
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name
                                   + "' could not find failure variant 'RequestedKeyHadNoBinding'");
        }();
        if (!failure_variant_tag_result.has_value()) {
            return std::unexpected(failure_variant_tag_result.error());
        }
        const std::uint32_t failure_variant_tag = *failure_variant_tag_result;

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout.llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout.payload_size == 0) {
                out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout.payload_storage_type << "\n";
            out << "  store " << yield_layout.payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout.payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout.llvm_type
                << " %" << prefix << ".tag, " << yield_layout.payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout.payload_field_index << "\n";
            out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".value\n";
        };

        const std::size_t entry_stride = element_storage_stride(*entry_type);
        out << "define " << linkage << yield_layout.llvm_type << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1) {\n";
        out << "entry:\n";
        out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "scan:\n";
        out << "  %index0 = phi i64 [ 0, %entry ], [ %next0, %not_match ]\n";
        out << "  %at_end0 = icmp eq i64 %index0, %count0\n";
        out << "  br i1 %at_end0, label %missing, label %probe\n";
        out << "\n";
        out << "probe:\n";
        out << "  %offset0 = mul i64 %index0, " << entry_stride << "\n";
        out << "  %entry_ptr0 = getelementptr i8, ptr %data0, i64 %offset0\n";
        out << "  %entry0 = load " << entry_type->llvm_type() << ", ptr %entry_ptr0\n";
        out << "  %candidate_key0 = extractvalue " << entry_type->llvm_type()
            << " %entry0, " << key_field_it->second << "\n";
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
            out << "  %key_equal0 = icmp eq " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::Nat:
            out << "  %key_cmp0 = call i32 @evid.nat.compare(" << key_type->llvm_type()
                << " %candidate_key0, " << key_type->llvm_type() << " %arg1)\n";
            out << "  %key_equal0 = icmp eq i32 %key_cmp0, 0\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
            out << "  %candidate_data0 = extractvalue " << key_type->llvm_type() << " %candidate_key0, 0\n";
            out << "  %candidate_count0 = extractvalue " << key_type->llvm_type() << " %candidate_key0, 1\n";
            out << "  %lookup_data0 = extractvalue " << key_type->llvm_type() << " %arg1, 0\n";
            out << "  %lookup_count0 = extractvalue " << key_type->llvm_type() << " %arg1, 1\n";
            out << "  %key_size_equal0 = icmp eq i64 %candidate_count0, %lookup_count0\n";
            out << "  br i1 %key_size_equal0, label %key_bytes_compare, label %not_match\n";
            out << "\n";
            out << "key_bytes_compare:\n";
            out << "  %key_cmp0 = call i32 @memcmp(ptr %candidate_data0, ptr %lookup_data0, i64 %candidate_count0)\n";
            out << "  %key_equal0 = icmp eq i32 %key_cmp0, 0\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::Float:
            out << "  %key_equal0 = fcmp oeq " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map lookup-copy function '"
                                   + hir_function_.qualified_name + "' reached unsupported key lowering");
        }
        out << "\n";
        out << "not_match:\n";
        out << "  %next0 = add i64 %index0, 1\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "found:\n";
        out << "  %value0 = extractvalue " << entry_type->llvm_type()
            << " %entry0, " << value_field_it->second << "\n";
        emit_yield_return("ok", "0", *success_type, "%value0");
        out << "\n";
        out << "missing:\n";
        out << "  %reason0 = insertvalue " << failure_type->llvm_type()
            << " zeroinitializer, i32 " << failure_variant_tag << ", 0\n";
        emit_yield_return("fail", "1", *failure_type, "%reason0");
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::MapBindNew:
    case CompilerOwnedFunctionLowering::MapReplaceBound:
    case CompilerOwnedFunctionLowering::MapBindOrReplace: {
        if (param_types.size() != 3) {
            return std::unexpected("internal backend error: map binding function '"
                                   + hir_function_.qualified_name + "' expected three parameters");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const MapBindingOperation operation = lowering == CompilerOwnedFunctionLowering::MapBindNew
            ? MapBindingOperation::BindNew
            : (lowering == CompilerOwnedFunctionLowering::MapReplaceBound
                   ? MapBindingOperation::ReplaceBound
                   : MapBindingOperation::BindOrReplace);
        const BuiltinKind expected_param_kind = (base_name == "nonempty_map_bind_new"
                                                 || base_name == "nonempty_map_replace_bound"
                                                 || base_name == "nonempty_map_bind_or_replace")
            ? BuiltinKind::NonEmptyMap
            : BuiltinKind::Map;
        if (!has_builtin_kind(param_types[0], expected_param_kind)) {
            return std::unexpected("internal backend error: map binding function '"
                                   + hir_function_.qualified_name + "' has unsupported map parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (hir_function_.params.size() != 3 || hir_function_.params[0].type.args.size() != 2) {
            return std::unexpected("internal backend error: map binding function '"
                                   + hir_function_.qualified_name + "' has malformed HIR parameters");
        }

        const std::string key_source_name = hir_function_.params[0].type.args[0].text;
        const std::string value_source_name = hir_function_.params[0].type.args[1].text;
        if (hir_function_.params[1].type.text != key_source_name
            || hir_function_.params[2].type.text != value_source_name) {
            return std::unexpected("internal backend error: map binding function '"
                                   + hir_function_.qualified_name + "' has mismatched key or value parameter type");
        }

        const std::expected<ResolvedType, std::string> key_type = model_.resolve_type_name(key_source_name);
        if (!key_type.has_value()) {
            return std::unexpected(key_type.error());
        }
        if (key_type->identity().category() != ResolvedTypeCategory::BuiltinType) {
            return std::unexpected("internal backend error: map binding function '"
                                   + hir_function_.qualified_name + "' uses non-builtin key type '"
                                   + key_type->source_name() + "'");
        }

        const BuiltinKind key_kind = key_type->identity().builtin_kind();
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
        case BuiltinKind::Float:
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map binding function '"
                                   + hir_function_.qualified_name + "' uses unsupported key type '"
                                   + key_type->source_name() + "'");
        }

        if (key_kind == BuiltinKind::Text || key_kind == BuiltinKind::Bytes) {
            module_.require_runtime_helper(RuntimeHelper::Memcmp);
        }

        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        if (!has_builtin_kind(*success_type, BuiltinKind::NonEmptyMap)) {
            return std::unexpected("internal backend error: map binding function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "'");
        }

        const std::string entry_source_name = "MapEntry<" + key_source_name + ", " + value_source_name + ">";
        const std::expected<ResolvedType, std::string> entry_type = model_.resolve_type_name(entry_source_name);
        if (!entry_type.has_value()) {
            return std::unexpected(entry_type.error());
        }
        const std::expected<const TypeLayout*, std::string> entry_layout_result =
            model_.ensure_backend_aggregate_layout(entry_source_name);
        if (!entry_layout_result.has_value()) {
            return std::unexpected(entry_layout_result.error());
        }
        const TypeLayout& entry_layout = **entry_layout_result;
        const auto key_field_it = entry_layout.field_indices.find("key");
        const auto value_field_it = entry_layout.field_indices.find("value");
        if (key_field_it == entry_layout.field_indices.end()
            || value_field_it == entry_layout.field_indices.end()) {
            return std::unexpected("internal backend error: map binding entry layout for '"
                                   + entry_source_name + "' is missing expected fields");
        }

        const YieldLayout* yield_layout = nullptr;
        ResolvedType failure_type = ResolvedType::runtime_value("",
                                                                "i8",
                                                                1,
                                                                1,
                                                                RuntimeStorageShape::Scalar,
                                                                ResolvedTypeIdentity::builtin_type(BuiltinKind::Unit));
        std::uint32_t failure_variant_tag = 0;
        if (operation == MapBindingOperation::BindOrReplace) {
            if (hir_function_.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
                return std::unexpected("internal backend error: map bind-or-replace function '"
                                       + hir_function_.qualified_name + "' unexpectedly declares fails");
            }
            if (return_type.llvm_type() != success_type->llvm_type()) {
                return std::unexpected("internal backend error: map bind-or-replace function '"
                                       + hir_function_.qualified_name + "' has mismatched return type");
            }
        } else {
            if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
                return std::unexpected("internal backend error: map binding function '"
                                       + hir_function_.qualified_name + "' does not declare fails");
            }

            const std::expected<const YieldLayout*, std::string> yield_layout_result =
                model_.ensure_yield_layout(hir_function_.id);
            if (!yield_layout_result.has_value()) {
                return std::unexpected(yield_layout_result.error());
            }
            yield_layout = *yield_layout_result;
            if (return_type.llvm_type() != yield_layout->llvm_type) {
                return std::unexpected("internal backend error: map binding function '"
                                       + hir_function_.qualified_name + "' has mismatched yield return type");
            }

            const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
            if (reason_decl.qualified_name != "MapBindingFailure") {
                return std::unexpected("internal backend error: map binding function '"
                                       + hir_function_.qualified_name + "' fails with '"
                                       + reason_decl.qualified_name + "'");
            }
            const std::expected<ResolvedType, std::string> failure_type_result =
                model_.resolve_type_name(reason_decl.qualified_name);
            if (!failure_type_result.has_value()) {
                return std::unexpected(failure_type_result.error());
            }
            failure_type = *failure_type_result;
            const std::expected<const TypeLayout*, std::string> failure_layout_result =
                model_.ensure_type_layout(reason_decl.id);
            if (!failure_layout_result.has_value()) {
                return std::unexpected(failure_layout_result.error());
            }
            const TypeLayout& failure_layout = **failure_layout_result;
            const std::string expected_failure_variant_name =
                operation == MapBindingOperation::BindNew
                ? "RequestedKeyAlreadyHadBinding"
                : "RequestedKeyHadNoBinding";

            const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
                [&]() -> std::expected<std::uint32_t, std::string> {
                for (const hir::VariantId variant_id : reason_decl.variants) {
                    const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                    if (variant_decl.name != expected_failure_variant_name) {
                        continue;
                    }
                    const auto layout_it = failure_layout.variants.find(variant_id);
                    if (layout_it == failure_layout.variants.end()) {
                        return std::unexpected("internal backend error: missing layout for failure variant '"
                                               + variant_decl.qualified_name + "'");
                    }
                    return layout_it->second.tag_value;
                }
                return std::unexpected("internal backend error: map binding function '"
                                       + hir_function_.qualified_name + "' could not find failure variant '"
                                       + expected_failure_variant_name + "'");
            }();
            if (!failure_variant_tag_result.has_value()) {
                return std::unexpected(failure_variant_tag_result.error());
            }
            failure_variant_tag = *failure_variant_tag_result;
        }

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        module_.require_runtime_helper(RuntimeHelper::Memcpy);

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout->llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout->payload_size == 0) {
                out << "  ret " << yield_layout->llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout->payload_storage_type << "\n";
            out << "  store " << yield_layout->payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout->payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout->llvm_type
                << " %" << prefix << ".tag, " << yield_layout->payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout->payload_field_index << "\n";
            out << "  ret " << yield_layout->llvm_type << " %" << prefix << ".value\n";
        };

        const auto emit_success_return =
            [&](const std::string& carrier_value) {
            if (operation == MapBindingOperation::BindOrReplace) {
                out << "  ret " << success_type->llvm_type() << " " << carrier_value << "\n";
                return;
            }
            emit_yield_return("ok", "0", *success_type, carrier_value);
        };

        const auto emit_failure_return = [&](std::string_view prefix) {
            out << "  %reason0 = insertvalue " << failure_type.llvm_type()
                << " zeroinitializer, i32 " << failure_variant_tag << ", 0\n";
            emit_yield_return(prefix, "1", failure_type, "%reason0");
        };

        const std::size_t entry_stride = element_storage_stride(*entry_type);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1, "
            << param_types[2].llvm_type() << " %arg2) {\n";
        out << "entry:\n";
        out << "  %old_data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %old_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %new_entry0 = insertvalue " << entry_type->llvm_type() << " zeroinitializer, "
            << param_types[1].llvm_type() << " %arg1, " << key_field_it->second << "\n";
        out << "  %new_entry1 = insertvalue " << entry_type->llvm_type() << " %new_entry0, "
            << param_types[2].llvm_type() << " %arg2, " << value_field_it->second << "\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "scan:\n";
        out << "  %index0 = phi i64 [ 0, %entry ], [ %next0, %scan_next ]\n";
        out << "  %at_end0 = icmp eq i64 %index0, %old_count0\n";
        out << "  br i1 %at_end0, label %at_end, label %probe\n";
        out << "\n";
        out << "probe:\n";
        out << "  %offset0 = mul i64 %index0, " << entry_stride << "\n";
        out << "  %entry_ptr0 = getelementptr i8, ptr %old_data0, i64 %offset0\n";
        out << "  %entry0 = load " << entry_type->llvm_type() << ", ptr %entry_ptr0\n";
        out << "  %candidate_key0 = extractvalue " << entry_type->llvm_type()
            << " %entry0, " << key_field_it->second << "\n";
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char: {
            const std::string less_predicate = key_kind == BuiltinKind::Int ? "slt" : "ult";
            out << "  %key_equal0 = icmp eq " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %key_equal0, label %duplicate, label %key_not_equal\n";
            out << "\n";
            out << "key_not_equal:\n";
            out << "  %candidate_less0 = icmp " << less_predicate << " "
                << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %candidate_less0, label %scan_next, label %insert_before\n";
            break;
        }
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
            out << "  %candidate_data0 = extractvalue " << key_type->llvm_type() << " %candidate_key0, 0\n";
            out << "  %candidate_count0 = extractvalue " << key_type->llvm_type() << " %candidate_key0, 1\n";
            out << "  %lookup_data0 = extractvalue " << key_type->llvm_type() << " %arg1, 0\n";
            out << "  %lookup_count0 = extractvalue " << key_type->llvm_type() << " %arg1, 1\n";
            out << "  %candidate_shorter0 = icmp ult i64 %candidate_count0, %lookup_count0\n";
            out << "  %min_count0 = select i1 %candidate_shorter0, i64 %candidate_count0, i64 %lookup_count0\n";
            out << "  %min_empty0 = icmp eq i64 %min_count0, 0\n";
            out << "  br i1 %min_empty0, label %key_bytes_equal_prefix, label %key_bytes_compare\n";
            out << "\n";
            out << "key_bytes_compare:\n";
            out << "  %key_cmp0 = call i32 @memcmp(ptr %candidate_data0, ptr %lookup_data0, i64 %min_count0)\n";
            out << "  %bytes_equal0 = icmp eq i32 %key_cmp0, 0\n";
            out << "  br i1 %bytes_equal0, label %key_bytes_equal_prefix, label %key_bytes_ordered\n";
            out << "\n";
            out << "key_bytes_ordered:\n";
            out << "  %candidate_bytes_less0 = icmp slt i32 %key_cmp0, 0\n";
            out << "  br i1 %candidate_bytes_less0, label %scan_next, label %insert_before\n";
            out << "\n";
            out << "key_bytes_equal_prefix:\n";
            out << "  %key_equal0 = icmp eq i64 %candidate_count0, %lookup_count0\n";
            out << "  br i1 %key_equal0, label %duplicate, label %key_prefix_not_equal\n";
            out << "\n";
            out << "key_prefix_not_equal:\n";
            out << "  br i1 %candidate_shorter0, label %scan_next, label %insert_before\n";
            break;
        case BuiltinKind::Float:
            out << "  %key_equal0 = fcmp oeq " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %key_equal0, label %duplicate, label %key_not_equal\n";
            out << "\n";
            out << "key_not_equal:\n";
            out << "  %candidate_less0 = fcmp olt " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %candidate_less0, label %scan_next, label %insert_before\n";
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map binding function '"
                                   + hir_function_.qualified_name + "' reached unsupported key lowering");
        }
        out << "\n";
        out << "scan_next:\n";
        out << "  %next0 = add i64 %index0, 1\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "at_end:\n";
        if (operation == MapBindingOperation::ReplaceBound) {
            out << "  br label %missing\n";
        } else {
            out << "  br label %insert_at_index\n";
        }
        out << "\n";
        out << "insert_before:\n";
        if (operation == MapBindingOperation::ReplaceBound) {
            out << "  br label %missing\n";
        } else {
            out << "  br label %insert_at_index\n";
        }
        out << "\n";
        out << "duplicate:\n";
        if (operation == MapBindingOperation::BindNew) {
            out << "  br label %already_bound\n";
        } else {
            out << "  br label %replace_existing\n";
        }
        out << "\n";

        if (operation != MapBindingOperation::ReplaceBound) {
            out << "insert_at_index:\n";
            out << "  %new_count0 = add i64 %old_count0, 1\n";
            out << "  %prefix_bytes0 = mul i64 %index0, " << entry_stride << "\n";
            out << "  %suffix_count0 = sub i64 %old_count0, %index0\n";
            out << "  %suffix_bytes0 = mul i64 %suffix_count0, " << entry_stride << "\n";
            out << "  %alloc_bytes0 = mul i64 %new_count0, " << entry_stride << "\n";
            out << "  %data0 = call ptr @malloc(i64 %alloc_bytes0)\n";
            out << "  call void @llvm.memcpy.p0.p0.i64(ptr %data0, ptr %old_data0, i64 %prefix_bytes0, i1 false)\n";
            out << "  %new_slot0 = getelementptr i8, ptr %data0, i64 %prefix_bytes0\n";
            out << "  store " << entry_type->llvm_type() << " %new_entry1, ptr %new_slot0\n";
            out << "  %suffix_src0 = getelementptr i8, ptr %old_data0, i64 %prefix_bytes0\n";
            out << "  %suffix_dest0 = getelementptr i8, ptr %new_slot0, i64 " << entry_stride << "\n";
            out << "  call void @llvm.memcpy.p0.p0.i64(ptr %suffix_dest0, ptr %suffix_src0, i64 %suffix_bytes0, i1 false)\n";
            out << "  %carrier0 = insertvalue " << success_type->llvm_type() << " zeroinitializer, ptr %data0, 0\n";
            out << "  %carrier1 = insertvalue " << success_type->llvm_type() << " %carrier0, i64 %new_count0, 1\n";
            emit_success_return("%carrier1");
            out << "\n";
        }

        if (operation != MapBindingOperation::BindNew) {
            out << "replace_existing:\n";
            out << "  %replace_bytes0 = mul i64 %old_count0, " << entry_stride << "\n";
            out << "  %replace_data0 = call ptr @malloc(i64 %replace_bytes0)\n";
            out << "  %replace_prefix_bytes0 = mul i64 %index0, " << entry_stride << "\n";
            out << "  call void @llvm.memcpy.p0.p0.i64(ptr %replace_data0, ptr %old_data0, i64 %replace_prefix_bytes0, i1 false)\n";
            out << "  %replace_slot0 = getelementptr i8, ptr %replace_data0, i64 %replace_prefix_bytes0\n";
            out << "  store " << entry_type->llvm_type() << " %new_entry1, ptr %replace_slot0\n";
            out << "  %after_index0 = add i64 %index0, 1\n";
            out << "  %replace_suffix_count0 = sub i64 %old_count0, %after_index0\n";
            out << "  %replace_suffix_bytes0 = mul i64 %replace_suffix_count0, " << entry_stride << "\n";
            out << "  %replace_suffix_src0 = getelementptr i8, ptr %old_data0, i64 "
                << entry_stride << "\n";
            out << "  %replace_suffix_src1 = getelementptr i8, ptr %replace_suffix_src0, i64 %replace_prefix_bytes0\n";
            out << "  %replace_suffix_dest0 = getelementptr i8, ptr %replace_slot0, i64 " << entry_stride << "\n";
            out << "  call void @llvm.memcpy.p0.p0.i64(ptr %replace_suffix_dest0, ptr %replace_suffix_src1, i64 %replace_suffix_bytes0, i1 false)\n";
            out << "  %replace_carrier0 = insertvalue " << success_type->llvm_type() << " zeroinitializer, ptr %replace_data0, 0\n";
            out << "  %replace_carrier1 = insertvalue " << success_type->llvm_type() << " %replace_carrier0, i64 %old_count0, 1\n";
            emit_success_return("%replace_carrier1");
            out << "\n";
        }

        if (operation == MapBindingOperation::BindNew) {
            out << "already_bound:\n";
            emit_failure_return("fail");
            out << "\n";
        }
        if (operation == MapBindingOperation::ReplaceBound) {
            out << "missing:\n";
            emit_failure_return("fail");
            out << "\n";
        }
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::MapRemoveBound: {
        if (param_types.size() != 2) {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' expected two parameters");
        }
        if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' does not declare fails");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const BuiltinKind expected_param_kind = base_name == "nonempty_map_remove_bound"
            ? BuiltinKind::NonEmptyMap
            : BuiltinKind::Map;
        if (!has_builtin_kind(param_types[0], expected_param_kind)) {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' has unsupported map parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (hir_function_.params.size() != 2 || hir_function_.params[0].type.args.size() != 2) {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' has malformed HIR parameters");
        }

        const std::string key_source_name = hir_function_.params[0].type.args[0].text;
        const std::string value_source_name = hir_function_.params[0].type.args[1].text;
        if (hir_function_.params[1].type.text != key_source_name) {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' has mismatched key parameter type");
        }

        const std::expected<ResolvedType, std::string> key_type = model_.resolve_type_name(key_source_name);
        if (!key_type.has_value()) {
            return std::unexpected(key_type.error());
        }
        if (key_type->identity().category() != ResolvedTypeCategory::BuiltinType) {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' uses non-builtin key type '"
                                   + key_type->source_name() + "'");
        }

        const BuiltinKind key_kind = key_type->identity().builtin_kind();
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
        case BuiltinKind::Float:
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' uses unsupported key type '"
                                   + key_type->source_name() + "'");
        }

        if (key_kind == BuiltinKind::Text || key_kind == BuiltinKind::Bytes) {
            module_.require_runtime_helper(RuntimeHelper::Memcmp);
        }

        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        if (!has_builtin_kind(*success_type, BuiltinKind::Map)) {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "'");
        }

        const std::string entry_source_name = "MapEntry<" + key_source_name + ", " + value_source_name + ">";
        const std::expected<ResolvedType, std::string> entry_type = model_.resolve_type_name(entry_source_name);
        if (!entry_type.has_value()) {
            return std::unexpected(entry_type.error());
        }
        const std::expected<const TypeLayout*, std::string> entry_layout_result =
            model_.ensure_backend_aggregate_layout(entry_source_name);
        if (!entry_layout_result.has_value()) {
            return std::unexpected(entry_layout_result.error());
        }
        const TypeLayout& entry_layout = **entry_layout_result;
        const auto key_field_it = entry_layout.field_indices.find("key");
        if (key_field_it == entry_layout.field_indices.end()) {
            return std::unexpected("internal backend error: map remove-bound entry layout for '"
                                   + entry_source_name + "' is missing expected key field");
        }

        const std::expected<const YieldLayout*, std::string> yield_layout_result =
            model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout_result.has_value()) {
            return std::unexpected(yield_layout_result.error());
        }
        const YieldLayout& yield_layout = **yield_layout_result;
        if (return_type.llvm_type() != yield_layout.llvm_type) {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' has mismatched yield return type");
        }

        const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
        if (reason_decl.qualified_name != "MapBindingFailure") {
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' fails with '"
                                   + reason_decl.qualified_name + "'");
        }
        const std::expected<ResolvedType, std::string> failure_type =
            model_.resolve_type_name(reason_decl.qualified_name);
        if (!failure_type.has_value()) {
            return std::unexpected(failure_type.error());
        }
        const std::expected<const TypeLayout*, std::string> failure_layout_result =
            model_.ensure_type_layout(reason_decl.id);
        if (!failure_layout_result.has_value()) {
            return std::unexpected(failure_layout_result.error());
        }
        const TypeLayout& failure_layout = **failure_layout_result;
        const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
            [&]() -> std::expected<std::uint32_t, std::string> {
            for (const hir::VariantId variant_id : reason_decl.variants) {
                const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                if (variant_decl.name != "RequestedKeyHadNoBinding") {
                    continue;
                }
                const auto layout_it = failure_layout.variants.find(variant_id);
                if (layout_it == failure_layout.variants.end()) {
                    return std::unexpected("internal backend error: missing layout for failure variant '"
                                           + variant_decl.qualified_name + "'");
                }
                return layout_it->second.tag_value;
            }
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name
                                   + "' could not find failure variant 'RequestedKeyHadNoBinding'");
        }();
        if (!failure_variant_tag_result.has_value()) {
            return std::unexpected(failure_variant_tag_result.error());
        }
        const std::uint32_t failure_variant_tag = *failure_variant_tag_result;

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        module_.require_runtime_helper(RuntimeHelper::Memcpy);

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout.llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout.payload_size == 0) {
                out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout.payload_storage_type << "\n";
            out << "  store " << yield_layout.payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout.payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout.llvm_type
                << " %" << prefix << ".tag, " << yield_layout.payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout.payload_field_index << "\n";
            out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".value\n";
        };

        const auto emit_failure_return = [&]() {
            out << "  %reason0 = insertvalue " << failure_type->llvm_type()
                << " zeroinitializer, i32 " << failure_variant_tag << ", 0\n";
            emit_yield_return("fail", "1", *failure_type, "%reason0");
        };

        const std::size_t entry_stride = element_storage_stride(*entry_type);
        out << "define " << linkage << yield_layout.llvm_type << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1) {\n";
        out << "entry:\n";
        out << "  %old_data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %old_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "scan:\n";
        out << "  %index0 = phi i64 [ 0, %entry ], [ %next0, %not_match ]\n";
        out << "  %at_end0 = icmp eq i64 %index0, %old_count0\n";
        out << "  br i1 %at_end0, label %missing, label %probe\n";
        out << "\n";
        out << "probe:\n";
        out << "  %offset0 = mul i64 %index0, " << entry_stride << "\n";
        out << "  %entry_ptr0 = getelementptr i8, ptr %old_data0, i64 %offset0\n";
        out << "  %entry0 = load " << entry_type->llvm_type() << ", ptr %entry_ptr0\n";
        out << "  %candidate_key0 = extractvalue " << entry_type->llvm_type()
            << " %entry0, " << key_field_it->second << "\n";
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
            out << "  %key_equal0 = icmp eq " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
            out << "  %candidate_data0 = extractvalue " << key_type->llvm_type() << " %candidate_key0, 0\n";
            out << "  %candidate_count0 = extractvalue " << key_type->llvm_type() << " %candidate_key0, 1\n";
            out << "  %lookup_data0 = extractvalue " << key_type->llvm_type() << " %arg1, 0\n";
            out << "  %lookup_count0 = extractvalue " << key_type->llvm_type() << " %arg1, 1\n";
            out << "  %key_size_equal0 = icmp eq i64 %candidate_count0, %lookup_count0\n";
            out << "  br i1 %key_size_equal0, label %key_size_equal, label %not_match\n";
            out << "\n";
            out << "key_size_equal:\n";
            out << "  %key_empty0 = icmp eq i64 %candidate_count0, 0\n";
            out << "  br i1 %key_empty0, label %found, label %key_bytes_compare\n";
            out << "\n";
            out << "key_bytes_compare:\n";
            out << "  %key_cmp0 = call i32 @memcmp(ptr %candidate_data0, ptr %lookup_data0, i64 %candidate_count0)\n";
            out << "  %key_equal0 = icmp eq i32 %key_cmp0, 0\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::Float:
            out << "  %key_equal0 = fcmp oeq " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map remove-bound function '"
                                   + hir_function_.qualified_name + "' reached unsupported key lowering");
        }
        out << "\n";
        out << "not_match:\n";
        out << "  %next0 = add i64 %index0, 1\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "found:\n";
        out << "  %new_count0 = sub i64 %old_count0, 1\n";
        out << "  %result_empty0 = icmp eq i64 %new_count0, 0\n";
        out << "  br i1 %result_empty0, label %removed_empty, label %removed_nonempty\n";
        out << "\n";
        out << "removed_nonempty:\n";
        out << "  %prefix_bytes0 = mul i64 %index0, " << entry_stride << "\n";
        out << "  %after_index0 = add i64 %index0, 1\n";
        out << "  %suffix_count0 = sub i64 %old_count0, %after_index0\n";
        out << "  %suffix_bytes0 = mul i64 %suffix_count0, " << entry_stride << "\n";
        out << "  %alloc_bytes0 = mul i64 %new_count0, " << entry_stride << "\n";
        out << "  %data0 = call ptr @malloc(i64 %alloc_bytes0)\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %data0, ptr %old_data0, i64 %prefix_bytes0, i1 false)\n";
        out << "  %suffix_src_offset0 = mul i64 %after_index0, " << entry_stride << "\n";
        out << "  %suffix_src0 = getelementptr i8, ptr %old_data0, i64 %suffix_src_offset0\n";
        out << "  %suffix_dest0 = getelementptr i8, ptr %data0, i64 %prefix_bytes0\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %suffix_dest0, ptr %suffix_src0, i64 %suffix_bytes0, i1 false)\n";
        out << "  %carrier0 = insertvalue " << success_type->llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %carrier1 = insertvalue " << success_type->llvm_type() << " %carrier0, i64 %new_count0, 1\n";
        emit_yield_return("ok_nonempty", "0", *success_type, "%carrier1");
        out << "\n";
        out << "removed_empty:\n";
        emit_yield_return("ok_empty", "0", *success_type, "zeroinitializer");
        out << "\n";
        out << "missing:\n";
        emit_failure_return();
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::MapConsumeBoundValue: {
        if (param_types.size() != 2) {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' expected two parameters");
        }
        if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' does not declare fails");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const BuiltinKind expected_param_kind = base_name == "nonempty_map_consume_bound_value"
            ? BuiltinKind::NonEmptyMap
            : BuiltinKind::Map;
        if (!has_builtin_kind(param_types[0], expected_param_kind)) {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' has unsupported map parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (hir_function_.params.size() != 2 || hir_function_.params[0].type.args.size() != 2) {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' has malformed HIR parameters");
        }

        const std::string key_source_name = hir_function_.params[0].type.args[0].text;
        const std::string value_source_name = hir_function_.params[0].type.args[1].text;
        if (hir_function_.params[1].type.text != key_source_name) {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' has mismatched key parameter type");
        }

        const std::expected<ResolvedType, std::string> key_type = model_.resolve_type_name(key_source_name);
        if (!key_type.has_value()) {
            return std::unexpected(key_type.error());
        }
        if (key_type->identity().category() != ResolvedTypeCategory::BuiltinType) {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' uses non-builtin key type '"
                                   + key_type->source_name() + "'");
        }

        const BuiltinKind key_kind = key_type->identity().builtin_kind();
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
        case BuiltinKind::Float:
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' uses unsupported key type '"
                                   + key_type->source_name() + "'");
        }

        if (key_kind == BuiltinKind::Text || key_kind == BuiltinKind::Bytes) {
            module_.require_runtime_helper(RuntimeHelper::Memcmp);
        }

        const std::expected<ResolvedType, std::string> value_type = model_.resolve_type_name(value_source_name);
        if (!value_type.has_value()) {
            return std::unexpected(value_type.error());
        }
        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        if (success_type->identity().category() != ResolvedTypeCategory::BackendAggregateStorage
            || type_base_name(success_type->source_name()) != "MapBoundValueAndRest") {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "'");
        }

        const std::string rest_source_name = "Map<" + key_source_name + ", " + value_source_name + ">";
        const std::expected<ResolvedType, std::string> rest_type = model_.resolve_type_name(rest_source_name);
        if (!rest_type.has_value()) {
            return std::unexpected(rest_type.error());
        }
        if (!has_builtin_kind(*rest_type, BuiltinKind::Map)) {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' built unsupported rest type '"
                                   + rest_type->source_name() + "'");
        }

        const std::string entry_source_name = "MapEntry<" + key_source_name + ", " + value_source_name + ">";
        const std::expected<ResolvedType, std::string> entry_type = model_.resolve_type_name(entry_source_name);
        if (!entry_type.has_value()) {
            return std::unexpected(entry_type.error());
        }
        const std::expected<const TypeLayout*, std::string> entry_layout_result =
            model_.ensure_backend_aggregate_layout(entry_source_name);
        if (!entry_layout_result.has_value()) {
            return std::unexpected(entry_layout_result.error());
        }
        const TypeLayout& entry_layout = **entry_layout_result;
        const auto key_field_it = entry_layout.field_indices.find("key");
        const auto entry_value_field_it = entry_layout.field_indices.find("value");
        if (key_field_it == entry_layout.field_indices.end()
            || entry_value_field_it == entry_layout.field_indices.end()) {
            return std::unexpected("internal backend error: map consume-bound-value entry layout for '"
                                   + entry_source_name + "' is missing expected fields");
        }

        const std::expected<const TypeLayout*, std::string> result_layout_result =
            model_.ensure_backend_aggregate_layout(success_type->source_name());
        if (!result_layout_result.has_value()) {
            return std::unexpected(result_layout_result.error());
        }
        const TypeLayout& result_layout = **result_layout_result;
        const auto result_value_field_it = result_layout.field_indices.find("value");
        const auto result_rest_field_it = result_layout.field_indices.find("rest");
        if (result_value_field_it == result_layout.field_indices.end()
            || result_rest_field_it == result_layout.field_indices.end()) {
            return std::unexpected("internal backend error: map consume-bound-value result layout for '"
                                   + success_type->source_name() + "' is missing expected fields");
        }

        const std::expected<const YieldLayout*, std::string> yield_layout_result =
            model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout_result.has_value()) {
            return std::unexpected(yield_layout_result.error());
        }
        const YieldLayout& yield_layout = **yield_layout_result;
        if (return_type.llvm_type() != yield_layout.llvm_type) {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' has mismatched yield return type");
        }

        const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
        if (reason_decl.qualified_name != "MapBindingFailure") {
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' fails with '"
                                   + reason_decl.qualified_name + "'");
        }
        const std::expected<ResolvedType, std::string> failure_type =
            model_.resolve_type_name(reason_decl.qualified_name);
        if (!failure_type.has_value()) {
            return std::unexpected(failure_type.error());
        }
        const std::expected<const TypeLayout*, std::string> failure_layout_result =
            model_.ensure_type_layout(reason_decl.id);
        if (!failure_layout_result.has_value()) {
            return std::unexpected(failure_layout_result.error());
        }
        const TypeLayout& failure_layout = **failure_layout_result;
        const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
            [&]() -> std::expected<std::uint32_t, std::string> {
            for (const hir::VariantId variant_id : reason_decl.variants) {
                const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                if (variant_decl.name != "RequestedKeyHadNoBinding") {
                    continue;
                }
                const auto layout_it = failure_layout.variants.find(variant_id);
                if (layout_it == failure_layout.variants.end()) {
                    return std::unexpected("internal backend error: missing layout for failure variant '"
                                           + variant_decl.qualified_name + "'");
                }
                return layout_it->second.tag_value;
            }
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name
                                   + "' could not find failure variant 'RequestedKeyHadNoBinding'");
        }();
        if (!failure_variant_tag_result.has_value()) {
            return std::unexpected(failure_variant_tag_result.error());
        }
        const std::uint32_t failure_variant_tag = *failure_variant_tag_result;

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        module_.require_runtime_helper(RuntimeHelper::Memcpy);

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout.llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout.payload_size == 0) {
                out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout.payload_storage_type << "\n";
            out << "  store " << yield_layout.payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout.payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout.llvm_type
                << " %" << prefix << ".tag, " << yield_layout.payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout.payload_field_index << "\n";
            out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".value\n";
        };

        const auto emit_failure_return = [&]() {
            out << "  %reason0 = insertvalue " << failure_type->llvm_type()
                << " zeroinitializer, i32 " << failure_variant_tag << ", 0\n";
            emit_yield_return("fail", "1", *failure_type, "%reason0");
        };

        const std::size_t entry_stride = element_storage_stride(*entry_type);
        out << "define " << linkage << yield_layout.llvm_type << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1) {\n";
        out << "entry:\n";
        out << "  %old_data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %old_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "scan:\n";
        out << "  %index0 = phi i64 [ 0, %entry ], [ %next0, %not_match ]\n";
        out << "  %at_end0 = icmp eq i64 %index0, %old_count0\n";
        out << "  br i1 %at_end0, label %missing, label %probe\n";
        out << "\n";
        out << "probe:\n";
        out << "  %offset0 = mul i64 %index0, " << entry_stride << "\n";
        out << "  %entry_ptr0 = getelementptr i8, ptr %old_data0, i64 %offset0\n";
        out << "  %entry0 = load " << entry_type->llvm_type() << ", ptr %entry_ptr0\n";
        out << "  %candidate_key0 = extractvalue " << entry_type->llvm_type()
            << " %entry0, " << key_field_it->second << "\n";
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
            out << "  %key_equal0 = icmp eq " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
            out << "  %candidate_data0 = extractvalue " << key_type->llvm_type() << " %candidate_key0, 0\n";
            out << "  %candidate_count0 = extractvalue " << key_type->llvm_type() << " %candidate_key0, 1\n";
            out << "  %lookup_data0 = extractvalue " << key_type->llvm_type() << " %arg1, 0\n";
            out << "  %lookup_count0 = extractvalue " << key_type->llvm_type() << " %arg1, 1\n";
            out << "  %key_size_equal0 = icmp eq i64 %candidate_count0, %lookup_count0\n";
            out << "  br i1 %key_size_equal0, label %key_size_equal, label %not_match\n";
            out << "\n";
            out << "key_size_equal:\n";
            out << "  %key_empty0 = icmp eq i64 %candidate_count0, 0\n";
            out << "  br i1 %key_empty0, label %found, label %key_bytes_compare\n";
            out << "\n";
            out << "key_bytes_compare:\n";
            out << "  %key_cmp0 = call i32 @memcmp(ptr %candidate_data0, ptr %lookup_data0, i64 %candidate_count0)\n";
            out << "  %key_equal0 = icmp eq i32 %key_cmp0, 0\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::Float:
            out << "  %key_equal0 = fcmp oeq " << key_type->llvm_type() << " %candidate_key0, %arg1\n";
            out << "  br i1 %key_equal0, label %found, label %not_match\n";
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map consume-bound-value function '"
                                   + hir_function_.qualified_name + "' reached unsupported key lowering");
        }
        out << "\n";
        out << "not_match:\n";
        out << "  %next0 = add i64 %index0, 1\n";
        out << "  br label %scan\n";
        out << "\n";
        out << "found:\n";
        out << "  %value0 = extractvalue " << entry_type->llvm_type()
            << " %entry0, " << entry_value_field_it->second << "\n";
        out << "  %new_count0 = sub i64 %old_count0, 1\n";
        out << "  %result_empty0 = icmp eq i64 %new_count0, 0\n";
        out << "  br i1 %result_empty0, label %consumed_empty, label %consumed_nonempty\n";
        out << "\n";
        out << "consumed_nonempty:\n";
        out << "  %prefix_bytes0 = mul i64 %index0, " << entry_stride << "\n";
        out << "  %after_index0 = add i64 %index0, 1\n";
        out << "  %suffix_count0 = sub i64 %old_count0, %after_index0\n";
        out << "  %suffix_bytes0 = mul i64 %suffix_count0, " << entry_stride << "\n";
        out << "  %alloc_bytes0 = mul i64 %new_count0, " << entry_stride << "\n";
        out << "  %data0 = call ptr @malloc(i64 %alloc_bytes0)\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %data0, ptr %old_data0, i64 %prefix_bytes0, i1 false)\n";
        out << "  %suffix_src_offset0 = mul i64 %after_index0, " << entry_stride << "\n";
        out << "  %suffix_src0 = getelementptr i8, ptr %old_data0, i64 %suffix_src_offset0\n";
        out << "  %suffix_dest0 = getelementptr i8, ptr %data0, i64 %prefix_bytes0\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %suffix_dest0, ptr %suffix_src0, i64 %suffix_bytes0, i1 false)\n";
        out << "  %rest0 = insertvalue " << rest_type->llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %rest1 = insertvalue " << rest_type->llvm_type() << " %rest0, i64 %new_count0, 1\n";
        out << "  %result0 = insertvalue " << success_type->llvm_type() << " zeroinitializer, "
            << value_type->llvm_type() << " %value0, " << result_value_field_it->second << "\n";
        out << "  %result1 = insertvalue " << success_type->llvm_type() << " %result0, "
            << rest_type->llvm_type() << " %rest1, " << result_rest_field_it->second << "\n";
        emit_yield_return("ok_nonempty", "0", *success_type, "%result1");
        out << "\n";
        out << "consumed_empty:\n";
        out << "  %empty_result0 = insertvalue " << success_type->llvm_type() << " zeroinitializer, "
            << value_type->llvm_type() << " %value0, " << result_value_field_it->second << "\n";
        out << "  %empty_result1 = insertvalue " << success_type->llvm_type() << " %empty_result0, "
            << rest_type->llvm_type() << " zeroinitializer, " << result_rest_field_it->second << "\n";
        emit_yield_return("ok_empty", "0", *success_type, "%empty_result1");
        out << "\n";
        out << "missing:\n";
        emit_failure_return();
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::MapMerge: {
        if (param_types.size() != 2) {
            return std::unexpected("internal backend error: map merge function '"
                                   + hir_function_.qualified_name + "' expected two parameters");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const auto rejects_shared_keys =
            base_name == "map_merge_rejecting_shared_keys"
            || base_name == "map_merge_left_nonempty_rejecting_shared_keys"
            || base_name == "map_merge_right_nonempty_rejecting_shared_keys"
            || base_name == "nonempty_map_merge_rejecting_shared_keys";
        const auto left_nonempty =
            base_name == "map_merge_left_nonempty_rejecting_shared_keys"
            || base_name == "nonempty_map_merge_rejecting_shared_keys"
            || base_name == "map_merge_left_nonempty_using_left_bindings_for_shared_keys"
            || base_name == "nonempty_map_merge_using_left_bindings_for_shared_keys"
            || base_name == "map_merge_left_nonempty_using_right_bindings_for_shared_keys"
            || base_name == "nonempty_map_merge_using_right_bindings_for_shared_keys";
        const auto right_nonempty =
            base_name == "map_merge_right_nonempty_rejecting_shared_keys"
            || base_name == "nonempty_map_merge_rejecting_shared_keys"
            || base_name == "map_merge_right_nonempty_using_left_bindings_for_shared_keys"
            || base_name == "nonempty_map_merge_using_left_bindings_for_shared_keys"
            || base_name == "map_merge_right_nonempty_using_right_bindings_for_shared_keys"
            || base_name == "nonempty_map_merge_using_right_bindings_for_shared_keys";
        const MapMergeCollisionPolicy collision_policy = rejects_shared_keys
            ? MapMergeCollisionPolicy::RejectSharedKeys
            : ((base_name == "map_merge_using_left_bindings_for_shared_keys"
                || base_name == "map_merge_left_nonempty_using_left_bindings_for_shared_keys"
                || base_name == "map_merge_right_nonempty_using_left_bindings_for_shared_keys"
                || base_name == "nonempty_map_merge_using_left_bindings_for_shared_keys")
                   ? MapMergeCollisionPolicy::UseLeftBinding
                   : MapMergeCollisionPolicy::UseRightBinding);

        const BuiltinKind expected_left_kind = left_nonempty ? BuiltinKind::NonEmptyMap : BuiltinKind::Map;
        const BuiltinKind expected_right_kind = right_nonempty ? BuiltinKind::NonEmptyMap : BuiltinKind::Map;
        const BuiltinKind expected_return_kind = (left_nonempty || right_nonempty)
            ? BuiltinKind::NonEmptyMap
            : BuiltinKind::Map;
        if (!has_builtin_kind(param_types[0], expected_left_kind)
            || !has_builtin_kind(param_types[1], expected_right_kind)) {
            return std::unexpected("internal backend error: map merge function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter types '"
                                   + param_types[0].source_name() + "' and '"
                                   + param_types[1].source_name() + "'");
        }
        if (hir_function_.params.size() != 2 || hir_function_.params[0].type.args.size() != 2
            || hir_function_.params[1].type.args.size() != 2) {
            return std::unexpected("internal backend error: map merge function '"
                                   + hir_function_.qualified_name + "' has malformed HIR parameters");
        }

        const std::string key_source_name = hir_function_.params[0].type.args[0].text;
        const std::string value_source_name = hir_function_.params[0].type.args[1].text;
        if (hir_function_.params[1].type.args[0].text != key_source_name
            || hir_function_.params[1].type.args[1].text != value_source_name) {
            return std::unexpected("internal backend error: map merge function '"
                                   + hir_function_.qualified_name + "' has mismatched map parameter types");
        }

        const std::expected<ResolvedType, std::string> key_type = model_.resolve_type_name(key_source_name);
        if (!key_type.has_value()) {
            return std::unexpected(key_type.error());
        }
        if (key_type->identity().category() != ResolvedTypeCategory::BuiltinType) {
            return std::unexpected("internal backend error: map merge function '"
                                   + hir_function_.qualified_name + "' uses non-builtin key type '"
                                   + key_type->source_name() + "'");
        }
        const BuiltinKind key_kind = key_type->identity().builtin_kind();
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
        case BuiltinKind::Float:
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map merge function '"
                                   + hir_function_.qualified_name + "' uses unsupported key type '"
                                   + key_type->source_name() + "'");
        }

        if (key_kind == BuiltinKind::Text || key_kind == BuiltinKind::Bytes) {
            module_.require_runtime_helper(RuntimeHelper::Memcmp);
        }

        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        if (!has_builtin_kind(*success_type, expected_return_kind)) {
            return std::unexpected("internal backend error: map merge function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "'");
        }

        const std::string entry_source_name = "MapEntry<" + key_source_name + ", " + value_source_name + ">";
        const std::expected<ResolvedType, std::string> entry_type = model_.resolve_type_name(entry_source_name);
        if (!entry_type.has_value()) {
            return std::unexpected(entry_type.error());
        }
        const std::expected<const TypeLayout*, std::string> entry_layout_result =
            model_.ensure_backend_aggregate_layout(entry_source_name);
        if (!entry_layout_result.has_value()) {
            return std::unexpected(entry_layout_result.error());
        }
        const TypeLayout& entry_layout = **entry_layout_result;
        const auto key_field_it = entry_layout.field_indices.find("key");
        if (key_field_it == entry_layout.field_indices.end()) {
            return std::unexpected("internal backend error: map merge entry layout for '"
                                   + entry_source_name + "' is missing expected key field");
        }

        const YieldLayout* yield_layout = nullptr;
        ResolvedType failure_type = ResolvedType::runtime_value("",
                                                                "i8",
                                                                1,
                                                                1,
                                                                RuntimeStorageShape::Scalar,
                                                                ResolvedTypeIdentity::builtin_type(BuiltinKind::Unit));
        std::uint32_t failure_variant_tag = 0;
        if (collision_policy == MapMergeCollisionPolicy::RejectSharedKeys) {
            if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
                return std::unexpected("internal backend error: map merge function '"
                                       + hir_function_.qualified_name + "' does not declare fails");
            }

            const std::expected<const YieldLayout*, std::string> yield_layout_result =
                model_.ensure_yield_layout(hir_function_.id);
            if (!yield_layout_result.has_value()) {
                return std::unexpected(yield_layout_result.error());
            }
            yield_layout = *yield_layout_result;
            if (return_type.llvm_type() != yield_layout->llvm_type) {
                return std::unexpected("internal backend error: map merge function '"
                                       + hir_function_.qualified_name + "' has mismatched yield return type");
            }

            const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
            if (reason_decl.qualified_name != "MapMergeFailure") {
                return std::unexpected("internal backend error: map merge function '"
                                       + hir_function_.qualified_name + "' fails with '"
                                       + reason_decl.qualified_name + "'");
            }
            const std::expected<ResolvedType, std::string> failure_type_result =
                model_.resolve_type_name(reason_decl.qualified_name);
            if (!failure_type_result.has_value()) {
                return std::unexpected(failure_type_result.error());
            }
            failure_type = *failure_type_result;
            const std::expected<const TypeLayout*, std::string> failure_layout_result =
                model_.ensure_type_layout(reason_decl.id);
            if (!failure_layout_result.has_value()) {
                return std::unexpected(failure_layout_result.error());
            }
            const TypeLayout& failure_layout = **failure_layout_result;

            const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
                [&]() -> std::expected<std::uint32_t, std::string> {
                for (const hir::VariantId variant_id : reason_decl.variants) {
                    const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                    if (variant_decl.name != "InputsHadSharedKey") {
                        continue;
                    }
                    const auto layout_it = failure_layout.variants.find(variant_id);
                    if (layout_it == failure_layout.variants.end()) {
                        return std::unexpected("internal backend error: missing layout for failure variant '"
                                               + variant_decl.qualified_name + "'");
                    }
                    return layout_it->second.tag_value;
                }
                return std::unexpected("internal backend error: map merge function '"
                                       + hir_function_.qualified_name
                                       + "' could not find failure variant 'InputsHadSharedKey'");
            }();
            if (!failure_variant_tag_result.has_value()) {
                return std::unexpected(failure_variant_tag_result.error());
            }
            failure_variant_tag = *failure_variant_tag_result;
        } else {
            if (hir_function_.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
                return std::unexpected("internal backend error: map merge policy function '"
                                       + hir_function_.qualified_name + "' unexpectedly declares fails");
            }
            if (return_type.llvm_type() != success_type->llvm_type()) {
                return std::unexpected("internal backend error: map merge policy function '"
                                       + hir_function_.qualified_name + "' has mismatched return type");
            }
        }

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        module_.require_runtime_helper(RuntimeHelper::Memcpy);

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout->llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout->payload_size == 0) {
                out << "  ret " << yield_layout->llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout->payload_storage_type << "\n";
            out << "  store " << yield_layout->payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout->payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout->llvm_type
                << " %" << prefix << ".tag, " << yield_layout->payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout->payload_field_index << "\n";
            out << "  ret " << yield_layout->llvm_type << " %" << prefix << ".value\n";
        };

        const auto emit_success_return =
            [&](std::string_view prefix, const std::string& carrier_value) {
            if (collision_policy == MapMergeCollisionPolicy::RejectSharedKeys) {
                emit_yield_return(prefix, "0", *success_type, carrier_value);
                return;
            }
            out << "  ret " << success_type->llvm_type() << " " << carrier_value << "\n";
        };

        const auto emit_failure_return = [&]() {
            out << "  %reason0 = insertvalue " << failure_type.llvm_type()
                << " zeroinitializer, i32 " << failure_variant_tag << ", 0\n";
            emit_yield_return("fail", "1", failure_type, "%reason0");
        };

        const std::size_t entry_stride = element_storage_stride(*entry_type);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1) {\n";
        out << "entry:\n";
        out << "  %left_data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %left_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %right_data0 = extractvalue " << param_types[1].llvm_type() << " %arg1, 0\n";
        out << "  %right_count0 = extractvalue " << param_types[1].llvm_type() << " %arg1, 1\n";
        out << "  %max_count0 = add i64 %left_count0, %right_count0\n";
        out << "  %result_empty0 = icmp eq i64 %max_count0, 0\n";
        out << "  br i1 %result_empty0, label %return_empty, label %allocate\n";
        out << "\n";
        out << "return_empty:\n";
        emit_success_return("ok_empty", "zeroinitializer");
        out << "\n";
        out << "allocate:\n";
        out << "  %alloc_bytes0 = mul i64 %max_count0, " << entry_stride << "\n";
        out << "  %data0 = call ptr @malloc(i64 %alloc_bytes0)\n";
        out << "  %left_index_slot = alloca i64\n";
        out << "  %right_index_slot = alloca i64\n";
        out << "  %out_index_slot = alloca i64\n";
        out << "  store i64 0, ptr %left_index_slot\n";
        out << "  store i64 0, ptr %right_index_slot\n";
        out << "  store i64 0, ptr %out_index_slot\n";
        out << "  br label %merge_loop\n";
        out << "\n";
        out << "merge_loop:\n";
        out << "  %left_index0 = load i64, ptr %left_index_slot\n";
        out << "  %right_index0 = load i64, ptr %right_index_slot\n";
        out << "  %out_index0 = load i64, ptr %out_index_slot\n";
        out << "  %left_done0 = icmp eq i64 %left_index0, %left_count0\n";
        out << "  br i1 %left_done0, label %left_exhausted, label %left_available\n";
        out << "\n";
        out << "left_exhausted:\n";
        out << "  %right_done0 = icmp eq i64 %right_index0, %right_count0\n";
        out << "  br i1 %right_done0, label %merge_done, label %copy_right_tail\n";
        out << "\n";
        out << "left_available:\n";
        out << "  %right_done1 = icmp eq i64 %right_index0, %right_count0\n";
        out << "  br i1 %right_done1, label %copy_left_tail, label %compare_keys\n";
        out << "\n";
        out << "copy_right_tail:\n";
        out << "  %right_remaining0 = sub i64 %right_count0, %right_index0\n";
        out << "  %right_tail_bytes0 = mul i64 %right_remaining0, " << entry_stride << "\n";
        out << "  %right_tail_src_offset0 = mul i64 %right_index0, " << entry_stride << "\n";
        out << "  %right_tail_src0 = getelementptr i8, ptr %right_data0, i64 %right_tail_src_offset0\n";
        out << "  %right_tail_dest_offset0 = mul i64 %out_index0, " << entry_stride << "\n";
        out << "  %right_tail_dest0 = getelementptr i8, ptr %data0, i64 %right_tail_dest_offset0\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %right_tail_dest0, ptr %right_tail_src0, i64 %right_tail_bytes0, i1 false)\n";
        out << "  %right_tail_out_next0 = add i64 %out_index0, %right_remaining0\n";
        out << "  store i64 %right_tail_out_next0, ptr %out_index_slot\n";
        out << "  br label %merge_done\n";
        out << "\n";
        out << "copy_left_tail:\n";
        out << "  %left_remaining0 = sub i64 %left_count0, %left_index0\n";
        out << "  %left_tail_bytes0 = mul i64 %left_remaining0, " << entry_stride << "\n";
        out << "  %left_tail_src_offset0 = mul i64 %left_index0, " << entry_stride << "\n";
        out << "  %left_tail_src0 = getelementptr i8, ptr %left_data0, i64 %left_tail_src_offset0\n";
        out << "  %left_tail_dest_offset0 = mul i64 %out_index0, " << entry_stride << "\n";
        out << "  %left_tail_dest0 = getelementptr i8, ptr %data0, i64 %left_tail_dest_offset0\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %left_tail_dest0, ptr %left_tail_src0, i64 %left_tail_bytes0, i1 false)\n";
        out << "  %left_tail_out_next0 = add i64 %out_index0, %left_remaining0\n";
        out << "  store i64 %left_tail_out_next0, ptr %out_index_slot\n";
        out << "  br label %merge_done\n";
        out << "\n";
        out << "compare_keys:\n";
        out << "  %left_offset0 = mul i64 %left_index0, " << entry_stride << "\n";
        out << "  %left_entry_ptr0 = getelementptr i8, ptr %left_data0, i64 %left_offset0\n";
        out << "  %left_entry0 = load " << entry_type->llvm_type() << ", ptr %left_entry_ptr0\n";
        out << "  %left_key0 = extractvalue " << entry_type->llvm_type()
            << " %left_entry0, " << key_field_it->second << "\n";
        out << "  %right_offset0 = mul i64 %right_index0, " << entry_stride << "\n";
        out << "  %right_entry_ptr0 = getelementptr i8, ptr %right_data0, i64 %right_offset0\n";
        out << "  %right_entry0 = load " << entry_type->llvm_type() << ", ptr %right_entry_ptr0\n";
        out << "  %right_key0 = extractvalue " << entry_type->llvm_type()
            << " %right_entry0, " << key_field_it->second << "\n";
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char: {
            const std::string less_predicate = key_kind == BuiltinKind::Int ? "slt" : "ult";
            out << "  %key_equal0 = icmp eq " << key_type->llvm_type() << " %left_key0, %right_key0\n";
            out << "  br i1 %key_equal0, label %shared_key, label %key_not_equal\n";
            out << "\n";
            out << "key_not_equal:\n";
            out << "  %left_key_less0 = icmp " << less_predicate << " "
                << key_type->llvm_type() << " %left_key0, %right_key0\n";
            out << "  br i1 %left_key_less0, label %take_left, label %take_right\n";
            break;
        }
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
            out << "  %left_key_data0 = extractvalue " << key_type->llvm_type() << " %left_key0, 0\n";
            out << "  %left_key_count0 = extractvalue " << key_type->llvm_type() << " %left_key0, 1\n";
            out << "  %right_key_data0 = extractvalue " << key_type->llvm_type() << " %right_key0, 0\n";
            out << "  %right_key_count0 = extractvalue " << key_type->llvm_type() << " %right_key0, 1\n";
            out << "  %left_key_shorter0 = icmp ult i64 %left_key_count0, %right_key_count0\n";
            out << "  %min_key_count0 = select i1 %left_key_shorter0, i64 %left_key_count0, i64 %right_key_count0\n";
            out << "  %min_key_empty0 = icmp eq i64 %min_key_count0, 0\n";
            out << "  br i1 %min_key_empty0, label %key_bytes_equal_prefix, label %key_bytes_compare\n";
            out << "\n";
            out << "key_bytes_compare:\n";
            out << "  %key_cmp0 = call i32 @memcmp(ptr %left_key_data0, ptr %right_key_data0, i64 %min_key_count0)\n";
            out << "  %key_bytes_equal0 = icmp eq i32 %key_cmp0, 0\n";
            out << "  br i1 %key_bytes_equal0, label %key_bytes_equal_prefix, label %key_bytes_ordered\n";
            out << "\n";
            out << "key_bytes_ordered:\n";
            out << "  %left_key_bytes_less0 = icmp slt i32 %key_cmp0, 0\n";
            out << "  br i1 %left_key_bytes_less0, label %take_left, label %take_right\n";
            out << "\n";
            out << "key_bytes_equal_prefix:\n";
            out << "  %key_equal0 = icmp eq i64 %left_key_count0, %right_key_count0\n";
            out << "  br i1 %key_equal0, label %shared_key, label %key_prefix_not_equal\n";
            out << "\n";
            out << "key_prefix_not_equal:\n";
            out << "  br i1 %left_key_shorter0, label %take_left, label %take_right\n";
            break;
        case BuiltinKind::Float:
            out << "  %key_equal0 = fcmp oeq " << key_type->llvm_type() << " %left_key0, %right_key0\n";
            out << "  br i1 %key_equal0, label %shared_key, label %key_not_equal\n";
            out << "\n";
            out << "key_not_equal:\n";
            out << "  %left_key_less0 = fcmp olt " << key_type->llvm_type() << " %left_key0, %right_key0\n";
            out << "  br i1 %left_key_less0, label %take_left, label %take_right\n";
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map merge function '"
                                   + hir_function_.qualified_name + "' reached unsupported key lowering");
        }
        out << "\n";
        out << "take_left:\n";
        out << "  %take_left_out_offset0 = mul i64 %out_index0, " << entry_stride << "\n";
        out << "  %take_left_out_ptr0 = getelementptr i8, ptr %data0, i64 %take_left_out_offset0\n";
        out << "  store " << entry_type->llvm_type() << " %left_entry0, ptr %take_left_out_ptr0\n";
        out << "  %take_left_next0 = add i64 %left_index0, 1\n";
        out << "  %take_left_out_next0 = add i64 %out_index0, 1\n";
        out << "  store i64 %take_left_next0, ptr %left_index_slot\n";
        out << "  store i64 %take_left_out_next0, ptr %out_index_slot\n";
        out << "  br label %merge_loop\n";
        out << "\n";
        out << "take_right:\n";
        out << "  %take_right_out_offset0 = mul i64 %out_index0, " << entry_stride << "\n";
        out << "  %take_right_out_ptr0 = getelementptr i8, ptr %data0, i64 %take_right_out_offset0\n";
        out << "  store " << entry_type->llvm_type() << " %right_entry0, ptr %take_right_out_ptr0\n";
        out << "  %take_right_next0 = add i64 %right_index0, 1\n";
        out << "  %take_right_out_next0 = add i64 %out_index0, 1\n";
        out << "  store i64 %take_right_next0, ptr %right_index_slot\n";
        out << "  store i64 %take_right_out_next0, ptr %out_index_slot\n";
        out << "  br label %merge_loop\n";
        out << "\n";
        out << "shared_key:\n";
        if (collision_policy == MapMergeCollisionPolicy::RejectSharedKeys) {
            emit_failure_return();
            out << "\n";
        } else {
            const std::string selected_entry =
                collision_policy == MapMergeCollisionPolicy::UseLeftBinding ? "%left_entry0" : "%right_entry0";
            out << "  %shared_out_offset0 = mul i64 %out_index0, " << entry_stride << "\n";
            out << "  %shared_out_ptr0 = getelementptr i8, ptr %data0, i64 %shared_out_offset0\n";
            out << "  store " << entry_type->llvm_type() << " " << selected_entry << ", ptr %shared_out_ptr0\n";
            out << "  %shared_left_next0 = add i64 %left_index0, 1\n";
            out << "  %shared_right_next0 = add i64 %right_index0, 1\n";
            out << "  %shared_out_next0 = add i64 %out_index0, 1\n";
            out << "  store i64 %shared_left_next0, ptr %left_index_slot\n";
            out << "  store i64 %shared_right_next0, ptr %right_index_slot\n";
            out << "  store i64 %shared_out_next0, ptr %out_index_slot\n";
            out << "  br label %merge_loop\n";
            out << "\n";
        }
        out << "merge_done:\n";
        out << "  %merged_count0 = load i64, ptr %out_index_slot\n";
        out << "  %carrier0 = insertvalue " << success_type->llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %carrier1 = insertvalue " << success_type->llvm_type() << " %carrier0, i64 %merged_count0, 1\n";
        emit_success_return("ok", "%carrier1");
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::MapFromEntries: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        MapFromEntriesDuplicatePolicy duplicate_policy = MapFromEntriesDuplicatePolicy::UseLastBinding;
        if (base_name == "map_from_entries_rejecting_shared_keys"
            || base_name == "nonempty_map_from_entries_rejecting_shared_keys") {
            duplicate_policy = MapFromEntriesDuplicatePolicy::RejectSharedKeys;
        } else if (base_name == "map_from_entries_using_first_bindings"
                   || base_name == "nonempty_map_from_entries_using_first_bindings") {
            duplicate_policy = MapFromEntriesDuplicatePolicy::UseFirstBinding;
        } else if (base_name == "map_from_entries_using_last_bindings"
                   || base_name == "nonempty_map_from_entries_using_last_bindings") {
            duplicate_policy = MapFromEntriesDuplicatePolicy::UseLastBinding;
        } else {
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' has unknown duplicate policy");
        }

        const BuiltinKind expected_param_kind = base_name.starts_with("nonempty_map_from_entries_")
            ? BuiltinKind::NonEmptyList
            : BuiltinKind::List;
        const BuiltinKind expected_return_kind = expected_param_kind == BuiltinKind::NonEmptyList
            ? BuiltinKind::NonEmptyMap
            : BuiltinKind::Map;
        if (!has_builtin_kind(param_types[0], expected_param_kind)) {
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (hir_function_.params.size() != 1 || hir_function_.params[0].type.args.size() != 1
            || hir_function_.return_type.args.size() != 2) {
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' has malformed HIR types");
        }

        const hir::TypeRef& entry_ref = hir_function_.params[0].type.args[0];
        if (type_base_name(entry_ref.text) != "MapEntry" || entry_ref.args.size() != 2) {
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' expected MapEntry<K, V> input entries");
        }
        const std::string key_source_name = entry_ref.args[0].text;
        const std::string value_source_name = entry_ref.args[1].text;
        if (hir_function_.return_type.args[0].text != key_source_name
            || hir_function_.return_type.args[1].text != value_source_name) {
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' has mismatched entry and return types");
        }

        const std::expected<ResolvedType, std::string> key_type = model_.resolve_type_name(key_source_name);
        if (!key_type.has_value()) {
            return std::unexpected(key_type.error());
        }
        if (key_type->identity().category() != ResolvedTypeCategory::BuiltinType) {
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' uses non-builtin key type '"
                                   + key_type->source_name() + "'");
        }
        const BuiltinKind key_kind = key_type->identity().builtin_kind();
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char:
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
        case BuiltinKind::Float:
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' uses unsupported key type '"
                                   + key_type->source_name() + "'");
        }

        if (key_kind == BuiltinKind::Text || key_kind == BuiltinKind::Bytes) {
            module_.require_runtime_helper(RuntimeHelper::Memcmp);
        }

        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        if (!has_builtin_kind(*success_type, expected_return_kind)) {
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "'");
        }

        const std::expected<ResolvedType, std::string> entry_type = model_.resolve_type_name(entry_ref.text);
        if (!entry_type.has_value()) {
            return std::unexpected(entry_type.error());
        }
        const std::expected<const TypeLayout*, std::string> entry_layout_result =
            model_.ensure_backend_aggregate_layout(entry_ref.text);
        if (!entry_layout_result.has_value()) {
            return std::unexpected(entry_layout_result.error());
        }
        const TypeLayout& entry_layout = **entry_layout_result;
        const auto key_field_it = entry_layout.field_indices.find("key");
        if (key_field_it == entry_layout.field_indices.end()) {
            return std::unexpected("internal backend error: map-from-entries entry layout for '"
                                   + entry_ref.text + "' is missing expected key field");
        }

        const YieldLayout* yield_layout = nullptr;
        ResolvedType failure_type = ResolvedType::runtime_value("",
                                                                "i8",
                                                                1,
                                                                1,
                                                                RuntimeStorageShape::Scalar,
                                                                ResolvedTypeIdentity::builtin_type(BuiltinKind::Unit));
        std::uint32_t failure_variant_tag = 0;
        if (duplicate_policy == MapFromEntriesDuplicatePolicy::RejectSharedKeys) {
            if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
                return std::unexpected("internal backend error: map-from-entries function '"
                                       + hir_function_.qualified_name + "' does not declare fails");
            }

            const std::expected<const YieldLayout*, std::string> yield_layout_result =
                model_.ensure_yield_layout(hir_function_.id);
            if (!yield_layout_result.has_value()) {
                return std::unexpected(yield_layout_result.error());
            }
            yield_layout = *yield_layout_result;
            if (return_type.llvm_type() != yield_layout->llvm_type) {
                return std::unexpected("internal backend error: map-from-entries function '"
                                       + hir_function_.qualified_name + "' has mismatched yield return type");
            }

            const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
            if (reason_decl.qualified_name != "MapMergeFailure") {
                return std::unexpected("internal backend error: map-from-entries function '"
                                       + hir_function_.qualified_name + "' fails with '"
                                       + reason_decl.qualified_name + "'");
            }
            const std::expected<ResolvedType, std::string> failure_type_result =
                model_.resolve_type_name(reason_decl.qualified_name);
            if (!failure_type_result.has_value()) {
                return std::unexpected(failure_type_result.error());
            }
            failure_type = *failure_type_result;
            const std::expected<const TypeLayout*, std::string> failure_layout_result =
                model_.ensure_type_layout(reason_decl.id);
            if (!failure_layout_result.has_value()) {
                return std::unexpected(failure_layout_result.error());
            }
            const TypeLayout& failure_layout = **failure_layout_result;

            const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
                [&]() -> std::expected<std::uint32_t, std::string> {
                for (const hir::VariantId variant_id : reason_decl.variants) {
                    const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                    if (variant_decl.name != "InputsHadSharedKey") {
                        continue;
                    }
                    const auto layout_it = failure_layout.variants.find(variant_id);
                    if (layout_it == failure_layout.variants.end()) {
                        return std::unexpected("internal backend error: missing layout for failure variant '"
                                               + variant_decl.qualified_name + "'");
                    }
                    return layout_it->second.tag_value;
                }
                return std::unexpected("internal backend error: map-from-entries function '"
                                       + hir_function_.qualified_name
                                       + "' could not find failure variant 'InputsHadSharedKey'");
            }();
            if (!failure_variant_tag_result.has_value()) {
                return std::unexpected(failure_variant_tag_result.error());
            }
            failure_variant_tag = *failure_variant_tag_result;
        } else {
            if (hir_function_.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
                return std::unexpected("internal backend error: map-from-entries policy function '"
                                       + hir_function_.qualified_name + "' unexpectedly declares fails");
            }
            if (return_type.llvm_type() != success_type->llvm_type()) {
                return std::unexpected("internal backend error: map-from-entries policy function '"
                                       + hir_function_.qualified_name + "' has mismatched return type");
            }
        }

        module_.require_runtime_helper(RuntimeHelper::Malloc);

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout->llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout->payload_size == 0) {
                out << "  ret " << yield_layout->llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout->payload_storage_type << "\n";
            out << "  store " << yield_layout->payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout->payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout->llvm_type
                << " %" << prefix << ".tag, " << yield_layout->payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout->payload_field_index << "\n";
            out << "  ret " << yield_layout->llvm_type << " %" << prefix << ".value\n";
        };

        const auto emit_success_return =
            [&](std::string_view prefix, const std::string& carrier_value) {
            if (duplicate_policy == MapFromEntriesDuplicatePolicy::RejectSharedKeys) {
                emit_yield_return(prefix, "0", *success_type, carrier_value);
                return;
            }
            out << "  ret " << success_type->llvm_type() << " " << carrier_value << "\n";
        };

        const auto emit_failure_return = [&]() {
            out << "  %reason0 = insertvalue " << failure_type.llvm_type()
                << " zeroinitializer, i32 " << failure_variant_tag << ", 0\n";
            emit_yield_return("fail", "1", failure_type, "%reason0");
        };

        const std::size_t entry_stride = element_storage_stride(*entry_type);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0) {\n";
        out << "entry:\n";
        out << "  %input_data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %input_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %input_empty0 = icmp eq i64 %input_count0, 0\n";
        out << "  br i1 %input_empty0, label %return_empty, label %allocate\n";
        out << "\n";
        out << "return_empty:\n";
        emit_success_return("ok_empty", "zeroinitializer");
        out << "\n";
        out << "allocate:\n";
        out << "  %alloc_bytes0 = mul i64 %input_count0, " << entry_stride << "\n";
        out << "  %data0 = call ptr @malloc(i64 %alloc_bytes0)\n";
        out << "  %input_index_slot = alloca i64\n";
        out << "  %out_count_slot = alloca i64\n";
        out << "  %scan_index_slot = alloca i64\n";
        out << "  %insert_index_slot = alloca i64\n";
        out << "  %shift_index_slot = alloca i64\n";
        out << "  store i64 0, ptr %input_index_slot\n";
        out << "  store i64 0, ptr %out_count_slot\n";
        out << "  br label %process_next\n";
        out << "\n";
        out << "process_next:\n";
        out << "  %input_index0 = load i64, ptr %input_index_slot\n";
        out << "  %input_done0 = icmp eq i64 %input_index0, %input_count0\n";
        out << "  br i1 %input_done0, label %done, label %load_input\n";
        out << "\n";
        out << "load_input:\n";
        out << "  %input_offset0 = mul i64 %input_index0, " << entry_stride << "\n";
        out << "  %input_entry_ptr0 = getelementptr i8, ptr %input_data0, i64 %input_offset0\n";
        out << "  %current_entry0 = load " << entry_type->llvm_type() << ", ptr %input_entry_ptr0\n";
        out << "  %current_key0 = extractvalue " << entry_type->llvm_type()
            << " %current_entry0, " << key_field_it->second << "\n";
        out << "  store i64 0, ptr %scan_index_slot\n";
        out << "  br label %scan_output\n";
        out << "\n";
        out << "scan_output:\n";
        out << "  %scan_index0 = load i64, ptr %scan_index_slot\n";
        out << "  %out_count0 = load i64, ptr %out_count_slot\n";
        out << "  %scan_done0 = icmp eq i64 %scan_index0, %out_count0\n";
        out << "  br i1 %scan_done0, label %insert_current, label %compare_existing\n";
        out << "\n";
        out << "compare_existing:\n";
        out << "  %output_offset0 = mul i64 %scan_index0, " << entry_stride << "\n";
        out << "  %existing_entry_ptr0 = getelementptr i8, ptr %data0, i64 %output_offset0\n";
        out << "  %existing_entry0 = load " << entry_type->llvm_type() << ", ptr %existing_entry_ptr0\n";
        out << "  %existing_key0 = extractvalue " << entry_type->llvm_type()
            << " %existing_entry0, " << key_field_it->second << "\n";
        switch (key_kind) {
        case BuiltinKind::Int:
        case BuiltinKind::Nat:
        case BuiltinKind::Byte:
        case BuiltinKind::Char: {
            const std::string less_predicate = key_kind == BuiltinKind::Int ? "slt" : "ult";
            out << "  %key_equal0 = icmp eq " << key_type->llvm_type() << " %current_key0, %existing_key0\n";
            out << "  br i1 %key_equal0, label %duplicate_key, label %key_not_equal\n";
            out << "\n";
            out << "key_not_equal:\n";
            out << "  %current_key_less0 = icmp " << less_predicate << " "
                << key_type->llvm_type() << " %current_key0, %existing_key0\n";
            out << "  br i1 %current_key_less0, label %insert_current, label %scan_next\n";
            break;
        }
        case BuiltinKind::Text:
        case BuiltinKind::Bytes:
            out << "  %current_key_data0 = extractvalue " << key_type->llvm_type() << " %current_key0, 0\n";
            out << "  %current_key_count0 = extractvalue " << key_type->llvm_type() << " %current_key0, 1\n";
            out << "  %existing_key_data0 = extractvalue " << key_type->llvm_type() << " %existing_key0, 0\n";
            out << "  %existing_key_count0 = extractvalue " << key_type->llvm_type() << " %existing_key0, 1\n";
            out << "  %current_key_shorter0 = icmp ult i64 %current_key_count0, %existing_key_count0\n";
            out << "  %min_key_count0 = select i1 %current_key_shorter0, i64 %current_key_count0, i64 %existing_key_count0\n";
            out << "  %min_key_empty0 = icmp eq i64 %min_key_count0, 0\n";
            out << "  br i1 %min_key_empty0, label %key_bytes_equal_prefix, label %key_bytes_compare\n";
            out << "\n";
            out << "key_bytes_compare:\n";
            out << "  %key_cmp0 = call i32 @memcmp(ptr %current_key_data0, ptr %existing_key_data0, i64 %min_key_count0)\n";
            out << "  %key_bytes_equal0 = icmp eq i32 %key_cmp0, 0\n";
            out << "  br i1 %key_bytes_equal0, label %key_bytes_equal_prefix, label %key_bytes_ordered\n";
            out << "\n";
            out << "key_bytes_ordered:\n";
            out << "  %current_key_bytes_less0 = icmp slt i32 %key_cmp0, 0\n";
            out << "  br i1 %current_key_bytes_less0, label %insert_current, label %scan_next\n";
            out << "\n";
            out << "key_bytes_equal_prefix:\n";
            out << "  %key_equal0 = icmp eq i64 %current_key_count0, %existing_key_count0\n";
            out << "  br i1 %key_equal0, label %duplicate_key, label %key_prefix_not_equal\n";
            out << "\n";
            out << "key_prefix_not_equal:\n";
            out << "  br i1 %current_key_shorter0, label %insert_current, label %scan_next\n";
            break;
        case BuiltinKind::Float:
            out << "  %key_equal0 = fcmp oeq " << key_type->llvm_type() << " %current_key0, %existing_key0\n";
            out << "  br i1 %key_equal0, label %duplicate_key, label %key_not_equal\n";
            out << "\n";
            out << "key_not_equal:\n";
            out << "  %current_key_less0 = fcmp olt " << key_type->llvm_type() << " %current_key0, %existing_key0\n";
            out << "  br i1 %current_key_less0, label %insert_current, label %scan_next\n";
            break;
        case BuiltinKind::CInt:
        case BuiltinKind::CSize:
        case BuiltinKind::CString:
        case BuiltinKind::NonEmptyText:
        case BuiltinKind::NonEmptyBytes:
        case BuiltinKind::List:
        case BuiltinKind::NonEmptyList:
        case BuiltinKind::Map:
        case BuiltinKind::NonEmptyMap:
        case BuiltinKind::Unit:
        case BuiltinKind::Never:
            return std::unexpected("internal backend error: map-from-entries function '"
                                   + hir_function_.qualified_name + "' reached unsupported key lowering");
        }
        out << "\n";
        out << "scan_next:\n";
        out << "  %scan_next0 = add i64 %scan_index0, 1\n";
        out << "  store i64 %scan_next0, ptr %scan_index_slot\n";
        out << "  br label %scan_output\n";
        out << "\n";
        out << "duplicate_key:\n";
        if (duplicate_policy == MapFromEntriesDuplicatePolicy::RejectSharedKeys) {
            emit_failure_return();
            out << "\n";
        } else if (duplicate_policy == MapFromEntriesDuplicatePolicy::UseFirstBinding) {
            out << "  br label %next_input\n";
            out << "\n";
        } else {
            out << "  store " << entry_type->llvm_type() << " %current_entry0, ptr %existing_entry_ptr0\n";
            out << "  br label %next_input\n";
            out << "\n";
        }
        out << "insert_current:\n";
        out << "  %insert_index0 = load i64, ptr %scan_index_slot\n";
        out << "  %out_count1 = load i64, ptr %out_count_slot\n";
        out << "  store i64 %insert_index0, ptr %insert_index_slot\n";
        out << "  %suffix_count0 = sub i64 %out_count1, %insert_index0\n";
        out << "  %suffix_empty0 = icmp eq i64 %suffix_count0, 0\n";
        out << "  br i1 %suffix_empty0, label %store_current, label %shift_setup\n";
        out << "\n";
        out << "shift_setup:\n";
        out << "  store i64 %out_count1, ptr %shift_index_slot\n";
        out << "  br label %shift_loop\n";
        out << "\n";
        out << "shift_loop:\n";
        out << "  %shift_index0 = load i64, ptr %shift_index_slot\n";
        out << "  %insert_index1 = load i64, ptr %insert_index_slot\n";
        out << "  %shift_done0 = icmp eq i64 %shift_index0, %insert_index1\n";
        out << "  br i1 %shift_done0, label %store_current, label %shift_one\n";
        out << "\n";
        out << "shift_one:\n";
        out << "  %shift_source_index0 = sub i64 %shift_index0, 1\n";
        out << "  %shift_source_offset0 = mul i64 %shift_source_index0, " << entry_stride << "\n";
        out << "  %shift_source_ptr0 = getelementptr i8, ptr %data0, i64 %shift_source_offset0\n";
        out << "  %shift_entry0 = load " << entry_type->llvm_type() << ", ptr %shift_source_ptr0\n";
        out << "  %shift_dest_offset0 = mul i64 %shift_index0, " << entry_stride << "\n";
        out << "  %shift_dest_ptr0 = getelementptr i8, ptr %data0, i64 %shift_dest_offset0\n";
        out << "  store " << entry_type->llvm_type() << " %shift_entry0, ptr %shift_dest_ptr0\n";
        out << "  store i64 %shift_source_index0, ptr %shift_index_slot\n";
        out << "  br label %shift_loop\n";
        out << "\n";
        out << "store_current:\n";
        out << "  %insert_index2 = load i64, ptr %insert_index_slot\n";
        out << "  %insert_offset0 = mul i64 %insert_index2, " << entry_stride << "\n";
        out << "  %insert_ptr0 = getelementptr i8, ptr %data0, i64 %insert_offset0\n";
        out << "  store " << entry_type->llvm_type() << " %current_entry0, ptr %insert_ptr0\n";
        out << "  %out_count2 = load i64, ptr %out_count_slot\n";
        out << "  %new_out_count0 = add i64 %out_count2, 1\n";
        out << "  store i64 %new_out_count0, ptr %out_count_slot\n";
        out << "  br label %next_input\n";
        out << "\n";
        out << "next_input:\n";
        out << "  %input_index1 = load i64, ptr %input_index_slot\n";
        out << "  %input_next0 = add i64 %input_index1, 1\n";
        out << "  store i64 %input_next0, ptr %input_index_slot\n";
        out << "  br label %process_next\n";
        out << "\n";
        out << "done:\n";
        out << "  %final_count0 = load i64, ptr %out_count_slot\n";
        out << "  %carrier0 = insertvalue " << success_type->llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %carrier1 = insertvalue " << success_type->llvm_type() << " %carrier0, i64 %final_count0, 1\n";
        emit_success_return("ok", "%carrier1");
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::MapConsumeFirstEntry: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: map consume-first-entry function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!has_builtin_kind(param_types[0], BuiltinKind::NonEmptyMap)) {
            return std::unexpected("internal backend error: map consume-first-entry function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (return_type.identity().category() != ResolvedTypeCategory::BackendAggregateStorage
            || type_base_name(return_type.source_name()) != "MapFirstEntryAndRest") {
            return std::unexpected("internal backend error: map consume-first-entry function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }
        if (hir_function_.params[0].type.args.size() != 2) {
            return std::unexpected("internal backend error: map consume-first-entry function '"
                                   + hir_function_.qualified_name + "' expected two map type arguments");
        }

        const std::string key_source_name = hir_function_.params[0].type.args[0].text;
        const std::string value_source_name = hir_function_.params[0].type.args[1].text;
        const std::string entry_source_name = "MapEntry<" + key_source_name + ", " + value_source_name + ">";
        const std::expected<ResolvedType, std::string> entry_type = model_.resolve_type_name(entry_source_name);
        if (!entry_type.has_value()) {
            return std::unexpected(entry_type.error());
        }
        const std::string rest_source_name = "Map<" + key_source_name + ", " + value_source_name + ">";
        const std::expected<ResolvedType, std::string> rest_type = model_.resolve_type_name(rest_source_name);
        if (!rest_type.has_value()) {
            return std::unexpected(rest_type.error());
        }
        if (!has_builtin_kind(*rest_type, BuiltinKind::Map)) {
            return std::unexpected("internal backend error: map consume-first-entry function '"
                                   + hir_function_.qualified_name + "' built unsupported rest type '"
                                   + rest_type->source_name() + "'");
        }

        const std::expected<const TypeLayout*, std::string> result_layout =
            model_.ensure_backend_aggregate_layout(return_type.source_name());
        if (!result_layout.has_value()) {
            return std::unexpected(result_layout.error());
        }
        const auto first_field_it = (*result_layout)->field_indices.find("first");
        const auto rest_field_it = (*result_layout)->field_indices.find("rest");
        if (first_field_it == (*result_layout)->field_indices.end()
            || rest_field_it == (*result_layout)->field_indices.end()) {
            return std::unexpected("internal backend error: map consume-first-entry result layout for '"
                                   + return_type.source_name() + "' is missing expected fields");
        }

        const std::size_t entry_stride = element_storage_stride(*entry_type);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %entry0 = load " << entry_type->llvm_type() << ", ptr %data0\n";
        out << "  %rest_count0 = sub i64 %count0, 1\n";
        out << "  %rest_data0 = getelementptr i8, ptr %data0, i64 " << entry_stride << "\n";
        out << "  %rest0 = insertvalue " << rest_type->llvm_type() << " zeroinitializer, ptr %rest_data0, 0\n";
        out << "  %rest1 = insertvalue " << rest_type->llvm_type() << " %rest0, i64 %rest_count0, 1\n";
        out << "  %result0 = insertvalue " << return_type.llvm_type() << " zeroinitializer, "
            << entry_type->llvm_type() << " %entry0, " << first_field_it->second << "\n";
        out << "  %result1 = insertvalue " << return_type.llvm_type() << " %result0, "
            << rest_type->llvm_type() << " %rest1, " << rest_field_it->second << "\n";
        out << "  ret " << return_type.llvm_type() << " %result1\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::ListConsumeFirst: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: list consume-first function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (!has_builtin_kind(param_types[0], BuiltinKind::NonEmptyList)) {
            return std::unexpected("internal backend error: list consume-first function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (return_type.identity().category() != ResolvedTypeCategory::BackendAggregateStorage
            || type_base_name(return_type.source_name()) != "ListFirstAndRest") {
            return std::unexpected("internal backend error: list consume-first function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }

        const std::expected<ResolvedType, std::string> element_type =
            resolve_collection_element_type(hir_function_.params[0].type, "list consume-first function '"
                                            + hir_function_.qualified_name + "'");
        if (!element_type.has_value()) {
            return std::unexpected(element_type.error());
        }

        const std::string rest_source_name = "List<" + hir_function_.params[0].type.args[0].text + ">";
        const std::expected<ResolvedType, std::string> rest_type = model_.resolve_type_name(rest_source_name);
        if (!rest_type.has_value()) {
            return std::unexpected(rest_type.error());
        }
        if (!has_builtin_kind(*rest_type, BuiltinKind::List)) {
            return std::unexpected("internal backend error: list consume-first function '"
                                   + hir_function_.qualified_name + "' built unsupported rest type '"
                                   + rest_type->source_name() + "'");
        }

        const std::expected<const TypeLayout*, std::string> result_layout =
            model_.ensure_backend_aggregate_layout(return_type.source_name());
        if (!result_layout.has_value()) {
            return std::unexpected(result_layout.error());
        }
        const auto first_field_it = (*result_layout)->field_indices.find("first");
        const auto rest_field_it = (*result_layout)->field_indices.find("rest");
        if (first_field_it == (*result_layout)->field_indices.end()
            || rest_field_it == (*result_layout)->field_indices.end()) {
            return std::unexpected("internal backend error: list consume-first result layout for '"
                                   + return_type.source_name() + "' is missing expected fields");
        }

        const std::size_t element_stride = element_storage_stride(*element_type);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %first0 = load " << element_type->llvm_type() << ", ptr %data0\n";
        out << "  %rest_count0 = sub i64 %count0, 1\n";
        out << "  %rest_data0 = getelementptr i8, ptr %data0, i64 " << element_stride << "\n";
        out << "  %rest0 = insertvalue " << rest_type->llvm_type() << " zeroinitializer, ptr %rest_data0, 0\n";
        out << "  %rest1 = insertvalue " << rest_type->llvm_type() << " %rest0, i64 %rest_count0, 1\n";
        out << "  %result0 = insertvalue " << return_type.llvm_type() << " zeroinitializer, "
            << element_type->llvm_type() << " %first0, " << first_field_it->second << "\n";
        out << "  %result1 = insertvalue " << return_type.llvm_type() << " %result0, "
            << rest_type->llvm_type() << " %rest1, " << rest_field_it->second << "\n";
        out << "  ret " << return_type.llvm_type() << " %result1\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::ListPrepend: {
        if (param_types.size() != 2) {
            return std::unexpected("internal backend error: list prepend function '"
                                   + hir_function_.qualified_name + "' expected two parameters");
        }
        if (!has_builtin_kind(return_type, BuiltinKind::NonEmptyList)
            || !has_builtin_kind(param_types[1], BuiltinKind::List)) {
            return std::unexpected("internal backend error: list prepend function '"
                                   + hir_function_.qualified_name + "' has unsupported signature");
        }
        if (param_types[0].materialization() == MaterializationState::NeverValue) {
            return std::unexpected("internal backend error: list prepend function '"
                                   + hir_function_.qualified_name + "' stores `Never`");
        }

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        module_.require_runtime_helper(RuntimeHelper::Memcpy);
        const std::size_t element_stride = element_storage_stride(param_types[0]);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1) {\n";
        out << "entry:\n";
        out << "  %old_data0 = extractvalue " << param_types[1].llvm_type() << " %arg1, 0\n";
        out << "  %old_count0 = extractvalue " << param_types[1].llvm_type() << " %arg1, 1\n";
        out << "  %new_count0 = add i64 %old_count0, 1\n";
        out << "  %copy_bytes0 = mul i64 %old_count0, " << element_stride << "\n";
        out << "  %alloc_bytes0 = mul i64 %new_count0, " << element_stride << "\n";
        out << "  %data0 = call ptr @malloc(i64 %alloc_bytes0)\n";
        out << "  store " << param_types[0].llvm_type() << " %arg0, ptr %data0\n";
        out << "  %tail0 = getelementptr i8, ptr %data0, i64 " << element_stride << "\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %tail0, ptr %old_data0, i64 %copy_bytes0, i1 false)\n";
        out << "  %carrier0 = insertvalue " << return_type.llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %carrier1 = insertvalue " << return_type.llvm_type() << " %carrier0, i64 %new_count0, 1\n";
        out << "  ret " << return_type.llvm_type() << " %carrier1\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::ListAppend: {
        if (param_types.size() != 2) {
            return std::unexpected("internal backend error: list append function '"
                                   + hir_function_.qualified_name + "' expected two parameters");
        }
        if (!has_builtin_kind(return_type, BuiltinKind::NonEmptyList)
            || !has_builtin_kind(param_types[0], BuiltinKind::List)) {
            return std::unexpected("internal backend error: list append function '"
                                   + hir_function_.qualified_name + "' has unsupported signature");
        }
        if (param_types[1].materialization() == MaterializationState::NeverValue) {
            return std::unexpected("internal backend error: list append function '"
                                   + hir_function_.qualified_name + "' stores `Never`");
        }

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        module_.require_runtime_helper(RuntimeHelper::Memcpy);
        const std::size_t element_stride = element_storage_stride(param_types[1]);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1) {\n";
        out << "entry:\n";
        out << "  %old_data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %old_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %new_count0 = add i64 %old_count0, 1\n";
        out << "  %copy_bytes0 = mul i64 %old_count0, " << element_stride << "\n";
        out << "  %alloc_bytes0 = mul i64 %new_count0, " << element_stride << "\n";
        out << "  %data0 = call ptr @malloc(i64 %alloc_bytes0)\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %data0, ptr %old_data0, i64 %copy_bytes0, i1 false)\n";
        out << "  %tail0 = getelementptr i8, ptr %data0, i64 %copy_bytes0\n";
        out << "  store " << param_types[1].llvm_type() << " %arg1, ptr %tail0\n";
        out << "  %carrier0 = insertvalue " << return_type.llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %carrier1 = insertvalue " << return_type.llvm_type() << " %carrier0, i64 %new_count0, 1\n";
        out << "  ret " << return_type.llvm_type() << " %carrier1\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::ListConcat: {
        if (param_types.size() != 2) {
            return std::unexpected("internal backend error: list concat function '"
                                   + hir_function_.qualified_name + "' expected two parameters");
        }
        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        if (base_name == "list_concat") {
            if (!has_builtin_kind(return_type, BuiltinKind::List)
                || !has_builtin_kind(param_types[0], BuiltinKind::List)
                || !has_builtin_kind(param_types[1], BuiltinKind::List)) {
                return std::unexpected("internal backend error: list concat function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
        } else if (base_name == "nonempty_list_concat_left") {
            if (!has_builtin_kind(return_type, BuiltinKind::NonEmptyList)
                || !has_builtin_kind(param_types[0], BuiltinKind::NonEmptyList)
                || !has_builtin_kind(param_types[1], BuiltinKind::List)) {
                return std::unexpected("internal backend error: list concat function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
        } else if (base_name == "nonempty_list_concat_right") {
            if (!has_builtin_kind(return_type, BuiltinKind::NonEmptyList)
                || !has_builtin_kind(param_types[0], BuiltinKind::List)
                || !has_builtin_kind(param_types[1], BuiltinKind::NonEmptyList)) {
                return std::unexpected("internal backend error: list concat function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
        } else if (!has_builtin_kind(return_type, BuiltinKind::NonEmptyList)
                   || !has_builtin_kind(param_types[0], BuiltinKind::NonEmptyList)
                   || !has_builtin_kind(param_types[1], BuiltinKind::NonEmptyList)) {
            return std::unexpected("internal backend error: list concat function '"
                                   + hir_function_.qualified_name + "' has unsupported signature");
        }

        const std::expected<ResolvedType, std::string> element_type =
            resolve_collection_element_type(hir_function_.params[0].type, "list concat function '"
                                            + hir_function_.qualified_name + "'");
        if (!element_type.has_value()) {
            return std::unexpected(element_type.error());
        }

        module_.require_runtime_helper(RuntimeHelper::Malloc);
        module_.require_runtime_helper(RuntimeHelper::Memcpy);
        const std::size_t element_stride = element_storage_stride(*element_type);
        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type()
            << " %arg0, " << param_types[1].llvm_type() << " %arg1) {\n";
        out << "entry:\n";
        out << "  %left_data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
        out << "  %left_count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %right_data0 = extractvalue " << param_types[1].llvm_type() << " %arg1, 0\n";
        out << "  %right_count0 = extractvalue " << param_types[1].llvm_type() << " %arg1, 1\n";
        out << "  %new_count0 = add i64 %left_count0, %right_count0\n";
        out << "  %left_bytes0 = mul i64 %left_count0, " << element_stride << "\n";
        out << "  %right_bytes0 = mul i64 %right_count0, " << element_stride << "\n";
        out << "  %alloc_bytes0 = mul i64 %new_count0, " << element_stride << "\n";
        out << "  %data0 = call ptr @malloc(i64 %alloc_bytes0)\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %data0, ptr %left_data0, i64 %left_bytes0, i1 false)\n";
        out << "  %right_dest0 = getelementptr i8, ptr %data0, i64 %left_bytes0\n";
        out << "  call void @llvm.memcpy.p0.p0.i64(ptr %right_dest0, ptr %right_data0, i64 %right_bytes0, i1 false)\n";
        out << "  %carrier0 = insertvalue " << return_type.llvm_type() << " zeroinitializer, ptr %data0, 0\n";
        out << "  %carrier1 = insertvalue " << return_type.llvm_type() << " %carrier0, i64 %new_count0, 1\n";
        out << "  ret " << return_type.llvm_type() << " %carrier1\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::MapEntries: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: map entries function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const BuiltinKind expected_param_kind = (base_name == "nonempty_map_entries_copy"
                                                 || base_name == "nonempty_map_consume_entries")
            ? BuiltinKind::NonEmptyMap
            : BuiltinKind::Map;
        const BuiltinKind expected_return_kind = expected_param_kind == BuiltinKind::NonEmptyMap
            ? BuiltinKind::NonEmptyList
            : BuiltinKind::List;
        if (!has_builtin_kind(param_types[0], expected_param_kind)) {
            return std::unexpected("internal backend error: map entries function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }
        if (!has_builtin_kind(return_type, expected_return_kind)) {
            return std::unexpected("internal backend error: map entries function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + return_type.source_name() + "'");
        }
        if (hir_function_.params.size() != 1) {
            return std::unexpected("internal backend error: map entries function '"
                                   + hir_function_.qualified_name + "' expected one HIR parameter");
        }
        const hir::TypeRef& map_type = hir_function_.params[0].type;
        const hir::TypeRef& list_type = hir_function_.return_type;
        if (map_type.args.size() != 2 || list_type.args.size() != 1) {
            return std::unexpected("internal backend error: map entries function '"
                                   + hir_function_.qualified_name + "' has malformed collection type arguments");
        }
        const hir::TypeRef& entry_type = list_type.args[0];
        if (type_base_name(entry_type.text) != "MapEntry" || entry_type.args.size() != 2
            || entry_type.args[0].text != map_type.args[0].text || entry_type.args[1].text != map_type.args[1].text) {
            return std::unexpected("internal backend error: map entries function '"
                                   + hir_function_.qualified_name + "' does not return matching MapEntry<K, V> values");
        }
        if (param_types[0].llvm_type() != return_type.llvm_type()) {
            return std::unexpected("internal backend error: map entries function '"
                                   + hir_function_.qualified_name + "' has incompatible ABI carriers");
        }

        out << "define " << linkage << return_type.llvm_type() << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  ret " << return_type.llvm_type() << " %arg0\n";
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::RequireNonEmptyCollection: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: collection require-nonempty function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }
        if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
            return std::unexpected("internal backend error: collection require-nonempty function '"
                                   + hir_function_.qualified_name + "' does not declare fails");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const RequireNonEmptyCollectionFamily collection_family = base_name == "list_require_nonempty"
            ? RequireNonEmptyCollectionFamily::List
            : RequireNonEmptyCollectionFamily::Map;
        const BuiltinKind expected_param_kind = collection_family == RequireNonEmptyCollectionFamily::List
            ? BuiltinKind::List
            : BuiltinKind::Map;
        const BuiltinKind expected_success_kind = collection_family == RequireNonEmptyCollectionFamily::List
            ? BuiltinKind::NonEmptyList
            : BuiltinKind::NonEmptyMap;
        const std::string expected_reason_name = collection_family == RequireNonEmptyCollectionFamily::List
            ? "ListCardinalityFailure"
            : "MapCardinalityFailure";
        const std::string expected_failure_variant_name = collection_family == RequireNonEmptyCollectionFamily::List
            ? "ListHadNoElements"
            : "MapHadNoEntries";

        if (!has_builtin_kind(param_types[0], expected_param_kind)) {
            return std::unexpected("internal backend error: collection require-nonempty function '"
                                   + hir_function_.qualified_name + "' has unsupported parameter type '"
                                   + param_types[0].source_name() + "'");
        }

        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }
        if (!has_builtin_kind(*success_type, expected_success_kind)) {
            return std::unexpected("internal backend error: collection require-nonempty function '"
                                   + hir_function_.qualified_name + "' returns '"
                                   + success_type->source_name() + "'");
        }

        const std::expected<const YieldLayout*, std::string> yield_layout_result =
            model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout_result.has_value()) {
            return std::unexpected(yield_layout_result.error());
        }
        const YieldLayout& yield_layout = **yield_layout_result;
        if (return_type.llvm_type() != yield_layout.llvm_type) {
            return std::unexpected("internal backend error: collection require-nonempty function '"
                                   + hir_function_.qualified_name + "' has mismatched yield return type");
        }

        const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
        if (reason_decl.qualified_name != expected_reason_name) {
            return std::unexpected("internal backend error: collection require-nonempty function '"
                                   + hir_function_.qualified_name + "' fails with '"
                                   + reason_decl.qualified_name + "'");
        }
        const std::expected<ResolvedType, std::string> failure_type =
            model_.resolve_type_name(reason_decl.qualified_name);
        if (!failure_type.has_value()) {
            return std::unexpected(failure_type.error());
        }
        const std::expected<const TypeLayout*, std::string> failure_layout_result =
            model_.ensure_type_layout(reason_decl.id);
        if (!failure_layout_result.has_value()) {
            return std::unexpected(failure_layout_result.error());
        }
        const TypeLayout& failure_layout = **failure_layout_result;

        const std::expected<std::uint32_t, std::string> failure_variant_tag_result =
            [&]() -> std::expected<std::uint32_t, std::string> {
            for (const hir::VariantId variant_id : reason_decl.variants) {
                const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                if (variant_decl.name != expected_failure_variant_name) {
                    continue;
                }
                const auto layout_it = failure_layout.variants.find(variant_id);
                if (layout_it == failure_layout.variants.end()) {
                    return std::unexpected("internal backend error: missing layout for failure variant '"
                                           + variant_decl.qualified_name + "'");
                }
                return layout_it->second.tag_value;
            }
            return std::unexpected("internal backend error: collection require-nonempty function '"
                                   + hir_function_.qualified_name + "' could not find failure variant '"
                                   + expected_failure_variant_name + "'");
        }();
        if (!failure_variant_tag_result.has_value()) {
            return std::unexpected(failure_variant_tag_result.error());
        }
        const std::uint32_t failure_variant_tag = *failure_variant_tag_result;

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout.llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout.payload_size == 0) {
                out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout.payload_storage_type << "\n";
            out << "  store " << yield_layout.payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout.payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout.llvm_type
                << " %" << prefix << ".tag, " << yield_layout.payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout.payload_field_index << "\n";
            out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".value\n";
        };

        out << "define " << linkage << yield_layout.llvm_type << " @"
            << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
        out << "entry:\n";
        out << "  %count0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
        out << "  %is_empty0 = icmp eq i64 %count0, 0\n";
        out << "  br i1 %is_empty0, label %empty, label %nonempty\n";
        out << "\n";
        out << "nonempty:\n";
        emit_yield_return("ok", "0", *success_type, "%arg0");
        out << "\n";
        out << "empty:\n";
        out << "  %reason0 = insertvalue " << failure_type->llvm_type()
            << " zeroinitializer, i32 " << failure_variant_tag << ", 0\n";
        emit_yield_return("fail", "1", *failure_type, "%reason0");
        out << "}\n";
        return out.str();
    }

    case CompilerOwnedFunctionLowering::ForeignTextConversion: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: foreign text conversion function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }

        if (base_name == "cstring_payload_bytes") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::CString)
                || !has_builtin_kind(*success_type, BuiltinKind::Bytes)
                || hir_function_.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
                return std::unexpected("internal backend error: foreign text conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            module_.require_runtime_helper(RuntimeHelper::Strlen);
            out << "define " << linkage << return_type.llvm_type() << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %len0 = call i64 @strlen(ptr %arg0)\n";
            out << "  %bytes0 = insertvalue " << return_type.llvm_type() << " zeroinitializer, ptr %arg0, 0\n";
            out << "  %bytes1 = insertvalue " << return_type.llvm_type() << " %bytes0, i64 %len0, 1\n";
            out << "  ret " << return_type.llvm_type() << " %bytes1\n";
            out << "}\n";
            return out.str();
        }

        if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
            return std::unexpected("internal backend error: foreign text conversion function '"
                                   + hir_function_.qualified_name + "' does not declare fails");
        }

        const std::expected<const YieldLayout*, std::string> yield_layout_result =
            model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout_result.has_value()) {
            return std::unexpected(yield_layout_result.error());
        }
        const YieldLayout& yield_layout = **yield_layout_result;
        if (return_type.llvm_type() != yield_layout.llvm_type) {
            return std::unexpected("internal backend error: foreign text conversion function '"
                                   + hir_function_.qualified_name + "' has mismatched yield return type");
        }

        const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
        if (reason_decl.qualified_name != "ForeignAbiTextFailure") {
            return std::unexpected("internal backend error: foreign text conversion function '"
                                   + hir_function_.qualified_name + "' fails with '"
                                   + reason_decl.qualified_name + "'");
        }
        const std::expected<ResolvedType, std::string> failure_type =
            model_.resolve_type_name(reason_decl.qualified_name);
        if (!failure_type.has_value()) {
            return std::unexpected(failure_type.error());
        }
        const std::expected<const TypeLayout*, std::string> failure_layout_result =
            model_.ensure_type_layout(reason_decl.id);
        if (!failure_layout_result.has_value()) {
            return std::unexpected(failure_layout_result.error());
        }
        const TypeLayout& failure_layout = **failure_layout_result;

        const auto failure_variant_tag =
            [&](std::string_view variant_name) -> std::expected<std::uint32_t, std::string> {
            for (const hir::VariantId variant_id : reason_decl.variants) {
                const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                if (variant_decl.name != variant_name) {
                    continue;
                }
                const auto layout_it = failure_layout.variants.find(variant_id);
                if (layout_it == failure_layout.variants.end()) {
                    return std::unexpected("internal backend error: missing layout for failure variant '"
                                           + variant_decl.qualified_name + "'");
                }
                return layout_it->second.tag_value;
            }
            return std::unexpected("internal backend error: foreign text conversion function '"
                                   + hir_function_.qualified_name + "' could not find failure variant '"
                                   + std::string(variant_name) + "'");
        };

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout.llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout.payload_size == 0) {
                out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout.payload_storage_type << "\n";
            out << "  store " << yield_layout.payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout.payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout.llvm_type
                << " %" << prefix << ".tag, " << yield_layout.payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout.payload_field_index << "\n";
            out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".value\n";
        };
        const auto emit_failure_return =
            [&](std::string_view prefix,
                std::string_view variant_name) -> std::expected<BackendStepSucceeded, std::string> {
            const std::expected<std::uint32_t, std::string> tag = failure_variant_tag(variant_name);
            if (!tag.has_value()) {
                return std::unexpected(tag.error());
            }
            out << "  %" << prefix << ".reason0 = insertvalue " << failure_type->llvm_type()
                << " zeroinitializer, i32 " << *tag << ", 0\n";
            emit_yield_return(prefix, "1", *failure_type, "%" + std::string(prefix) + ".reason0");
            return BackendStepSucceeded{};
        };

        if (base_name == "cstring_payload_text") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::CString)
                || !has_builtin_kind(*success_type, BuiltinKind::Text)) {
                return std::unexpected("internal backend error: foreign text conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            module_.require_runtime_helper(RuntimeHelper::Strlen);
            module_.require_runtime_helper(RuntimeHelper::Utf8);
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %len0 = call i64 @strlen(ptr %arg0)\n";
            out << "  %utf8.ok0 = call i1 @evid.utf8.is.valid(ptr %arg0, i64 %len0)\n";
            out << "  br i1 %utf8.ok0, label %ok, label %fail\n";
            out << "\n";
            out << "ok:\n";
            out << "  %text0 = insertvalue " << success_type->llvm_type() << " zeroinitializer, ptr %arg0, 0\n";
            out << "  %text1 = insertvalue " << success_type->llvm_type() << " %text0, i64 %len0, 1\n";
            emit_yield_return("ok", "0", *success_type, "%text1");
            out << "\n";
            out << "fail:\n";
            const std::expected<BackendStepSucceeded, std::string> failure =
                emit_failure_return("fail", "CStringPayloadWasNotUtf8");
            if (!failure.has_value()) {
                return std::unexpected(failure.error());
            }
            out << "}\n";
            return out.str();
        }

        if (base_name == "text_to_cstring" || base_name == "bytes_to_cstring") {
            const BuiltinKind expected_input_kind =
                base_name == "text_to_cstring" ? BuiltinKind::Text : BuiltinKind::Bytes;
            if (!has_builtin_kind(param_types[0], expected_input_kind)
                || !has_builtin_kind(*success_type, BuiltinKind::CString)) {
                return std::unexpected("internal backend error: foreign text conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            module_.require_runtime_helper(RuntimeHelper::Malloc);
            module_.require_runtime_helper(RuntimeHelper::Memcpy);
            if (expected_input_kind == BuiltinKind::Bytes) {
                module_.require_runtime_helper(RuntimeHelper::Utf8);
            }
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %data0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 0\n";
            out << "  %len0 = extractvalue " << param_types[0].llvm_type() << " %arg0, 1\n";
            if (expected_input_kind == BuiltinKind::Bytes) {
                out << "  %utf8.ok0 = call i1 @evid.utf8.is.valid(ptr %data0, i64 %len0)\n";
                out << "  br i1 %utf8.ok0, label %scan, label %fail_utf8\n";
            } else {
                out << "  br label %scan\n";
            }
            out << "\n";
            out << "scan:\n";
            out << "  %index0 = phi i64 [ 0, %entry ], [ %next_index0, %advance ]\n";
            out << "  %at_end0 = icmp eq i64 %index0, %len0\n";
            out << "  br i1 %at_end0, label %copy, label %probe\n";
            out << "probe:\n";
            out << "  %byte_ptr0 = getelementptr i8, ptr %data0, i64 %index0\n";
            out << "  %byte0 = load i8, ptr %byte_ptr0\n";
            out << "  %is_nul0 = icmp eq i8 %byte0, 0\n";
            out << "  br i1 %is_nul0, label %fail_nul, label %advance\n";
            out << "advance:\n";
            out << "  %next_index0 = add i64 %index0, 1\n";
            out << "  br label %scan\n";
            out << "\n";
            out << "copy:\n";
            out << "  %alloc_len0 = add i64 %len0, 1\n";
            out << "  %copy0 = call ptr @malloc(i64 %alloc_len0)\n";
            out << "  call void @llvm.memcpy.p0.p0.i64(ptr %copy0, ptr %data0, i64 %len0, i1 false)\n";
            out << "  %nul_ptr0 = getelementptr i8, ptr %copy0, i64 %len0\n";
            out << "  store i8 0, ptr %nul_ptr0\n";
            emit_yield_return("ok", "0", *success_type, "%copy0");
            out << "\n";
            if (expected_input_kind == BuiltinKind::Bytes) {
                out << "fail_utf8:\n";
                const std::expected<BackendStepSucceeded, std::string> utf8_failure =
                    emit_failure_return("fail_utf8", "CStringPayloadWasNotUtf8");
                if (!utf8_failure.has_value()) {
                    return std::unexpected(utf8_failure.error());
                }
                out << "\n";
            }
            out << "fail_nul:\n";
            const std::expected<BackendStepSucceeded, std::string> nul_failure =
                emit_failure_return("fail_nul", "CStringPayloadContainedNul");
            if (!nul_failure.has_value()) {
                return std::unexpected(nul_failure.error());
            }
            out << "}\n";
            return out.str();
        }

        return std::unexpected("backend does not yet support compiler-owned function '"
                               + hir_function_.qualified_name + "'");
    }

    case CompilerOwnedFunctionLowering::ForeignIntegerConversion: {
        if (param_types.size() != 1) {
            return std::unexpected("internal backend error: foreign integer conversion function '"
                                   + hir_function_.qualified_name + "' expected one parameter");
        }

        const std::string_view base_name = function_base_name(hir_function_.qualified_name);
        const std::expected<ResolvedType, std::string> success_type =
            model_.resolve_type_name(hir_function_.return_type.text);
        if (!success_type.has_value()) {
            return std::unexpected(success_type.error());
        }

        if (base_name == "csize_to_nat") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::CSize)
                || !has_builtin_kind(*success_type, BuiltinKind::Nat)
                || hir_function_.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
                return std::unexpected("internal backend error: foreign integer conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            module_.require_runtime_helper(RuntimeHelper::Nat);
            out << "define " << linkage << return_type.llvm_type() << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %nat0 = call " << return_type.llvm_type() << " @evid.nat.from.u64(i64 %arg0)\n";
            out << "  ret " << return_type.llvm_type() << " %nat0\n";
            out << "}\n";
            return out.str();
        }

        if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
            return std::unexpected("internal backend error: foreign integer conversion function '"
                                   + hir_function_.qualified_name + "' does not declare fails");
        }

        const std::expected<const YieldLayout*, std::string> yield_layout_result =
            model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout_result.has_value()) {
            return std::unexpected(yield_layout_result.error());
        }
        const YieldLayout& yield_layout = **yield_layout_result;
        if (return_type.llvm_type() != yield_layout.llvm_type) {
            return std::unexpected("internal backend error: foreign integer conversion function '"
                                   + hir_function_.qualified_name + "' has mismatched yield return type");
        }

        const hir::TypeDecl& reason_decl = model_.type(hir_function_.failure.reason_type_id());
        if (reason_decl.qualified_name != "ForeignAbiIntegerFailure") {
            return std::unexpected("internal backend error: foreign integer conversion function '"
                                   + hir_function_.qualified_name + "' fails with '"
                                   + reason_decl.qualified_name + "'");
        }
        const std::expected<ResolvedType, std::string> failure_type =
            model_.resolve_type_name(reason_decl.qualified_name);
        if (!failure_type.has_value()) {
            return std::unexpected(failure_type.error());
        }
        const std::expected<const TypeLayout*, std::string> failure_layout_result =
            model_.ensure_type_layout(reason_decl.id);
        if (!failure_layout_result.has_value()) {
            return std::unexpected(failure_layout_result.error());
        }
        const TypeLayout& failure_layout = **failure_layout_result;

        const auto failure_variant_tag =
            [&](std::string_view variant_name) -> std::expected<std::uint32_t, std::string> {
            for (const hir::VariantId variant_id : reason_decl.variants) {
                const hir::VariantDecl& variant_decl = model_.variant(variant_id);
                if (variant_decl.name != variant_name) {
                    continue;
                }
                const auto layout_it = failure_layout.variants.find(variant_id);
                if (layout_it == failure_layout.variants.end()) {
                    return std::unexpected("internal backend error: missing layout for failure variant '"
                                           + variant_decl.qualified_name + "'");
                }
                return layout_it->second.tag_value;
            }
            return std::unexpected("internal backend error: foreign integer conversion function '"
                                   + hir_function_.qualified_name + "' could not find failure variant '"
                                   + std::string(variant_name) + "'");
        };

        const auto emit_yield_return =
            [&](std::string_view prefix,
                std::string_view wrapper_tag,
                const ResolvedType& payload_type,
                const std::string& payload_value) {
            out << "  %" << prefix << ".tag = insertvalue " << yield_layout.llvm_type
                << " zeroinitializer, i8 " << wrapper_tag << ", 0\n";
            if (yield_layout.payload_size == 0) {
                out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".tag\n";
                return;
            }
            out << "  %" << prefix << ".pack.slot = alloca " << yield_layout.payload_storage_type << "\n";
            out << "  store " << yield_layout.payload_storage_type << " zeroinitializer, ptr %"
                << prefix << ".pack.slot\n";
            out << "  store " << payload_type.llvm_type() << " " << payload_value << ", ptr %"
                << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".pack = load " << yield_layout.payload_storage_type
                << ", ptr %" << prefix << ".pack.slot\n";
            out << "  %" << prefix << ".value = insertvalue " << yield_layout.llvm_type
                << " %" << prefix << ".tag, " << yield_layout.payload_storage_type << " %"
                << prefix << ".pack, " << yield_layout.payload_field_index << "\n";
            out << "  ret " << yield_layout.llvm_type << " %" << prefix << ".value\n";
        };
        const auto emit_failure_return =
            [&](std::string_view variant_name) -> std::expected<BackendStepSucceeded, std::string> {
            const std::expected<std::uint32_t, std::string> tag = failure_variant_tag(variant_name);
            if (!tag.has_value()) {
                return std::unexpected(tag.error());
            }
            out << "  %reason0 = insertvalue " << failure_type->llvm_type()
                << " zeroinitializer, i32 " << *tag << ", 0\n";
            emit_yield_return("fail", "1", *failure_type, "%reason0");
            return BackendStepSucceeded{};
        };

        if (base_name == "cint_to_int") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::CInt)
                || !has_builtin_kind(*success_type, BuiltinKind::Int)) {
                return std::unexpected("internal backend error: foreign integer conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %wide0 = sext i32 %arg0 to i64\n";
            emit_yield_return("ok", "0", *success_type, "%wide0");
            out << "}\n";
            return out.str();
        }

        if (base_name == "cint_require_nat") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::CInt)
                || !has_builtin_kind(*success_type, BuiltinKind::Nat)) {
                return std::unexpected("internal backend error: foreign integer conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            module_.require_runtime_helper(RuntimeHelper::Nat);
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %nonnegative0 = icmp sge i32 %arg0, 0\n";
            out << "  br i1 %nonnegative0, label %ok, label %fail\n";
            out << "\n";
            out << "ok:\n";
            out << "  %wide0 = zext i32 %arg0 to i64\n";
            out << "  %nat0 = call " << success_type->llvm_type() << " @evid.nat.from.u64(i64 %wide0)\n";
            emit_yield_return("ok", "0", *success_type, "%nat0");
            out << "\n";
            out << "fail:\n";
            const std::expected<BackendStepSucceeded, std::string> failure = emit_failure_return("ForeignIntegerWasNegative");
            if (!failure.has_value()) {
                return std::unexpected(failure.error());
            }
            out << "}\n";
            return out.str();
        }

        if (base_name == "csize_to_int") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::CSize)
                || !has_builtin_kind(*success_type, BuiltinKind::Int)) {
                return std::unexpected("internal backend error: foreign integer conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %fits0 = icmp ule i64 %arg0, 9223372036854775807\n";
            out << "  br i1 %fits0, label %ok, label %fail\n";
            out << "\n";
            out << "ok:\n";
            emit_yield_return("ok", "0", *success_type, "%arg0");
            out << "\n";
            out << "fail:\n";
            const std::expected<BackendStepSucceeded, std::string> failure = emit_failure_return("ForeignIntegerExceededIntRange");
            if (!failure.has_value()) {
                return std::unexpected(failure.error());
            }
            out << "}\n";
            return out.str();
        }

        if (base_name == "int_to_cint") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::Int)
                || !has_builtin_kind(*success_type, BuiltinKind::CInt)) {
                return std::unexpected("internal backend error: foreign integer conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %lower0 = icmp sge i64 %arg0, -2147483648\n";
            out << "  %upper0 = icmp sle i64 %arg0, 2147483647\n";
            out << "  %fits0 = and i1 %lower0, %upper0\n";
            out << "  br i1 %fits0, label %ok, label %fail\n";
            out << "\n";
            out << "ok:\n";
            out << "  %narrow0 = trunc i64 %arg0 to i32\n";
            emit_yield_return("ok", "0", *success_type, "%narrow0");
            out << "\n";
            out << "fail:\n";
            const std::expected<BackendStepSucceeded, std::string> failure = emit_failure_return("CoreIntegerExceededCIntRange");
            if (!failure.has_value()) {
                return std::unexpected(failure.error());
            }
            out << "}\n";
            return out.str();
        }

        if (base_name == "nat_to_cint") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::Nat)
                || !has_builtin_kind(*success_type, BuiltinKind::CInt)) {
                return std::unexpected("internal backend error: foreign integer conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            module_.require_runtime_helper(RuntimeHelper::Nat);
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %fits.u64_0 = call i1 @evid.nat.fits.u64(" << param_types[0].llvm_type() << " %arg0)\n";
            out << "  br i1 %fits.u64_0, label %range, label %fail\n";
            out << "\n";
            out << "range:\n";
            out << "  %wide0 = call i64 @evid.nat.to.u64(" << param_types[0].llvm_type() << " %arg0)\n";
            out << "  %fits.cint0 = icmp ule i64 %wide0, 2147483647\n";
            out << "  br i1 %fits.cint0, label %ok, label %fail\n";
            out << "\n";
            out << "ok:\n";
            out << "  %narrow0 = trunc i64 %wide0 to i32\n";
            emit_yield_return("ok", "0", *success_type, "%narrow0");
            out << "\n";
            out << "fail:\n";
            const std::expected<BackendStepSucceeded, std::string> failure = emit_failure_return("CoreIntegerExceededCIntRange");
            if (!failure.has_value()) {
                return std::unexpected(failure.error());
            }
            out << "}\n";
            return out.str();
        }

        if (base_name == "int_to_csize") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::Int)
                || !has_builtin_kind(*success_type, BuiltinKind::CSize)) {
                return std::unexpected("internal backend error: foreign integer conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %nonnegative0 = icmp sge i64 %arg0, 0\n";
            out << "  br i1 %nonnegative0, label %ok, label %fail\n";
            out << "\n";
            out << "ok:\n";
            emit_yield_return("ok", "0", *success_type, "%arg0");
            out << "\n";
            out << "fail:\n";
            const std::expected<BackendStepSucceeded, std::string> failure = emit_failure_return("CoreIntegerWasNegative");
            if (!failure.has_value()) {
                return std::unexpected(failure.error());
            }
            out << "}\n";
            return out.str();
        }

        if (base_name == "nat_to_csize") {
            if (!has_builtin_kind(param_types[0], BuiltinKind::Nat)
                || !has_builtin_kind(*success_type, BuiltinKind::CSize)) {
                return std::unexpected("internal backend error: foreign integer conversion function '"
                                       + hir_function_.qualified_name + "' has unsupported signature");
            }
            module_.require_runtime_helper(RuntimeHelper::Nat);
            out << "define " << linkage << yield_layout.llvm_type << " @"
                << model_.function_symbol(hir_function_.id) << "(" << param_types[0].llvm_type() << " %arg0) {\n";
            out << "entry:\n";
            out << "  %fits0 = call i1 @evid.nat.fits.u64(" << param_types[0].llvm_type() << " %arg0)\n";
            out << "  br i1 %fits0, label %ok, label %fail\n";
            out << "\n";
            out << "ok:\n";
            out << "  %wide0 = call i64 @evid.nat.to.u64(" << param_types[0].llvm_type() << " %arg0)\n";
            emit_yield_return("ok", "0", *success_type, "%wide0");
            out << "\n";
            out << "fail:\n";
            const std::expected<BackendStepSucceeded, std::string> failure = emit_failure_return("CoreIntegerExceededCSizeRange");
            if (!failure.has_value()) {
                return std::unexpected(failure.error());
            }
            out << "}\n";
            return out.str();
        }

        return std::unexpected("backend does not yet support compiler-owned function '"
                               + hir_function_.qualified_name + "'");
    }

    case CompilerOwnedFunctionLowering::Unsupported:
        break;
    }
    return std::unexpected("backend does not yet support compiler-owned function '"
                           + hir_function_.qualified_name + "'");
}

std::expected<FunctionEmitter::TypedValue, std::string> FunctionEmitter::materialize_operand(
    const mir::Operand& operand,
    const ResolvedType& expected_type) {
    return operand.match(
        [&](mir::Operand::LocalValue local) -> std::expected<TypedValue, std::string> {
            const std::expected<const ResolvedType*, std::string> source_type = local_type(local.local_id);
            if (!source_type.has_value()) {
                return std::unexpected(source_type.error());
            }
            if ((*source_type)->source_name() != expected_type.source_name()) {
                return std::unexpected("backend type mismatch: expected '" + expected_type.source_name()
                                       + "', got '" + (*source_type)->source_name() + "'");
            }
            const std::expected<std::string, std::string> slot = local_slot(local.local_id);
            if (!slot.has_value()) {
                return std::unexpected(slot.error());
            }
            const std::expected<std::string, std::string> loaded = load_from_slot(*slot, *(*source_type));
            if (!loaded.has_value()) {
                return std::unexpected(loaded.error());
            }
            return TypedValue{expected_type, *loaded};
        },
        [&](mir::Operand::IntLiteralValue literal) -> std::expected<TypedValue, std::string> {
            if (is_builtin_nat_type(expected_type)) {
                return make_nat_literal_value(expected_type, literal.text);
            }
            const std::expected<std::string, std::string> materialized =
                materialize_number_literal_text(literal.text, expected_type, {});
            if (!materialized.has_value()) {
                return std::unexpected(materialized.error());
            }
            return TypedValue{expected_type, *materialized};
        },
        [&](mir::Operand::StringLiteralValue literal) -> std::expected<TypedValue, std::string> {
            if (expected_type.identity().category() != ResolvedTypeCategory::BuiltinType
                || (expected_type.identity().builtin_kind() != BuiltinKind::Text
                    && expected_type.identity().builtin_kind() != BuiltinKind::NonEmptyText
                    && expected_type.identity().builtin_kind() != BuiltinKind::Bytes
                    && expected_type.identity().builtin_kind() != BuiltinKind::NonEmptyBytes
                    && expected_type.identity().builtin_kind() != BuiltinKind::CString)) {
                return std::unexpected("backend only supports string literals for Text/NonEmptyText/Bytes/"
                                       "NonEmptyBytes/CString values, got '"
                                       + expected_type.source_name() + "'");
            }
            return make_string_literal_value(expected_type, literal.text);
        },
        [&](mir::Operand::UnitValue) -> std::expected<TypedValue, std::string> {
            if (expected_type.identity().category() != ResolvedTypeCategory::BuiltinType
                || expected_type.identity().builtin_kind() != BuiltinKind::Unit) {
                return std::unexpected("backend expected Unit, got '" + expected_type.source_name() + "'");
            }
            return TypedValue{expected_type, "0"};
        });
}

BackendStepResult FunctionEmitter::emit_call_argument(std::vector<std::string>& args,
                                                      const hir::FunctionDecl& callee,
                                                      std::size_t index,
                                                      const mir::Operand& operand) {
    const std::expected<ResolvedType, std::string> param_type = model_.resolve_type_name(callee.params[index].type.text);
    if (!param_type.has_value()) {
        return std::unexpected(param_type.error());
    }

    const ForeignArgumentPassing argument_passing =
        callee.implementation == ast::FunctionImplementation::ForeignImport && param_type->size() > 8
            ? ForeignArgumentPassing::Pointer
            : ForeignArgumentPassing::Direct;
    if (argument_passing == ForeignArgumentPassing::Direct) {
        const std::expected<TypedValue, std::string> arg = materialize_operand(operand, *param_type);
        if (!arg.has_value()) {
            return std::unexpected(arg.error());
        }
        args.push_back(arg->type.llvm_type() + " " + arg->value);
        return BackendStepSucceeded{};
    }

    const auto materialize_stack_argument = [&]() -> BackendStepResult {
        const std::expected<TypedValue, std::string> arg = materialize_operand(operand, *param_type);
        if (!arg.has_value()) {
            return std::unexpected(arg.error());
        }

        const std::string temp_slot = next_temp("arg");
        append_line(temp_slot + " = alloca " + arg->type.llvm_type());
        append_line("store " + arg->type.llvm_type() + " " + arg->value + ", ptr " + temp_slot);
        args.push_back("ptr " + temp_slot);
        return BackendStepSucceeded{};
    };

    return operand.match(
        [&](mir::Operand::LocalValue local) -> BackendStepResult {
            const std::expected<const ResolvedType*, std::string> source_type = local_type(local.local_id);
            if (!source_type.has_value()) {
                return std::unexpected(source_type.error());
            }
            if ((*source_type)->source_name() != param_type->source_name()) {
                return std::unexpected("backend type mismatch for call argument " + std::to_string(index)
                                       + " of foreign function '" + callee.qualified_name + "': expected '"
                                       + param_type->source_name() + "', got '" + (*source_type)->source_name() + "'");
            }
            const std::expected<std::string, std::string> slot = local_slot(local.local_id);
            if (!slot.has_value()) {
                return std::unexpected(slot.error());
            }
            args.push_back("ptr " + *slot);
            return BackendStepSucceeded{};
        },
        [&](mir::Operand::IntLiteralValue) -> BackendStepResult {
            return materialize_stack_argument();
        },
        [&](mir::Operand::StringLiteralValue) -> BackendStepResult {
            return materialize_stack_argument();
        },
        [&](mir::Operand::UnitValue) -> BackendStepResult {
            return materialize_stack_argument();
        });
}

BackendStepResult FunctionEmitter::emit_assign_use(mir::LocalId dest, const mir::Operand& operand) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }

    const auto store_materialized_operand = [&]() -> BackendStepResult {
        const std::expected<TypedValue, std::string> value = materialize_operand(operand, *(*dest_type));
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        return store_typed_value(*dest_slot, value->type, value->value);
    };

    return operand.match(
        [&](mir::Operand::LocalValue) -> BackendStepResult {
            return store_materialized_operand();
        },
        [&](mir::Operand::IntLiteralValue) -> BackendStepResult {
            return store_materialized_operand();
        },
        [&](mir::Operand::StringLiteralValue literal) -> BackendStepResult {
            return store_string_literal(*dest_slot, *(*dest_type), literal.text);
        },
        [&](mir::Operand::UnitValue) -> BackendStepResult {
            return store_materialized_operand();
        });
}

BackendStepResult FunctionEmitter::emit_assign_call(mir::LocalId dest, mir::Rvalue::CallValue value) {
    const hir::FunctionDecl& callee = model_.function(value.function_id);
    if (!callee.generics.empty()) {
        return std::unexpected("backend does not yet support generic function '" + callee.qualified_name + "'");
    }
    if (callee.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
        return std::unexpected("internal backend error: `fails` call used as a plain assignment for '"
                               + callee.qualified_name + "'");
    }

    const std::expected<ResolvedType, std::string> return_type = model_.resolve_type_name(callee.return_type.text);
    if (!return_type.has_value()) {
        return std::unexpected(return_type.error());
    }

    std::vector<std::string> args;
    args.reserve(value.args.size());
    for (std::size_t index = 0; index < value.args.size(); ++index) {
        if (materialization_of(callee.params[index].type.discipline)
            == typesys::DisciplineMaterialization::CompileTimeOnly) {
            if (compile_time_argument_encoding_of(value.args[index])
                != CompileTimeArgumentEncoding::ErasedToUnitOperand) {
                return std::unexpected("internal backend error: compile-time-only parameter "
                                       + std::to_string(index) + " of callee '" + callee.qualified_name
                                       + "' was not erased");
            }
            continue;
        }
        const BackendStepResult arg = emit_call_argument(args, callee, index, value.args[index]);
        if (!arg.has_value()) {
            return std::unexpected(arg.error());
        }
    }

    const std::string call_value = next_temp("call");
    std::ostringstream call;
    call << call_value << " = call " << return_type->llvm_type() << " @" << model_.function_symbol(callee.id) << '(';
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            call << ", ";
        }
        call << args[index];
    }
    call << ')';
    append_line(call.str());

    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }
    return store_typed_value(*dest_slot, *return_type, call_value);
}

BackendStepResult FunctionEmitter::emit_assign_construct(mir::LocalId dest,
                                                         mir::Rvalue::ConstructNamedTypeValue value) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }
    if ((*dest_type)->identity().category() != ResolvedTypeCategory::PackageTypeDeclaration) {
        return std::unexpected("backend only supports constructing user-defined records and variants, got '"
                               + (*dest_type)->source_name() + "'");
    }

    auto& mutable_model = model_;
    if ((*dest_type)->identity().package_type_id() != value.owner_type_id) {
        return std::unexpected("internal backend error: construct owner type mismatch for '" + value.qualified_name + "'");
    }
    const std::expected<const TypeLayout*, std::string> owner_layout = mutable_model.ensure_type_layout(value.owner_type_id);
    if (!owner_layout.has_value()) {
        return std::unexpected(owner_layout.error());
    }

    std::string record_value = "zeroinitializer";
    for (const mir::FieldValue& field : value.fields) {
        const auto field_it = (*owner_layout)->field_indices.find(field.field_name());
        if (field_it == (*owner_layout)->field_indices.end()) {
            return std::unexpected("backend could not resolve field '" + field.field_name()
                                   + "' in '" + value.qualified_name + "'");
        }
        const FieldLayout& field_layout = (*owner_layout)->fields[field_it->second];
        const ResolvedType field_type = resolved_type_from_field(field_layout);
        const std::expected<TypedValue, std::string> field_value = materialize_operand(field.operand(), field_type);
        if (!field_value.has_value()) {
            return std::unexpected(field_value.error());
        }
        const std::expected<std::string, std::string> inserted = insert_value(
            (*owner_layout)->llvm_type,
            record_value,
            field_type.llvm_type(),
            field_value->value,
            field_layout.index());
        if (!inserted.has_value()) {
            return std::unexpected(inserted.error());
        }
        record_value = *inserted;
    }

    return store_typed_value(*dest_slot, *(*dest_type), record_value);
}

BackendStepResult FunctionEmitter::emit_assign_construct(mir::LocalId dest,
                                                         mir::Rvalue::ConstructNamedVariantValue value) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }
    if ((*dest_type)->identity().category() != ResolvedTypeCategory::PackageTypeDeclaration) {
        return std::unexpected("backend only supports constructing user-defined records and variants, got '"
                               + (*dest_type)->source_name() + "'");
    }

    auto& mutable_model = model_;
    if ((*dest_type)->identity().package_type_id() != value.owner_type_id) {
        return std::unexpected("internal backend error: construct owner type mismatch for '" + value.qualified_name + "'");
    }
    const std::expected<const TypeLayout*, std::string> owner_layout = mutable_model.ensure_type_layout(value.owner_type_id);
    if (!owner_layout.has_value()) {
        return std::unexpected(owner_layout.error());
    }

    const auto variant_it = (*owner_layout)->variants.find(value.variant_id);
    if (variant_it == (*owner_layout)->variants.end()) {
        return std::unexpected("backend could not resolve variant layout for '" + value.qualified_name + "'");
    }
    const VariantLayout& variant_layout = variant_it->second;

    std::expected<std::string, std::string> aggregate_value = insert_value(
        (*owner_layout)->llvm_type,
        "zeroinitializer",
        "i32",
        std::to_string(variant_layout.tag_value),
        0);
    if (!aggregate_value.has_value()) {
        return std::unexpected(aggregate_value.error());
    }

    if (!value.fields.empty() && (*owner_layout)->payload_state == PayloadStorageState::CarriesPayload) {
        std::string payload_value = "zeroinitializer";
        for (const mir::FieldValue& field : value.fields) {
            const auto field_it = variant_layout.field_indices.find(field.field_name());
            if (field_it == variant_layout.field_indices.end()) {
                return std::unexpected("backend could not resolve variant field '" + field.field_name()
                                       + "' in '" + value.qualified_name + "'");
            }
            const FieldLayout& field_layout = variant_layout.fields[field_it->second];
            const ResolvedType field_type = resolved_type_from_field(field_layout);
            const std::expected<TypedValue, std::string> field_value = materialize_operand(field.operand(), field_type);
            if (!field_value.has_value()) {
                return std::unexpected(field_value.error());
            }
            const std::expected<std::string, std::string> inserted_payload = insert_value(
                variant_layout.payload_llvm_type,
                payload_value,
                field_type.llvm_type(),
                field_value->value,
                field_layout.index());
            if (!inserted_payload.has_value()) {
                return std::unexpected(inserted_payload.error());
            }
            payload_value = *inserted_payload;
        }

        const ResolvedType payload_type = resolved_aggregate_type(
            variant_layout.payload_llvm_type,
            variant_layout.payload_llvm_type,
            variant_layout.payload_size,
            variant_layout.payload_align);
        const std::expected<std::string, std::string> packed_payload = pack_value_for_storage(
            TypedValue{payload_type, payload_value},
            (*owner_layout)->payload_storage_type,
            (*owner_layout)->payload_size);
        if (!packed_payload.has_value()) {
            return std::unexpected(packed_payload.error());
        }
        aggregate_value = insert_value(
            (*owner_layout)->llvm_type,
            *aggregate_value,
            (*owner_layout)->payload_storage_type,
            *packed_payload,
            (*owner_layout)->payload_field_index);
        if (!aggregate_value.has_value()) {
            return std::unexpected(aggregate_value.error());
        }
    }

    return store_typed_value(*dest_slot, *(*dest_type), *aggregate_value);
}

BackendStepResult FunctionEmitter::emit_assign_project_field(mir::LocalId dest,
                                                             mir::Rvalue::ProjectNamedTypeFieldValue value) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }
    const std::expected<std::string, std::string> base_slot = local_slot(value.base_local);
    if (!base_slot.has_value()) {
        return std::unexpected(base_slot.error());
    }

    const std::expected<const TypeLayout*, std::string> owner_layout = user_layout_for_local(value.base_local);
    if (!owner_layout.has_value()) {
        return std::unexpected(owner_layout.error());
    }
    const auto field_it = (*owner_layout)->field_indices.find(value.field_name);
    if (field_it == (*owner_layout)->field_indices.end()) {
        return std::unexpected("backend could not resolve projected field '" + value.field_name + "'");
    }
    const FieldLayout& field_layout = (*owner_layout)->fields[field_it->second];
    const ResolvedType field_type = resolved_type_from_field(field_layout);
    const std::expected<const ResolvedType*, std::string> base_type = local_type(value.base_local);
    if (!base_type.has_value()) {
        return std::unexpected(base_type.error());
    }
    const std::expected<std::string, std::string> base_value = load_from_slot(*base_slot, *(*base_type));
    if (!base_value.has_value()) {
        return std::unexpected(base_value.error());
    }
    const std::expected<std::string, std::string> field_value = extract_value(
        (*owner_layout)->llvm_type,
        *base_value,
        field_layout.index());
    if (!field_value.has_value()) {
        return std::unexpected(field_value.error());
    }
    return store_typed_value(*dest_slot, *(*dest_type), *field_value);
}

BackendStepResult FunctionEmitter::emit_assign_project_field(
    mir::LocalId dest,
    mir::Rvalue::ProjectNamedVariantPayloadFieldValue value) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }
    const std::expected<std::string, std::string> base_slot = local_slot(value.base_local);
    if (!base_slot.has_value()) {
        return std::unexpected(base_slot.error());
    }

    auto& mutable_model = model_;
    const std::expected<const TypeLayout*, std::string> owner_layout = mutable_model.ensure_type_layout(
        value.projection_owner_type_id);
    if (!owner_layout.has_value()) {
        return std::unexpected(owner_layout.error());
    }
    const std::expected<const ResolvedType*, std::string> base_type = local_type(value.base_local);
    if (!base_type.has_value()) {
        return std::unexpected(base_type.error());
    }
    const auto variant_it = (*owner_layout)->variants.find(value.variant_id);
    if (variant_it == (*owner_layout)->variants.end()) {
        return std::unexpected("backend could not resolve projection variant for field '" + value.field_name + "'");
    }
    const VariantLayout& variant_layout = variant_it->second;
    const auto field_it = variant_layout.field_indices.find(value.field_name);
    if (field_it == variant_layout.field_indices.end()) {
        return std::unexpected("backend could not resolve projected field '" + value.field_name + "'");
    }
    const std::expected<std::string, std::string> base_value = load_from_slot(*base_slot, *(*base_type));
    if (!base_value.has_value()) {
        return std::unexpected(base_value.error());
    }
    const std::expected<std::string, std::string> raw_payload = extract_value(
        (*owner_layout)->llvm_type,
        *base_value,
        (*owner_layout)->payload_field_index);
    if (!raw_payload.has_value()) {
        return std::unexpected(raw_payload.error());
    }
    const ResolvedType payload_type = resolved_aggregate_type(
        variant_layout.payload_llvm_type,
        variant_layout.payload_llvm_type,
        variant_layout.payload_size,
        variant_layout.payload_align);
    const std::expected<TypedValue, std::string> payload_value = unpack_value_from_storage(
        *raw_payload,
        (*owner_layout)->payload_storage_type,
        payload_type);
    if (!payload_value.has_value()) {
        return std::unexpected(payload_value.error());
    }
    const FieldLayout& field_layout = variant_layout.fields[field_it->second];
    const ResolvedType field_type = resolved_type_from_field(field_layout);
    const std::expected<std::string, std::string> field_value = extract_value(
        variant_layout.payload_llvm_type,
        payload_value->value,
        field_layout.index());
    if (!field_value.has_value()) {
        return std::unexpected(field_value.error());
    }
    return store_typed_value(*dest_slot, *(*dest_type), *field_value);
}

BackendStepResult FunctionEmitter::emit_assign_project_list_element(
    mir::LocalId dest,
    mir::Rvalue::ProjectListElementValue value) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }
    const std::expected<const ResolvedType*, std::string> list_type = local_type(value.list_local);
    if (!list_type.has_value()) {
        return std::unexpected(list_type.error());
    }
    if ((*list_type)->identity().category() != ResolvedTypeCategory::BuiltinType
        || ((*list_type)->identity().builtin_kind() != BuiltinKind::List
            && (*list_type)->identity().builtin_kind() != BuiltinKind::NonEmptyList)) {
        return std::unexpected("internal backend error: list element projection expected List<T> or NonEmptyList<T>, got '"
                               + (*list_type)->source_name() + "'");
    }
    const std::expected<const ResolvedType*, std::string> index_type = local_type(value.index_local);
    if (!index_type.has_value()) {
        return std::unexpected(index_type.error());
    }
    if ((*index_type)->identity().category() != ResolvedTypeCategory::BuiltinType
        || (*index_type)->identity().builtin_kind() != BuiltinKind::Nat) {
        return std::unexpected("internal backend error: list element projection index expected Nat, got '"
                               + (*index_type)->source_name() + "'");
    }
    const std::expected<std::string, std::string> list_slot = local_slot(value.list_local);
    if (!list_slot.has_value()) {
        return std::unexpected(list_slot.error());
    }
    const std::expected<std::string, std::string> index_slot = local_slot(value.index_local);
    if (!index_slot.has_value()) {
        return std::unexpected(index_slot.error());
    }

    const std::expected<std::string, std::string> list_value = load_from_slot(*list_slot, *(*list_type));
    if (!list_value.has_value()) {
        return std::unexpected(list_value.error());
    }
    const std::expected<std::string, std::string> index_value = load_from_slot(*index_slot, *(*index_type));
    if (!index_value.has_value()) {
        return std::unexpected(index_value.error());
    }
    const std::expected<std::string, std::string> index_u64 = convert_nat_to_u64(*index_value);
    if (!index_u64.has_value()) {
        return std::unexpected(index_u64.error());
    }
    const std::expected<std::string, std::string> data_value =
        extract_value((*list_type)->llvm_type(), *list_value, 0);
    if (!data_value.has_value()) {
        return std::unexpected(data_value.error());
    }

    const std::size_t stride = element_storage_stride_for((*dest_type)->size(), (*dest_type)->align());
    const std::string offset = next_temp("list.offset");
    append_line(offset + " = mul i64 " + *index_u64 + ", " + std::to_string(stride));
    const std::string element_ptr = next_temp("list.element");
    append_line(element_ptr + " = getelementptr i8, ptr " + *data_value + ", i64 " + offset);
    const std::string element_value = next_temp("list.load");
    append_line(element_value + " = load " + (*dest_type)->llvm_type() + ", ptr " + element_ptr);
    return store_typed_value(*dest_slot, *(*dest_type), element_value);
}

BackendStepResult FunctionEmitter::emit_assign_add_nat(mir::LocalId dest,
                                                       mir::Rvalue::AddNatValue value) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    if ((*dest_type)->identity().category() != ResolvedTypeCategory::BuiltinType
        || (*dest_type)->identity().builtin_kind() != BuiltinKind::Nat) {
        return std::unexpected("internal backend error: Nat addition destination expected Nat, got '"
                               + (*dest_type)->source_name() + "'");
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }
    const std::expected<TypedValue, std::string> lhs = materialize_operand(value.lhs, *(*dest_type));
    if (!lhs.has_value()) {
        return std::unexpected(lhs.error());
    }
    const std::expected<TypedValue, std::string> rhs = materialize_operand(value.rhs, *(*dest_type));
    if (!rhs.has_value()) {
        return std::unexpected(rhs.error());
    }
    module_.require_runtime_helper(RuntimeHelper::Nat);
    const std::string sum = next_temp("nat.add");
    append_line(sum + " = call { ptr, i64 } @evid.nat.add({ ptr, i64 } " + lhs->value
                + ", { ptr, i64 } " + rhs->value + ")");
    return store_typed_value(*dest_slot, *(*dest_type), sum);
}

BackendStepResult FunctionEmitter::emit_statement(const mir::Statement& statement) {
    const mir::LocalId statement_dest = statement.dest_local();
    const mir::Rvalue& statement_value = statement.rvalue();

    return statement_value.match(
        [&](mir::Rvalue::UseValue value) -> BackendStepResult {
            return emit_assign_use(statement_dest, value.operand);
        },
        [&](mir::Rvalue::CallValue value) -> BackendStepResult {
            return emit_assign_call(statement_dest, value);
        },
        [&](mir::Rvalue::ConstructNamedTypeValue value) -> BackendStepResult {
            return emit_assign_construct(statement_dest, value);
        },
        [&](mir::Rvalue::ConstructNamedVariantValue value) -> BackendStepResult {
            return emit_assign_construct(statement_dest, value);
        },
        [&](mir::Rvalue::ProjectNamedTypeFieldValue value) -> BackendStepResult {
            return emit_assign_project_field(statement_dest, value);
        },
        [&](mir::Rvalue::ProjectNamedVariantPayloadFieldValue value) -> BackendStepResult {
            return emit_assign_project_field(statement_dest, value);
        },
        [&](mir::Rvalue::ProjectListElementValue value) -> BackendStepResult {
            return emit_assign_project_list_element(statement_dest, value);
        },
        [&](mir::Rvalue::AddNatValue value) -> BackendStepResult {
            return emit_assign_add_nat(statement_dest, value);
        });
}

BackendStepResult FunctionEmitter::emit_success_or_failure_return(const mir::Operand& operand,
                                                                  YieldReturnKind return_kind) {
    auto& mutable_model = model_;
    const std::expected<const YieldLayout*, std::string> yield_layout = mutable_model.ensure_yield_layout(hir_function_.id);
    if (!yield_layout.has_value()) {
        return std::unexpected(yield_layout.error());
    }

    const std::string payload_type_name = return_kind == YieldReturnKind::Failure
        ? model_.type(hir_function_.failure.reason_type_id()).qualified_name
        : hir_function_.return_type.text;
    const std::string wrapper_tag = return_kind == YieldReturnKind::Failure ? "1" : "0";
    const std::expected<ResolvedType, std::string> payload_type = model_.resolve_type_name(
        payload_type_name);
    if (!payload_type.has_value()) {
        return std::unexpected(payload_type.error());
    }
    const std::expected<TypedValue, std::string> payload_value = materialize_operand(operand, *payload_type);
    if (!payload_value.has_value()) {
        return std::unexpected(payload_value.error());
    }

    std::expected<std::string, std::string> wrapper_value = insert_value(
        (*yield_layout)->llvm_type,
        "zeroinitializer",
        "i8",
        wrapper_tag,
        0);
    if (!wrapper_value.has_value()) {
        return std::unexpected(wrapper_value.error());
    }

    if ((*yield_layout)->payload_size > 0) {
        const std::expected<std::string, std::string> packed_payload = pack_value_for_storage(
            *payload_value,
            (*yield_layout)->payload_storage_type,
            (*yield_layout)->payload_size);
        if (!packed_payload.has_value()) {
            return std::unexpected(packed_payload.error());
        }
        wrapper_value = insert_value(
            (*yield_layout)->llvm_type,
            *wrapper_value,
            (*yield_layout)->payload_storage_type,
            *packed_payload,
            (*yield_layout)->payload_field_index);
        if (!wrapper_value.has_value()) {
            return std::unexpected(wrapper_value.error());
        }
    }

    append_line("ret " + (*yield_layout)->llvm_type + " " + *wrapper_value);
    return BackendStepSucceeded{};
}

BackendStepResult FunctionEmitter::emit_function_return(const mir::Operand& operand) {
    if (hir_function_.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
        return emit_success_or_failure_return(operand, YieldReturnKind::Success);
    }

    const std::expected<ResolvedType, std::string> return_type = model_.resolve_type_name(hir_function_.return_type.text);
    if (!return_type.has_value()) {
        return std::unexpected(return_type.error());
    }
    const std::expected<TypedValue, std::string> value = materialize_operand(operand, *return_type);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    append_line("ret " + return_type->llvm_type() + " " + value->value);
    return BackendStepSucceeded{};
}

BackendStepResult FunctionEmitter::emit_function_fail(const mir::Operand& operand) {
    if (hir_function_.failure.behavior() != hir::FunctionFailureBehavior::YieldsReason) {
        return std::unexpected("internal backend error: `fail` terminator in non-yielding function '"
                               + hir_function_.qualified_name + "'");
    }
    return emit_success_or_failure_return(operand, YieldReturnKind::Failure);
}

BackendStepResult FunctionEmitter::emit_switch_variant(mir::Terminator::SwitchVariantValue terminator) {
    const std::expected<const TypeLayout*, std::string> layout = user_layout_for_local(terminator.scrutinee_local);
    if (!layout.has_value()) {
        return std::unexpected(layout.error());
    }
    const std::expected<const ResolvedType*, std::string> scrutinee_type = local_type(terminator.scrutinee_local);
    if (!scrutinee_type.has_value()) {
        return std::unexpected(scrutinee_type.error());
    }
    const std::expected<std::string, std::string> scrutinee_slot = local_slot(terminator.scrutinee_local);
    if (!scrutinee_slot.has_value()) {
        return std::unexpected(scrutinee_slot.error());
    }

    const std::expected<std::string, std::string> scrutinee_value = load_from_slot(*scrutinee_slot, *(*scrutinee_type));
    if (!scrutinee_value.has_value()) {
        return std::unexpected(scrutinee_value.error());
    }
    const std::expected<std::string, std::string> tag_value = extract_value(
        (*layout)->llvm_type,
        *scrutinee_value,
        0);
    if (!tag_value.has_value()) {
        return std::unexpected(tag_value.error());
    }

    const std::string default_block = next_aux_block();
    append_line("switch i32 " + *tag_value + ", label %" + default_block + " [");
    for (const mir::SwitchEdge& edge : terminator.edges) {
        append_line("  i32 " + std::to_string(edge.variant_id()) + ", label %" + block_name(edge.target_block()));
    }
    append_line("]");

    std::ostringstream unreachable_block;
    unreachable_block << default_block << ":\n";
    unreachable_block << "  unreachable\n";
    extra_blocks_.push_back(unreachable_block.str());
    return BackendStepSucceeded{};
}

BackendStepResult FunctionEmitter::emit_branch_list_element(
    mir::Terminator::BranchListElementValue terminator) {
    const std::expected<const ResolvedType*, std::string> list_type = local_type(terminator.list_local);
    if (!list_type.has_value()) {
        return std::unexpected(list_type.error());
    }
    if ((*list_type)->identity().category() != ResolvedTypeCategory::BuiltinType
        || ((*list_type)->identity().builtin_kind() != BuiltinKind::List
            && (*list_type)->identity().builtin_kind() != BuiltinKind::NonEmptyList)) {
        return std::unexpected("internal backend error: list branch expected List<T> or NonEmptyList<T>, got '"
                               + (*list_type)->source_name() + "'");
    }
    const std::expected<const ResolvedType*, std::string> index_type = local_type(terminator.index_local);
    if (!index_type.has_value()) {
        return std::unexpected(index_type.error());
    }
    if ((*index_type)->identity().category() != ResolvedTypeCategory::BuiltinType
        || (*index_type)->identity().builtin_kind() != BuiltinKind::Nat) {
        return std::unexpected("internal backend error: list branch index expected Nat, got '"
                               + (*index_type)->source_name() + "'");
    }
    const std::expected<std::string, std::string> list_slot = local_slot(terminator.list_local);
    if (!list_slot.has_value()) {
        return std::unexpected(list_slot.error());
    }
    const std::expected<std::string, std::string> index_slot = local_slot(terminator.index_local);
    if (!index_slot.has_value()) {
        return std::unexpected(index_slot.error());
    }
    const std::expected<std::string, std::string> list_value = load_from_slot(*list_slot, *(*list_type));
    if (!list_value.has_value()) {
        return std::unexpected(list_value.error());
    }
    const std::expected<std::string, std::string> index_value = load_from_slot(*index_slot, *(*index_type));
    if (!index_value.has_value()) {
        return std::unexpected(index_value.error());
    }
    const std::expected<std::string, std::string> index_u64 = convert_nat_to_u64(*index_value);
    if (!index_u64.has_value()) {
        return std::unexpected(index_u64.error());
    }
    const std::expected<std::string, std::string> count_value =
        extract_value((*list_type)->llvm_type(), *list_value, 1);
    if (!count_value.has_value()) {
        return std::unexpected(count_value.error());
    }
    const std::string has_element = next_temp("list.has");
    append_line(has_element + " = icmp ult i64 " + *index_u64 + ", " + *count_value);
    append_line("br i1 " + has_element
                + ", label %" + block_name(terminator.element_block)
                + ", label %" + block_name(terminator.empty_block));
    return BackendStepSucceeded{};
}

BackendStepResult FunctionEmitter::emit_invoke(mir::Terminator::InvokeValue terminator) {
    const hir::FunctionDecl& callee = model_.function(terminator.function_id);
    auto& mutable_model = model_;
    const std::expected<const YieldLayout*, std::string> yield_layout = mutable_model.ensure_yield_layout(callee.id);
    if (!yield_layout.has_value()) {
        return std::unexpected(yield_layout.error());
    }

    std::vector<std::string> args;
    args.reserve(terminator.args.size());
    for (std::size_t index = 0; index < terminator.args.size(); ++index) {
        if (materialization_of(callee.params[index].type.discipline)
            == typesys::DisciplineMaterialization::CompileTimeOnly) {
            if (compile_time_argument_encoding_of(terminator.args[index])
                != CompileTimeArgumentEncoding::ErasedToUnitOperand) {
                return std::unexpected("internal backend error: compile-time-only parameter "
                                       + std::to_string(index) + " of callee '" + callee.qualified_name
                                       + "' was not erased");
            }
            continue;
        }
        const BackendStepResult arg = emit_call_argument(args, callee, index, terminator.args[index]);
        if (!arg.has_value()) {
            return std::unexpected(arg.error());
        }
    }

    const std::string call_value = next_temp("invoke");
    std::ostringstream call;
    call << call_value << " = call " << (*yield_layout)->llvm_type << " @" << model_.function_symbol(callee.id) << '(';
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            call << ", ";
        }
        call << args[index];
    }
    call << ')';
    append_line(call.str());

    const std::expected<std::string, std::string> tag_value = extract_value(
        (*yield_layout)->llvm_type,
        call_value,
        0);
    if (!tag_value.has_value()) {
        return std::unexpected(tag_value.error());
    }

    const std::expected<const ResolvedType*, std::string> success_type = local_type(terminator.success_local);
    if (!success_type.has_value()) {
        return std::unexpected(success_type.error());
    }
    const std::expected<std::string, std::string> success_slot = local_slot(terminator.success_local);
    if (!success_slot.has_value()) {
        return std::unexpected(success_slot.error());
    }
    const std::expected<const ResolvedType*, std::string> failure_type = local_type(terminator.failure_local);
    if (!failure_type.has_value()) {
        return std::unexpected(failure_type.error());
    }
    const std::expected<std::string, std::string> failure_slot = local_slot(terminator.failure_local);
    if (!failure_slot.has_value()) {
        return std::unexpected(failure_slot.error());
    }

    std::string success_value = zero_value(*(*success_type));
    std::string failure_value = zero_value(*(*failure_type));
    if ((*yield_layout)->payload_size > 0) {
        const std::expected<std::string, std::string> raw_payload = extract_value(
            (*yield_layout)->llvm_type,
            call_value,
            (*yield_layout)->payload_field_index);
        if (!raw_payload.has_value()) {
            return std::unexpected(raw_payload.error());
        }
        const std::expected<TypedValue, std::string> unpacked_success = unpack_value_from_storage(
            *raw_payload,
            (*yield_layout)->payload_storage_type,
            *(*success_type));
        if (!unpacked_success.has_value()) {
            return std::unexpected(unpacked_success.error());
        }
        const std::expected<TypedValue, std::string> unpacked_failure = unpack_value_from_storage(
            *raw_payload,
            (*yield_layout)->payload_storage_type,
            *(*failure_type));
        if (!unpacked_failure.has_value()) {
            return std::unexpected(unpacked_failure.error());
        }
        success_value = unpacked_success->value;
        failure_value = unpacked_failure->value;
    }

    const std::string success_dispatch_block = next_aux_block("invoke.ok");
    const std::string failure_dispatch_block = next_aux_block("invoke.fail");
    const std::string default_block = next_aux_block();
    append_line("switch i8 " + *tag_value + ", label %" + default_block + " [");
    append_line("  i8 0, label %" + success_dispatch_block);
    append_line("  i8 1, label %" + failure_dispatch_block);
    append_line("]");

    std::ostringstream success_block;
    success_block << success_dispatch_block << ":\n";
    success_block << "  store " << (*success_type)->llvm_type() << ' ' << success_value << ", ptr " << *success_slot << '\n';
    success_block << "  br label %" << block_name(terminator.success_block) << '\n';
    extra_blocks_.push_back(success_block.str());

    std::ostringstream failure_block;
    failure_block << failure_dispatch_block << ":\n";
    failure_block << "  store " << (*failure_type)->llvm_type() << ' ' << failure_value << ", ptr " << *failure_slot << '\n';
    failure_block << "  br label %" << block_name(terminator.failure_block) << '\n';
    extra_blocks_.push_back(failure_block.str());

    std::ostringstream unreachable_block;
    unreachable_block << default_block << ":\n";
    unreachable_block << "  unreachable\n";
    extra_blocks_.push_back(unreachable_block.str());
    return BackendStepSucceeded{};
}

BackendStepResult FunctionEmitter::emit_terminator(const mir::Terminator& terminator) {
    return terminator.match(
        [&](mir::Terminator::ReturnValue terminator) -> BackendStepResult {
            return emit_function_return(terminator.value);
        },
        [&](mir::Terminator::FailValue terminator) -> BackendStepResult {
            return emit_function_fail(terminator.reason);
        },
        [&](mir::Terminator::GotoValue terminator) -> BackendStepResult {
            append_line("br label %" + block_name(terminator.target_block));
            return BackendStepSucceeded{};
        },
        [&](mir::Terminator::SwitchVariantValue terminator) -> BackendStepResult {
            return emit_switch_variant(terminator);
        },
        [&](mir::Terminator::BranchListElementValue terminator) -> BackendStepResult {
            return emit_branch_list_element(terminator);
        },
        [&](mir::Terminator::InvokeValue terminator) -> BackendStepResult {
            return emit_invoke(terminator);
        },
        [&](mir::Terminator::UnreachableValue) -> BackendStepResult {
            append_line("unreachable");
            return BackendStepSucceeded{};
        });
}

std::expected<std::string, std::string> FunctionEmitter::emit() {
    if (const BackendStepResult prepared = prepare_locals(); !prepared.has_value()) {
        return std::unexpected(prepared.error());
    }

    std::expected<ResolvedType, std::string> signature_return_type = model_.resolve_type_name(hir_function_.return_type.text);
    if (hir_function_.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
        const std::expected<const YieldLayout*, std::string> yield_layout = model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout.has_value()) {
            return std::unexpected(yield_layout.error());
        }
        signature_return_type = ResolvedType::runtime_value("",
                                                            (*yield_layout)->llvm_type,
                                                            0,
                                                            1,
                                                            RuntimeStorageShape::Aggregate,
                                                            ResolvedTypeIdentity::backend_aggregate_storage());
    }
    if (!signature_return_type.has_value()) {
        return std::unexpected(signature_return_type.error());
    }

    std::ostringstream out;
    if (hir_function_.implementation == ast::FunctionImplementation::ForeignImport) {
        out << "declare " << signature_return_type->llvm_type() << " @" << model_.function_symbol(hir_function_.id) << '(';
        std::vector<std::string> runtime_params;
        runtime_params.reserve(hir_function_.params.size());
        for (std::size_t index = 0; index < hir_function_.params.size(); ++index) {
            if (materialization_of(hir_function_.params[index].type.discipline)
                == typesys::DisciplineMaterialization::CompileTimeOnly) {
                continue;
            }
            const std::expected<ResolvedType, std::string> param_type = model_.resolve_type_name(hir_function_.params[index].type.text);
            if (!param_type.has_value()) {
                return std::unexpected(param_type.error());
            }
            if (param_type->size() > 8) {
                runtime_params.push_back("ptr");
                continue;
            }
            runtime_params.push_back(param_type->llvm_type());
        }
        for (std::size_t index = 0; index < runtime_params.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            out << runtime_params[index];
        }
        out << ")\n";
        return out.str();
    }

    if (hir_function_.body == nullptr) {
        return emit_compiler_owned_function(*signature_return_type);
    }

    const std::string linkage = hir_function_.visibility == ast::Visibility::Public ? "" : "internal ";
    out << "define " << linkage << signature_return_type->llvm_type() << " @" << model_.function_symbol(hir_function_.id)
        << '(';
    std::vector<std::string> runtime_params;
    runtime_params.reserve(hir_function_.params.size());
    std::size_t runtime_arg_index = 0;
    for (std::size_t index = 0; index < hir_function_.params.size(); ++index) {
        if (materialization_of(hir_function_.params[index].type.discipline)
            == typesys::DisciplineMaterialization::CompileTimeOnly) {
            continue;
        }
        const std::expected<ResolvedType, std::string> param_type = model_.resolve_type_name(hir_function_.params[index].type.text);
        if (!param_type.has_value()) {
            return std::unexpected(param_type.error());
        }
        runtime_params.push_back(param_type->llvm_type() + " %arg" + std::to_string(runtime_arg_index++));
    }
    for (std::size_t index = 0; index < runtime_params.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << runtime_params[index];
    }
    out << ") {\n";
    out << "entry:\n";

    for (const mir::Local& local : mir_function_.locals()) {
        if (materialization_of(local.discipline()) == typesys::DisciplineMaterialization::CompileTimeOnly) {
            continue;
        }
        const std::expected<const ResolvedType*, std::string> local_type_result = local_type(local.id());
        if (!local_type_result.has_value()) {
            return std::unexpected(local_type_result.error());
        }
        emit_instruction(out, *local_slot(local.id()) + " = alloca " + (*local_type_result)->llvm_type());
    }
    runtime_arg_index = 0;
    for (std::size_t index = 0; index < hir_function_.params.size(); ++index) {
        if (materialization_of(hir_function_.params[index].type.discipline)
            == typesys::DisciplineMaterialization::CompileTimeOnly) {
            continue;
        }
        const mir::LocalId local_id = mir_function_.locals().at(index).id();
        const std::expected<const ResolvedType*, std::string> param_type = local_type(local_id);
        if (!param_type.has_value()) {
            return std::unexpected(param_type.error());
        }
        emit_instruction(out, "store " + (*param_type)->llvm_type() + " %arg" + std::to_string(runtime_arg_index++)
                              + ", ptr " + *local_slot(local_id));
    }

    if (mir_function_.blocks().empty()) {
        emit_instruction(out, "unreachable");
        out << "}\n";
        return out.str();
    }

    emit_instruction(out, "br label %" + block_name(mir_function_.blocks().front().id()));
    out << '\n';

    for (const mir::BasicBlock& block : mir_function_.blocks()) {
        out << block_name(block.id()) << ":\n";
        emit_block_lines(out, block_preludes_.at(block.id()));
        current_block_lines_.clear();
        for (const mir::Statement& statement : block.statements()) {
            if (const BackendStepResult emitted = emit_statement(statement); !emitted.has_value()) {
                return std::unexpected(emitted.error());
            }
        }
        if (const BackendStepResult emitted = emit_terminator(block.terminator()); !emitted.has_value()) {
            return std::unexpected(emitted.error());
        }
        emit_block_lines(out, current_block_lines_);
        out << '\n';
    }

    for (const std::string& extra_block : extra_blocks_) {
        out << extra_block << '\n';
    }

    out << "}\n";
    return out.str();
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

std::string narrow_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring quote_windows_arg(std::string_view arg) {
    const ArgumentQuotingState quoting = arg.empty() || arg.find_first_of(" \t\"") != std::string_view::npos
        ? ArgumentQuotingState::MustQuote
        : ArgumentQuotingState::CanPassUnquoted;
    if (quoting == ArgumentQuotingState::CanPassUnquoted) {
        return widen_utf8(arg);
    }

    std::wstring out;
    out.push_back(L'"');
    int backslashes = 0;
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

std::string display_command(const std::vector<std::string>& args) {
    std::ostringstream out;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            out << ' ';
        }
        out << narrow_utf8(quote_windows_arg(args[index]));
    }
    return out.str();
}

std::expected<std::pair<int, std::string>, std::string> run_process_capture(const std::vector<std::string>& args,
                                                                             const std::filesystem::path& log_path) {
    std::wstring command_line;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_arg(args[index]);
    }

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    const std::wstring log_path_wide = widen_utf8(log_path.string());
    HANDLE log_handle = CreateFileW(log_path_wide.c_str(),
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    &security_attributes,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (log_handle == INVALID_HANDLE_VALUE) {
        return std::unexpected("failed to create compiler log file '" + log_path.string() + "'");
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = log_handle;
    startup_info.hStdError = log_handle;

    PROCESS_INFORMATION process_info{};
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    const BOOL created = CreateProcessW(nullptr,
                                        mutable_command.data(),
                                        nullptr,
                                        nullptr,
                                        TRUE,
                                        0,
                                        nullptr,
                                        nullptr,
                                        &startup_info,
                                        &process_info);
    CloseHandle(log_handle);
    if (!created) {
        const DWORD error_code = GetLastError();
        return std::unexpected("failed to launch process '" + display_command(args) + "' (Windows error "
                               + std::to_string(error_code) + ")");
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    std::ifstream in(log_path, std::ios::binary);
    std::ostringstream captured;
    captured << in.rdbuf();
    in.close();
    return std::pair<int, std::string>{static_cast<int>(exit_code), captured.str()};
}

#else

std::string posix_error_message(int error_code) {
    return std::error_code(error_code, std::generic_category()).message();
}

std::string quote_posix_arg(std::string_view arg) {
    if (arg.empty()) {
        return "''";
    }

    const ArgumentQuotingState quoting = arg.find_first_of(" \t\n'\"\\$&;()<>|*?![]{}") != std::string_view::npos
        ? ArgumentQuotingState::MustQuote
        : ArgumentQuotingState::CanPassUnquoted;
    if (quoting == ArgumentQuotingState::CanPassUnquoted) {
        return std::string(arg);
    }

    std::string out;
    out.push_back('\'');
    for (char ch : arg) {
        if (ch == '\'') {
            out += "'\\''";
            continue;
        }
        out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string display_command(const std::vector<std::string>& args) {
    std::ostringstream out;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            out << ' ';
        }
        out << quote_posix_arg(args[index]);
    }
    return out.str();
}

std::expected<std::pair<int, std::string>, std::string> run_process_capture(const std::vector<std::string>& args,
                                                                             const std::filesystem::path& log_path) {
    if (args.empty()) {
        return std::unexpected("failed to launch empty process command");
    }

    posix_spawn_file_actions_t file_actions{};
    int action_result = posix_spawn_file_actions_init(&file_actions);
    if (action_result != 0) {
        return std::unexpected("failed to initialize process file actions for '" + display_command(args) + "' (error: "
                               + posix_error_message(action_result) + ")");
    }

    const auto destroy_file_actions = [&file_actions]() {
        posix_spawn_file_actions_destroy(&file_actions);
    };

    const std::string log_path_text = log_path.string();
    action_result = posix_spawn_file_actions_addopen(
        &file_actions,
        STDOUT_FILENO,
        log_path_text.c_str(),
        O_CREAT | O_WRONLY | O_TRUNC,
        0600);
    if (action_result != 0) {
        destroy_file_actions();
        return std::unexpected("failed to redirect process output for '" + display_command(args) + "' (error: "
                               + posix_error_message(action_result) + ")");
    }

    action_result = posix_spawn_file_actions_adddup2(&file_actions, STDOUT_FILENO, STDERR_FILENO);
    if (action_result != 0) {
        destroy_file_actions();
        return std::unexpected("failed to redirect process diagnostics for '" + display_command(args) + "' (error: "
                               + posix_error_message(action_result) + ")");
    }

    std::vector<std::vector<char>> argv_storage;
    std::vector<char*> argv;
    argv_storage.reserve(args.size());
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        argv_storage.emplace_back(arg.begin(), arg.end());
        argv_storage.back().push_back('\0');
        argv.push_back(argv_storage.back().data());
    }
    argv.push_back(nullptr);

    pid_t child_pid = 0;
    const int spawn_result = posix_spawnp(
        &child_pid,
        args.front().c_str(),
        &file_actions,
        nullptr,
        argv.data(),
        ::environ);
    destroy_file_actions();
    if (spawn_result != 0) {
        return std::unexpected("failed to launch process '" + display_command(args) + "' (error: "
                               + posix_error_message(spawn_result) + ")");
    }

    int status = 0;
    pid_t waited = 0;
    do {
        waited = waitpid(child_pid, &status, 0);
    } while (waited == -1 && errno == EINTR);
    if (waited == -1) {
        const int wait_error = errno;
        return std::unexpected("failed to wait for process '" + display_command(args) + "' (error: "
                               + posix_error_message(wait_error) + ")");
    }

    int exit_code = status;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    std::ifstream in(log_path, std::ios::binary);
    std::ostringstream captured;
    captured << in.rdbuf();
    in.close();
    std::string output = captured.str();
    if (exit_code == 127 && output.empty()) {
        return std::unexpected("failed to launch process '" + display_command(args)
                               + "' (child exited with status 127 before producing output)");
    }
    return std::pair<int, std::string>{exit_code, std::move(output)};
}

#endif

ToolchainCommandResult run_toolchain_command(const std::vector<std::string>& args,
                                             const std::filesystem::path& log_path,
                                             const std::string& temp_input_path) {
    const std::expected<std::pair<int, std::string>, std::string> result = run_process_capture(args, log_path);
    if (!result.has_value()) {
        std::ostringstream error;
        error << result.error() << '\n'
              << "required toolchain driver: " << args.front() << '\n'
              << "Install LLVM clang/lld or set " << kClangEnvVar << " to the clang executable.";
        return std::unexpected(ToolchainCommandFailure{
            error.str(),
            TemporaryInputCleanup::RemoveTemporaryInput,
        });
    }
    if (result->first != 0) {
        std::ostringstream error;
        error << "toolchain command failed: " << display_command(args) << '\n';
        if (!temp_input_path.empty()) {
            error << "temporary LLVM IR: " << temp_input_path << '\n';
        }
        if (!result->second.empty()) {
            error << result->second;
        }
        return std::unexpected(ToolchainCommandFailure{
            error.str(),
            TemporaryInputCleanup::PreserveTemporaryInput,
        });
    }
    return BackendStepSucceeded{};
}

std::expected<std::string, std::string> probe_driver_version(const std::string& driver,
                                                             std::string_view required_tool_label,
                                                             std::string_view install_hint) {
    const std::vector<std::string> command = {driver, "--version"};
    std::expected<std::filesystem::path, std::string> log_path = make_toolchain_probe_log_path();
    if (!log_path.has_value()) {
        return std::unexpected(log_path.error());
    }

    const std::expected<std::pair<int, std::string>, std::string> result = run_process_capture(command, *log_path);
    std::error_code remove_error;
    std::filesystem::remove(*log_path, remove_error);

    if (!result.has_value()) {
        std::ostringstream error;
        error << result.error() << '\n'
              << "required " << required_tool_label << ": " << driver << '\n'
              << install_hint;
        return std::unexpected(error.str());
    }
    if (result->first != 0) {
        std::ostringstream error;
        error << required_tool_label << " version probe failed: " << display_command(command) << '\n';
        if (!result->second.empty()) {
            error << result->second;
        }
        return std::unexpected(error.str());
    }

    std::string version_line = first_nonempty_line(result->second);
    if (version_line.empty()) {
        return std::unexpected(std::string(required_tool_label)
                               + " version probe produced no output: "
                               + display_command(command));
    }
    return version_line;
}

} // namespace

const char* emit_kind_name(EmitKind kind) {
    switch (kind) {
    case EmitKind::Llvm:
        return "LLVM IR";
    case EmitKind::Assembly:
        return "assembly";
    case EmitKind::Object:
        return "object";
    case EmitKind::Executable:
        return "executable";
    }
    return "artifact";
}

std::expected<std::string, std::string> selected_toolchain_driver() {
    return toolchain_driver();
}

std::expected<std::string, std::string> probe_toolchain_driver_version() {
    const std::expected<std::string, std::string> driver = toolchain_driver();
    if (!driver.has_value()) {
        return std::unexpected(driver.error());
    }
    return probe_driver_version(
        *driver,
        "toolchain driver",
        std::string("Install LLVM clang/lld or set ") + kClangEnvVar + " to the clang executable.");
}

std::expected<std::string, std::string> probe_linker_driver_version() {
    return probe_driver_version(
        kLinkerDriver,
        "linker driver",
        "Install LLVM lld-link and ensure it is available on PATH.");
}

std::string_view supported_target_triple() {
    return kSupportedTarget;
}

std::string_view toolchain_driver_environment_variable() {
    return kClangEnvVar;
}

std::expected<std::string, std::string> emit_llvm_ir(const hir::Package& hir_package,
                                                     const mir::Package& mir_package,
                                                     const std::string& target_triple,
                                                     EntryPointEmission entry_point_emission) {
    if (target_triple != kSupportedTarget) {
        return std::unexpected("backend currently supports only target '" + std::string(kSupportedTarget) + "'");
    }

    BackendModel validation_model(hir_package);
    if (const BackendStepResult validated = validate_backend_package(
            validation_model,
            hir_package,
            mir_package,
            entry_point_emission);
        !validated.has_value()) {
        return std::unexpected(validated.error());
    }

    BackendModel model(hir_package);
    ModuleEmitter emitter(model, hir_package, mir_package, target_triple, entry_point_emission);
    return emitter.emit();
}

std::expected<ArtifactEmissionSucceeded, std::string> emit_artifact(const hir::Package& hir_package,
                                                                    const mir::Package& mir_package,
                                                                    const EmitOptions& options) {
    EntryPointEmission entry_point_emission = EntryPointEmission::UserFunctionsOnly;
    if (options.kind() == EmitKind::Executable) {
        entry_point_emission = EntryPointEmission::IncludeExecutableEntryPoint;
    }
    const std::expected<std::string, std::string> llvm_ir = emit_llvm_ir(
        hir_package,
        mir_package,
        options.target_triple(),
        entry_point_emission);
    if (!llvm_ir.has_value()) {
        return std::unexpected(llvm_ir.error());
    }

    if (options.kind() == EmitKind::Llvm) {
        const std::expected<TextFileWriteSucceeded, std::string> written =
            write_text_file(options.output_path(), *llvm_ir);
        if (!written.has_value()) {
            return std::unexpected("failed to write LLVM IR output to '" + options.output_path() + "'");
        }
        return ArtifactEmissionSucceeded{};
    }

    const std::expected<std::string, std::string> driver = toolchain_driver();
    if (!driver.has_value()) {
        return std::unexpected(driver.error());
    }

    const std::filesystem::path output_path(options.output_path());
    const std::filesystem::path temp_ir_path = output_path.string() + ".tmp.ll";
    const std::filesystem::path log_path = output_path.string() + ".tool.log";

    const std::expected<TextFileWriteSucceeded, std::string> temp_ir_written =
        write_text_file(temp_ir_path.string(), *llvm_ir);
    if (!temp_ir_written.has_value()) {
        return std::unexpected("failed to write temporary LLVM IR to '" + temp_ir_path.string() + "'");
    }

    std::vector<std::string> command = {*driver, "-target", options.target_triple()};
    switch (options.kind()) {
    case EmitKind::Assembly:
        command.push_back("-S");
        break;
    case EmitKind::Object:
        command.push_back("-c");
        break;
    case EmitKind::Executable:
        command.push_back("-fuse-ld=lld");
        break;
    case EmitKind::Llvm:
        break;
    }
    command.push_back(temp_ir_path.string());
    if (options.kind() == EmitKind::Executable) {
        const std::string runtime_library = EVIDENT_NATIVE_RUNTIME_LIBRARY;
        if (!runtime_library.empty()) {
            command.push_back(runtime_library);
        }
    }
    command.push_back("-o");
    command.push_back(output_path.string());

    const ToolchainCommandResult command_result = run_toolchain_command(
        command,
        log_path,
        temp_ir_path.string());
    std::error_code remove_error;
    std::filesystem::remove(log_path, remove_error);
    if (!command_result.has_value()) {
        if (command_result.error().temporary_input_cleanup == TemporaryInputCleanup::RemoveTemporaryInput) {
            std::filesystem::remove(temp_ir_path, remove_error);
        }
        return std::unexpected(command_result.error().message);
    }

    std::filesystem::remove(temp_ir_path, remove_error);
    return ArtifactEmissionSucceeded{};
}

} // namespace evident::backend
