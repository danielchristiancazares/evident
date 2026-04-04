#include "evident/Backend.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
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
#endif

namespace evident::backend {

namespace {

constexpr std::string_view kSupportedTarget = "x86_64-pc-windows-msvc";

std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
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
        default:
            decoded.push_back(escaped);
            break;
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

enum class BuiltinKind;

struct FieldLayout {
    std::string name;
    std::string source_name;
    std::string llvm_type;
    std::size_t index = 0;
    std::size_t size = 0;
    std::size_t align = 1;
    bool is_scalar = false;
    bool is_never = false;
    std::optional<hir::TypeId> type_id;
    std::optional<BuiltinKind> builtin;
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
    bool has_payload = false;
    std::string payload_storage_type;
    std::size_t payload_size = 0;
    std::size_t payload_align = 1;
    std::size_t payload_field_index = 0;
};

enum class BuiltinKind {
    Int,
    Nat,
    Float,
    Char,
    Byte,
    CInt,
    CSize,
    Text,
    Bytes,
    Unit,
    Never,
};

struct ResolvedType {
    std::string source_name;
    std::string llvm_type;
    std::size_t size = 0;
    std::size_t align = 1;
    bool is_scalar = false;
    bool is_never = false;
    std::optional<hir::TypeId> type_id;
    std::optional<BuiltinKind> builtin;
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
    [[nodiscard]] std::expected<const YieldLayout*, std::string> ensure_yield_layout(hir::FunctionId id);
    [[nodiscard]] std::expected<const hir::FunctionDecl*, std::string> validate_entry_main() const;

    [[nodiscard]] const std::vector<std::string>& type_definitions() const;
    [[nodiscard]] const std::vector<std::string>& yield_definitions() const;

private:
    [[nodiscard]] std::expected<TypeLayout, std::string> build_type_layout(const hir::TypeDecl& type_decl);
    [[nodiscard]] std::expected<TypeLayout, std::string> build_record_layout(const hir::TypeDecl& type_decl);
    [[nodiscard]] std::expected<TypeLayout, std::string> build_tagged_union_layout(const hir::TypeDecl& type_decl);
    [[nodiscard]] std::expected<TypeLayout, std::string> build_permit_layout(const hir::TypeDecl& type_decl);

    const hir::Package& package_;
    std::unordered_map<std::string, hir::TypeId> type_ids_by_name_;
    std::unordered_map<hir::TypeId, TypeLayout> type_layouts_;
    std::vector<int> visit_state_;
    std::vector<std::string> type_definition_order_;
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

    [[nodiscard]] std::expected<void, std::string> prepare_locals();
    [[nodiscard]] std::expected<void, std::string> emit_statement(const mir::Statement& statement);
    [[nodiscard]] std::expected<void, std::string> emit_assign_use(mir::LocalId dest, const mir::Operand& operand);
    [[nodiscard]] std::expected<void, std::string> emit_assign_call(mir::LocalId dest, const mir::Rvalue& value);
    [[nodiscard]] std::expected<void, std::string> emit_assign_construct(mir::LocalId dest, const mir::Rvalue& value);
    [[nodiscard]] std::expected<void, std::string> emit_assign_project_field(mir::LocalId dest, const mir::Rvalue& value);
    [[nodiscard]] std::expected<void, std::string> emit_terminator(const mir::Terminator& terminator);
    [[nodiscard]] std::expected<void, std::string> emit_switch_variant(const mir::Terminator& terminator);
    [[nodiscard]] std::expected<void, std::string> emit_invoke(const mir::Terminator& terminator);
    [[nodiscard]] std::expected<void, std::string> emit_function_return(const mir::Operand& operand);
    [[nodiscard]] std::expected<void, std::string> emit_function_fail(const mir::Operand& operand);
    [[nodiscard]] std::expected<void, std::string> emit_success_or_failure_return(const mir::Operand& operand,
                                                                                  bool is_failure);
    [[nodiscard]] std::expected<TypedValue, std::string> materialize_operand(const mir::Operand& operand,
                                                                             const ResolvedType& expected_type);
    [[nodiscard]] std::expected<std::string, std::string> local_slot(mir::LocalId local_id) const;
    [[nodiscard]] std::expected<const ResolvedType*, std::string> local_type(mir::LocalId local_id) const;
    [[nodiscard]] std::expected<const TypeLayout*, std::string> user_layout_for_local(mir::LocalId local_id) const;
    [[nodiscard]] std::expected<void, std::string> store_typed_value(const std::string& slot_ptr,
                                                                     const ResolvedType& type,
                                                                     const std::string& value);
    [[nodiscard]] std::expected<std::string, std::string> load_from_slot(const std::string& slot_ptr,
                                                                         const ResolvedType& type);
    [[nodiscard]] std::expected<void, std::string> store_text_literal(const std::string& slot_ptr,
                                                                      const ResolvedType& type,
                                                                      const std::string& lexeme);
    [[nodiscard]] std::expected<TypedValue, std::string> make_text_literal_value(const ResolvedType& type,
                                                                                 const std::string& lexeme);
    [[nodiscard]] std::expected<std::string, std::string> insert_value(const std::string& aggregate_type,
                                                                       const std::string& aggregate_value,
                                                                       const std::string& element_type,
                                                                       const std::string& element_value,
                                                                       std::size_t field_index);
    [[nodiscard]] std::expected<std::string, std::string> extract_value(const std::string& aggregate_type,
                                                                        const std::string& aggregate_value,
                                                                        const std::string& element_type,
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
                  bool include_entry_wrapper)
        : model_(model),
          hir_package_(hir_package),
          mir_package_(mir_package),
          target_triple_(std::move(target_triple)),
          include_entry_wrapper_(include_entry_wrapper) {}

    [[nodiscard]] std::expected<std::string, std::string> emit();
    [[nodiscard]] std::expected<StringGlobal, std::string> intern_string(const std::string& lexeme);

private:
    [[nodiscard]] std::string emit_entry_wrapper(const hir::FunctionDecl& source_main) const;

    BackendModel& model_;
    const hir::Package& hir_package_;
    const mir::Package& mir_package_;
    std::string target_triple_;
    bool include_entry_wrapper_ = false;
    std::vector<StringGlobal> string_globals_;
    std::unordered_map<std::string, std::size_t> string_indices_;
};

[[nodiscard]] ResolvedType resolved_type_from_field(const FieldLayout& field);
[[nodiscard]] ResolvedType resolved_aggregate_type(std::string source_name,
                                                   std::string llvm_type,
                                                   std::size_t size,
                                                   std::size_t align);
[[nodiscard]] std::expected<void, std::string> validate_backend_package(BackendModel& model,
                                                                        const hir::Package& hir_package,
                                                                        const mir::Package& mir_package,
                                                                        bool include_entry_wrapper);

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
    if (name == "Int") {
        return ResolvedType{name, "i64", 8, 8, true, false, std::nullopt, BuiltinKind::Int};
    }
    if (name == "Nat") {
        return ResolvedType{name, "i64", 8, 8, true, false, std::nullopt, BuiltinKind::Nat};
    }
    if (name == "Float") {
        return ResolvedType{name, "double", 8, 8, true, false, std::nullopt, BuiltinKind::Float};
    }
    if (name == "Char") {
        return ResolvedType{name, "i32", 4, 4, true, false, std::nullopt, BuiltinKind::Char};
    }
    if (name == "Byte") {
        return ResolvedType{name, "i8", 1, 1, true, false, std::nullopt, BuiltinKind::Byte};
    }
    if (name == "CInt") {
        return ResolvedType{name, "i32", 4, 4, true, false, std::nullopt, BuiltinKind::CInt};
    }
    if (name == "CSize") {
        return ResolvedType{name, "i64", 8, 8, true, false, std::nullopt, BuiltinKind::CSize};
    }
    if (name == "Text") {
        return ResolvedType{name, "{ ptr, i64 }", 16, 8, false, false, std::nullopt, BuiltinKind::Text};
    }
    if (name == "Bytes") {
        return ResolvedType{name, "{ ptr, i64 }", 16, 8, false, false, std::nullopt, BuiltinKind::Bytes};
    }
    if (name == "Unit") {
        return ResolvedType{name, "i8", 1, 1, true, false, std::nullopt, BuiltinKind::Unit};
    }
    if (name == "Never") {
        return ResolvedType{name, "i8", 1, 1, true, true, std::nullopt, BuiltinKind::Never};
    }
    if (name == "CString" || name == "List" || name == "NonEmptyList" || name == "Map" || name == "NonEmptyMap") {
        return std::unexpected("backend does not yet support builtin type '" + name + "'");
    }

    const auto type_it = type_ids_by_name_.find(name);
    if (type_it == type_ids_by_name_.end()) {
        return std::unexpected("backend could not resolve type '" + name + "'");
    }

    const std::expected<const TypeLayout*, std::string> layout = ensure_type_layout(type_it->second);
    if (!layout.has_value()) {
        return std::unexpected(layout.error());
    }
    return ResolvedType{
        name,
        (*layout)->llvm_type,
        (*layout)->size,
        (*layout)->align,
        false,
        false,
        type_it->second,
        std::nullopt,
    };
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

std::expected<const YieldLayout*, std::string> BackendModel::ensure_yield_layout(hir::FunctionId id) {
    if (const auto it = yield_layouts_.find(id); it != yield_layouts_.end()) {
        return &it->second;
    }

    const hir::FunctionDecl& function_decl = function(id);
    if (!function_decl.fails_reason_type_id.has_value()) {
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
        hir::lookup_type(package_, *function_decl.fails_reason_type_id).qualified_name);
    if (!failure_type.has_value()) {
        return std::unexpected(failure_type.error());
    }

    const std::size_t payload_align = std::max(success_type->align, failure_type->align);
    const std::size_t payload_size = std::max(success_type->size, failure_type->size);
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
    if (main_function->is_foreign || main_function->body == nullptr) {
        return std::unexpected("entry `main` must have a function body");
    }
    if (!main_function->generics.empty()) {
        return std::unexpected("entry `main` may not be generic");
    }
    if (!main_function->params.empty()) {
        return std::unexpected("entry `main` may not take parameters");
    }
    if (main_function->fails_reason_type_id.has_value()) {
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
            return std::unexpected(field_type.error());
        }
        if (field_type->is_never) {
            return std::unexpected("backend does not support materialized field type `Never` in '"
                                   + type_decl.qualified_name + "'");
        }

        offset = align_up(offset, field_type->align);
        record_align = std::max(record_align, field_type->align);
        layout.field_indices.emplace(field.name, layout.fields.size());
        layout.fields.push_back(FieldLayout{
            field.name,
            field.type.text,
            field_type->llvm_type,
            layout.fields.size(),
            field_type->size,
            field_type->align,
            field_type->is_scalar,
            field_type->is_never,
            field_type->type_id,
            field_type->builtin,
        });
        field_types.push_back(field_type->llvm_type);
        offset += field_type->size;
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
            if (field_type->is_never) {
                return std::unexpected("backend does not support materialized field type `Never` in '"
                                       + variant_decl.qualified_name + "'");
            }

            offset = align_up(offset, field_type->align);
            payload_align = std::max(payload_align, field_type->align);
            variant_layout.field_indices.emplace(field.name, variant_layout.fields.size());
            variant_layout.fields.push_back(FieldLayout{
                field.name,
                field.type.text,
                field_type->llvm_type,
                variant_layout.fields.size(),
                field_type->size,
                field_type->align,
                field_type->is_scalar,
                field_type->is_never,
                field_type->type_id,
                field_type->builtin,
            });
            field_types.push_back(field_type->llvm_type);
            offset += field_type->size;
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
        layout.has_payload = true;
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
    return ResolvedType{
        field.source_name,
        field.llvm_type,
        field.size,
        field.align,
        field.is_scalar,
        field.is_never,
        field.type_id,
        field.builtin,
    };
}

ResolvedType resolved_aggregate_type(std::string source_name,
                                     std::string llvm_type,
                                     std::size_t size,
                                     std::size_t align) {
    return ResolvedType{
        std::move(source_name),
        std::move(llvm_type),
        size,
        align,
        false,
        false,
        std::nullopt,
        std::nullopt,
    };
}

std::expected<void, std::string> validate_backend_package(BackendModel& model,
                                                          const hir::Package& hir_package,
                                                          const mir::Package& mir_package,
                                                          bool include_entry_wrapper) {
    if (include_entry_wrapper) {
        const std::expected<const hir::FunctionDecl*, std::string> validated_main = model.validate_entry_main();
        if (!validated_main.has_value()) {
            return std::unexpected(validated_main.error());
        }
    }

    auto require_materializable_type = [](const ResolvedType& type, const std::string& context)
        -> std::expected<void, std::string> {
        if (type.is_never) {
            return std::unexpected("backend does not yet support materialized `Never` in " + context);
        }
        return {};
    };

    auto resolve_materializable_type = [&](const std::string& type_name,
                                           const std::string& context) -> std::expected<ResolvedType, std::string> {
        const std::expected<ResolvedType, std::string> resolved = model.resolve_type_name(type_name);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        if (const std::expected<void, std::string> valid = require_materializable_type(*resolved, context);
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
        if (const std::expected<ResolvedType, std::string> return_type = resolve_materializable_type(
                function_decl.return_type.text,
                signature_context);
            !return_type.has_value()) {
            return std::unexpected(return_type.error());
        }
        for (const hir::Parameter& param : function_decl.params) {
            if (param.is_compile_time_only) {
                continue;
            }
            const std::expected<ResolvedType, std::string> param_type = resolve_materializable_type(
                param.type.text,
                "parameter '" + param.name + "' of function '" + function_decl.qualified_name + "'");
            if (!param_type.has_value()) {
                return std::unexpected(param_type.error());
            }
        }
        if (function_decl.fails_reason_type_id.has_value()) {
            const std::expected<const YieldLayout*, std::string> yield_layout = model.ensure_yield_layout(function_decl.id);
            if (!yield_layout.has_value()) {
                return std::unexpected(yield_layout.error());
            }
            const hir::TypeDecl& reason_type = model.type(*function_decl.fails_reason_type_id);
            const std::expected<ResolvedType, std::string> resolved_reason = resolve_materializable_type(
                reason_type.qualified_name,
                "yield reason of function '" + function_decl.qualified_name + "'");
            if (!resolved_reason.has_value()) {
                return std::unexpected(resolved_reason.error());
            }
        }
    }

    if (mir_package.functions.size() != hir_package.functions.size()) {
        return std::unexpected("internal backend error: HIR/MIR function count mismatch");
    }

    for (const mir::Function& mir_function : mir_package.functions) {
        if (mir_function.function_id >= hir_package.functions.size()) {
            return std::unexpected("internal backend error: MIR function id out of range");
        }

        const hir::FunctionDecl& hir_function = hir::lookup_function(hir_package, mir_function.function_id);
        if (mir_function.is_foreign != hir_function.is_foreign) {
            return std::unexpected("internal backend error: foreign flag mismatch for function '"
                                   + hir_function.qualified_name + "'");
        }

        std::unordered_map<mir::LocalId, ResolvedType> local_types;
        local_types.reserve(mir_function.locals.size());
        std::unordered_map<mir::LocalId, bool> local_compile_time_only;
        local_compile_time_only.reserve(mir_function.locals.size());
        for (const mir::Local& local : mir_function.locals) {
            const std::expected<ResolvedType, std::string> resolved = local.is_compile_time_only
                ? model.resolve_type_name(local.type)
                : resolve_materializable_type(
                    local.type,
                    "local '" + local.name + "' in function '" + hir_function.qualified_name + "'");
            if (!resolved.has_value()) {
                return std::unexpected(resolved.error());
            }
            local_types.emplace(local.id, *resolved);
            local_compile_time_only.emplace(local.id, local.is_compile_time_only);
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
                                    const std::string& context) -> std::expected<void, std::string> {
            switch (operand.kind) {
            case mir::OperandKind::Local: {
                const auto compile_time_only_it = local_compile_time_only.find(operand.local_id);
                if (compile_time_only_it != local_compile_time_only.end() && compile_time_only_it->second) {
                    return std::unexpected("internal backend error: compile-time-only local %"
                                           + std::to_string(operand.local_id) + " used in " + context);
                }
                const std::expected<const ResolvedType*, std::string> actual_type = local_type(operand.local_id, context);
                if (!actual_type.has_value()) {
                    return std::unexpected(actual_type.error());
                }
                if ((*actual_type)->source_name != expected_type.source_name) {
                    return std::unexpected("backend type mismatch in " + context + ": expected '"
                                           + expected_type.source_name + "', got '" + (*actual_type)->source_name + "'");
                }
                return {};
            }
            case mir::OperandKind::IntLiteral:
                if (!expected_type.is_scalar
                    || (expected_type.llvm_type != "i64" && expected_type.llvm_type != "i32"
                        && expected_type.llvm_type != "i8")) {
                    return std::unexpected("backend only supports integer literals in integer destinations for "
                                           + context + ", got '" + expected_type.source_name + "'");
                }
                return {};
            case mir::OperandKind::StringLiteral:
                if (expected_type.builtin != BuiltinKind::Text && expected_type.builtin != BuiltinKind::Bytes) {
                    return std::unexpected("backend only supports string literals for Text/Bytes values in "
                                           + context + ", got '" + expected_type.source_name + "'");
                }
                return {};
            case mir::OperandKind::Unit:
                if (expected_type.builtin != BuiltinKind::Unit) {
                    return std::unexpected("backend expected Unit in " + context + ", got '"
                                           + expected_type.source_name + "'");
                }
                return {};
            }
            return std::unexpected("internal backend error: unsupported operand in " + context);
        };

        auto validate_block_target = [&](mir::BlockId block_id, const std::string& context)
            -> std::expected<void, std::string> {
            if (block_id >= mir_function.blocks.size()) {
                return std::unexpected("internal backend error: target block %" + std::to_string(block_id)
                                       + " out of range in " + context);
            }
            return {};
        };

        if (hir_function.is_foreign) {
            if (!mir_function.blocks.empty()) {
                return std::unexpected("internal backend error: foreign function '" + hir_function.qualified_name
                                       + "' unexpectedly has MIR blocks");
            }
            continue;
        }

        std::vector<bool> seen_blocks(mir_function.blocks.size(), false);
        for (const mir::BasicBlock& block : mir_function.blocks) {
            if (block.id >= mir_function.blocks.size()) {
                return std::unexpected("internal backend error: block id out of range in function '"
                                       + hir_function.qualified_name + "'");
            }
            if (seen_blocks[block.id]) {
                return std::unexpected("internal backend error: duplicate block id %" + std::to_string(block.id)
                                       + " in function '" + hir_function.qualified_name + "'");
            }
            seen_blocks[block.id] = true;

            for (const mir::Statement& statement : block.statements) {
                const std::string statement_context = "function '" + hir_function.qualified_name + "'";
                const std::expected<const ResolvedType*, std::string> dest_type
                    = local_type(statement.dest_local, statement_context);
                if (!dest_type.has_value()) {
                    return std::unexpected(dest_type.error());
                }
                const auto dest_compile_time_only = local_compile_time_only.find(statement.dest_local);
                if (dest_compile_time_only != local_compile_time_only.end() && dest_compile_time_only->second) {
                    return std::unexpected("internal backend error: compile-time-only local %"
                                           + std::to_string(statement.dest_local)
                                           + " cannot be assigned in function '" + hir_function.qualified_name + "'");
                }

                switch (statement.value.kind) {
                case mir::RvalueKind::Use: {
                    if (const std::expected<void, std::string> valid = validate_operand(
                            statement.value.operand,
                            *(*dest_type),
                            "assignment to local %" + std::to_string(statement.dest_local)
                                + " in function '" + hir_function.qualified_name + "'");
                        !valid.has_value()) {
                        return std::unexpected(valid.error());
                    }
                    break;
                }
                case mir::RvalueKind::Call: {
                    if (statement.value.function_id >= hir_package.functions.size()) {
                        return std::unexpected("internal backend error: call target id out of range in function '"
                                               + hir_function.qualified_name + "'");
                    }
                    const hir::FunctionDecl& callee = hir::lookup_function(hir_package, statement.value.function_id);
                    if (callee.fails_reason_type_id.has_value()) {
                        return std::unexpected("backend does not support lowering a yielding call as a plain assignment in '"
                                               + hir_function.qualified_name + "'");
                    }
                    if (statement.value.args.size() != callee.params.size()) {
                        return std::unexpected("internal backend error: call arity mismatch for '"
                                               + callee.qualified_name + "'");
                    }
                    const std::expected<ResolvedType, std::string> return_type = resolve_materializable_type(
                        callee.return_type.text,
                        "return type of callee '" + callee.qualified_name + "'");
                    if (!return_type.has_value()) {
                        return std::unexpected(return_type.error());
                    }
                    if (return_type->source_name != (*dest_type)->source_name) {
                        return std::unexpected("backend type mismatch for call result of '" + callee.qualified_name
                                               + "': expected '" + (*dest_type)->source_name + "', got '"
                                               + return_type->source_name + "'");
                    }
                    for (std::size_t index = 0; index < statement.value.args.size(); ++index) {
                        if (callee.params[index].is_compile_time_only) {
                            if (statement.value.args[index].kind != mir::OperandKind::Unit) {
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
                        const std::expected<void, std::string> valid = validate_operand(
                            statement.value.args[index],
                            *param_type,
                            "argument " + std::to_string(index) + " of call to '" + callee.qualified_name + "'");
                        if (!valid.has_value()) {
                            return std::unexpected(valid.error());
                        }
                    }
                    break;
                }
                case mir::RvalueKind::Construct: {
                    if (!(*dest_type)->type_id.has_value()) {
                        return std::unexpected("backend only supports constructing user-defined values, got '"
                                               + (*dest_type)->source_name + "'");
                    }
                    const std::expected<const TypeLayout*, std::string> owner_layout
                        = model.ensure_type_layout(statement.value.owner_type_id);
                    if (!owner_layout.has_value()) {
                        return std::unexpected(owner_layout.error());
                    }
                    if (statement.value.variant_id.has_value()) {
                        const auto variant_it = (*owner_layout)->variants.find(*statement.value.variant_id);
                        if (variant_it == (*owner_layout)->variants.end()) {
                            return std::unexpected("backend could not resolve variant layout for '"
                                                   + statement.value.qualified_name + "'");
                        }
                        const VariantLayout& variant_layout = variant_it->second;
                        for (const mir::FieldValue& field : statement.value.fields) {
                            const auto field_it = variant_layout.field_indices.find(field.name);
                            if (field_it == variant_layout.field_indices.end()) {
                                return std::unexpected("backend could not resolve variant field '" + field.name
                                                       + "' in '" + statement.value.qualified_name + "'");
                            }
                            const ResolvedType field_type = resolved_type_from_field(variant_layout.fields[field_it->second]);
                            const std::expected<void, std::string> valid = validate_operand(
                                field.value,
                                field_type,
                                "field '" + field.name + "' of '" + statement.value.qualified_name + "'");
                            if (!valid.has_value()) {
                                return std::unexpected(valid.error());
                            }
                        }
                    } else {
                        for (const mir::FieldValue& field : statement.value.fields) {
                            const auto field_it = (*owner_layout)->field_indices.find(field.name);
                            if (field_it == (*owner_layout)->field_indices.end()) {
                                return std::unexpected("backend could not resolve field '" + field.name
                                                       + "' in '" + statement.value.qualified_name + "'");
                            }
                            const ResolvedType field_type = resolved_type_from_field((*owner_layout)->fields[field_it->second]);
                            const std::expected<void, std::string> valid = validate_operand(
                                field.value,
                                field_type,
                                "field '" + field.name + "' of '" + statement.value.qualified_name + "'");
                            if (!valid.has_value()) {
                                return std::unexpected(valid.error());
                            }
                        }
                    }
                    break;
                }
                case mir::RvalueKind::ProjectField: {
                    const std::expected<const ResolvedType*, std::string> base_type
                        = local_type(statement.value.base_local, statement_context);
                    if (!base_type.has_value()) {
                        return std::unexpected(base_type.error());
                    }
                    if (!(*base_type)->type_id.has_value()) {
                        return std::unexpected("backend expected a user-defined base for projection in function '"
                                               + hir_function.qualified_name + "'");
                    }
                    if (statement.value.projection_variant_id.has_value()) {
                        const std::expected<const TypeLayout*, std::string> owner_layout
                            = model.ensure_type_layout(statement.value.projection_owner_type_id);
                        if (!owner_layout.has_value()) {
                            return std::unexpected(owner_layout.error());
                        }
                        const auto variant_it = (*owner_layout)->variants.find(*statement.value.projection_variant_id);
                        if (variant_it == (*owner_layout)->variants.end()) {
                            return std::unexpected("backend could not resolve projection variant for field '"
                                                   + statement.value.field_name + "'");
                        }
                        const auto field_it = variant_it->second.field_indices.find(statement.value.field_name);
                        if (field_it == variant_it->second.field_indices.end()) {
                            return std::unexpected("backend could not resolve projected field '"
                                                   + statement.value.field_name + "'");
                        }
                        const ResolvedType field_type
                            = resolved_type_from_field(variant_it->second.fields[field_it->second]);
                        if (field_type.source_name != (*dest_type)->source_name) {
                            return std::unexpected("backend type mismatch for projected field '" + statement.value.field_name
                                                   + "': expected '" + (*dest_type)->source_name + "', got '"
                                                   + field_type.source_name + "'");
                        }
                    } else {
                        const std::expected<const TypeLayout*, std::string> owner_layout
                            = model.ensure_type_layout(*(*base_type)->type_id);
                        if (!owner_layout.has_value()) {
                            return std::unexpected(owner_layout.error());
                        }
                        const auto field_it = (*owner_layout)->field_indices.find(statement.value.field_name);
                        if (field_it == (*owner_layout)->field_indices.end()) {
                            return std::unexpected("backend could not resolve projected field '"
                                                   + statement.value.field_name + "'");
                        }
                        const ResolvedType field_type = resolved_type_from_field((*owner_layout)->fields[field_it->second]);
                        if (field_type.source_name != (*dest_type)->source_name) {
                            return std::unexpected("backend type mismatch for projected field '" + statement.value.field_name
                                                   + "': expected '" + (*dest_type)->source_name + "', got '"
                                                   + field_type.source_name + "'");
                        }
                    }
                    break;
                }
                }
            }

            if (!block.has_terminator) {
                continue;
            }

            const mir::Terminator& terminator = block.terminator;
            switch (terminator.kind) {
            case mir::TerminatorKind::Return: {
                if (!terminator.value.has_value()) {
                    return std::unexpected("internal backend error: missing return value in function '"
                                           + hir_function.qualified_name + "'");
                }
                const std::expected<ResolvedType, std::string> return_type = resolve_materializable_type(
                    hir_function.return_type.text,
                    "return type of function '" + hir_function.qualified_name + "'");
                if (!return_type.has_value()) {
                    return std::unexpected(return_type.error());
                }
                const std::expected<void, std::string> valid = validate_operand(
                    *terminator.value,
                    *return_type,
                    "return from function '" + hir_function.qualified_name + "'");
                if (!valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                break;
            }
            case mir::TerminatorKind::Fail: {
                if (!hir_function.fails_reason_type_id.has_value()) {
                    return std::unexpected("internal backend error: `fail` terminator in non-yielding function '"
                                           + hir_function.qualified_name + "'");
                }
                if (!terminator.value.has_value()) {
                    return std::unexpected("internal backend error: missing fail value in function '"
                                           + hir_function.qualified_name + "'");
                }
                const hir::TypeDecl& reason_type = model.type(*hir_function.fails_reason_type_id);
                const std::expected<ResolvedType, std::string> resolved_reason = resolve_materializable_type(
                    reason_type.qualified_name,
                    "fail reason of function '" + hir_function.qualified_name + "'");
                if (!resolved_reason.has_value()) {
                    return std::unexpected(resolved_reason.error());
                }
                const std::expected<void, std::string> valid = validate_operand(
                    *terminator.value,
                    *resolved_reason,
                    "fail from function '" + hir_function.qualified_name + "'");
                if (!valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                break;
            }
            case mir::TerminatorKind::Goto: {
                const std::expected<void, std::string> valid = validate_block_target(
                    terminator.target_block,
                    "function '" + hir_function.qualified_name + "'");
                if (!valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                break;
            }
            case mir::TerminatorKind::SwitchVariant: {
                const std::expected<const ResolvedType*, std::string> scrutinee_type
                    = local_type(terminator.scrutinee_local, "switch in function '" + hir_function.qualified_name + "'");
                if (!scrutinee_type.has_value()) {
                    return std::unexpected(scrutinee_type.error());
                }
                if (!(*scrutinee_type)->type_id.has_value()) {
                    return std::unexpected("backend expected a user-defined scrutinee for variant switch in function '"
                                           + hir_function.qualified_name + "'");
                }
                const std::expected<const TypeLayout*, std::string> owner_layout
                    = model.ensure_type_layout(*(*scrutinee_type)->type_id);
                if (!owner_layout.has_value()) {
                    return std::unexpected(owner_layout.error());
                }
                for (const mir::SwitchEdge& edge : terminator.edges) {
                    if ((*owner_layout)->variants.find(edge.variant_id) == (*owner_layout)->variants.end()) {
                        return std::unexpected("backend could not resolve switched variant id "
                                               + std::to_string(edge.variant_id) + " in function '"
                                               + hir_function.qualified_name + "'");
                    }
                    const std::expected<void, std::string> valid = validate_block_target(
                        edge.target_block,
                        "switch in function '" + hir_function.qualified_name + "'");
                    if (!valid.has_value()) {
                        return std::unexpected(valid.error());
                    }
                }
                break;
            }
            case mir::TerminatorKind::Invoke: {
                if (terminator.function_id >= hir_package.functions.size()) {
                    return std::unexpected("internal backend error: invoke target id out of range in function '"
                                           + hir_function.qualified_name + "'");
                }
                const hir::FunctionDecl& callee = hir::lookup_function(hir_package, terminator.function_id);
                if (!callee.fails_reason_type_id.has_value()) {
                    return std::unexpected("internal backend error: invoke used on non-yielding function '"
                                           + callee.qualified_name + "'");
                }
                if (terminator.args.size() != callee.params.size()) {
                    return std::unexpected("internal backend error: invoke arity mismatch for '"
                                           + callee.qualified_name + "'");
                }
                for (std::size_t index = 0; index < terminator.args.size(); ++index) {
                    if (callee.params[index].is_compile_time_only) {
                        if (terminator.args[index].kind != mir::OperandKind::Unit) {
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
                    const std::expected<void, std::string> valid = validate_operand(
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
                const hir::TypeDecl& callee_reason = model.type(*callee.fails_reason_type_id);
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
                if ((*success_local_type)->source_name != success_type->source_name) {
                    return std::unexpected("backend type mismatch for invoke success local in function '"
                                           + hir_function.qualified_name + "': expected '" + success_type->source_name
                                           + "', got '" + (*success_local_type)->source_name + "'");
                }
                if ((*failure_local_type)->source_name != failure_type->source_name) {
                    return std::unexpected("backend type mismatch for invoke failure local in function '"
                                           + hir_function.qualified_name + "': expected '" + failure_type->source_name
                                           + "', got '" + (*failure_local_type)->source_name + "'");
                }
                if (const std::expected<void, std::string> valid = validate_block_target(
                        terminator.success_block,
                        "invoke success in function '" + hir_function.qualified_name + "'");
                    !valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                if (const std::expected<void, std::string> valid = validate_block_target(
                        terminator.failure_block,
                        "invoke failure in function '" + hir_function.qualified_name + "'");
                    !valid.has_value()) {
                    return std::unexpected(valid.error());
                }
                break;
            }
            case mir::TerminatorKind::Unreachable:
                break;
            }
        }
    }

    return {};
}

std::expected<std::string, std::string> ModuleEmitter::emit() {
    const hir::FunctionDecl* entry_main = nullptr;
    if (include_entry_wrapper_) {
        const std::expected<const hir::FunctionDecl*, std::string> validated_main = model_.validate_entry_main();
        if (!validated_main.has_value()) {
            return std::unexpected(validated_main.error());
        }
        entry_main = *validated_main;
    }

    std::vector<std::string> function_chunks;
    function_chunks.reserve(mir_package_.functions.size() + (entry_main != nullptr ? 1 : 0));
    for (const mir::Function& mir_function : mir_package_.functions) {
        const hir::FunctionDecl& hir_function = hir::lookup_function(hir_package_, mir_function.function_id);
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

    for (const std::string& function_chunk : function_chunks) {
        out << function_chunk;
        if (!function_chunk.empty() && function_chunk.back() != '\n') {
            out << '\n';
        }
        out << '\n';
    }

    return out.str();
}

std::expected<StringGlobal, std::string> ModuleEmitter::intern_string(const std::string& lexeme) {
    const std::expected<std::string, std::string> decoded = decode_string_literal(lexeme);
    if (!decoded.has_value()) {
        return std::unexpected(decoded.error());
    }
    if (const auto it = string_indices_.find(*decoded); it != string_indices_.end()) {
        return string_globals_.at(it->second);
    }

    const std::size_t index = string_globals_.size();
    string_indices_.emplace(*decoded, index);
    string_globals_.push_back(StringGlobal{"evid.str." + std::to_string(index), *decoded});
    return string_globals_.back();
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
      block_preludes_(mir_function.blocks.size()) {}

std::expected<void, std::string> FunctionEmitter::prepare_locals() {
    for (const mir::Local& local : mir_function_.locals) {
        const std::expected<ResolvedType, std::string> resolved = model_.resolve_type_name(local.type);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        local_types_.emplace(local.id, *resolved);
        if (!local.is_compile_time_only) {
            local_slots_.emplace(local.id, "%slot" + std::to_string(local.id));
        }
    }
    return {};
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
    for (const mir::Local& local : mir_function_.locals) {
        if (local.id == local_id && local.is_compile_time_only) {
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

std::expected<const TypeLayout*, std::string> FunctionEmitter::user_layout_for_local(mir::LocalId local_id) const {
    const std::expected<const ResolvedType*, std::string> resolved = local_type(local_id);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }
    if (!(*resolved)->type_id.has_value()) {
        return std::unexpected("backend expected a user-defined type for local %" + std::to_string(local_id)
                               + ", got '" + (*resolved)->source_name + "'");
    }
    auto& mutable_model = const_cast<BackendModel&>(model_);
    return mutable_model.ensure_type_layout(*(*resolved)->type_id);
}

std::expected<void, std::string> FunctionEmitter::store_typed_value(const std::string& slot_ptr,
                                                                    const ResolvedType& type,
                                                                    const std::string& value) {
    if (type.is_never) {
        return std::unexpected("backend attempted to materialize `Never`");
    }
    append_line("store " + type.llvm_type + " " + value + ", ptr " + slot_ptr);
    return {};
}

std::expected<std::string, std::string> FunctionEmitter::load_from_slot(const std::string& slot_ptr,
                                                                        const ResolvedType& type) {
    if (type.is_never) {
        return std::unexpected("backend attempted to load a `Never` value");
    }
    const std::string value = next_temp("load");
    append_line(value + " = load " + type.llvm_type + ", ptr " + slot_ptr);
    return value;
}

std::expected<void, std::string> FunctionEmitter::store_text_literal(const std::string& slot_ptr,
                                                                     const ResolvedType& type,
                                                                     const std::string& lexeme) {
    const std::expected<TypedValue, std::string> literal_value = make_text_literal_value(type, lexeme);
    if (!literal_value.has_value()) {
        return std::unexpected(literal_value.error());
    }
    return store_typed_value(slot_ptr, literal_value->type, literal_value->value);
}

std::expected<FunctionEmitter::TypedValue, std::string> FunctionEmitter::make_text_literal_value(
    const ResolvedType& type,
    const std::string& lexeme) {
    if (type.builtin != BuiltinKind::Text && type.builtin != BuiltinKind::Bytes) {
        return std::unexpected("backend expected Text/Bytes storage for string literal, got '" + type.source_name + "'");
    }

    const std::expected<StringGlobal, std::string> global = module_.intern_string(lexeme);
    if (!global.has_value()) {
        return std::unexpected(global.error());
    }

    const std::string ptr_value = next_temp("strptr");
    append_line(ptr_value + " = getelementptr inbounds ["
                + std::to_string(global->bytes.size() + 1) + " x i8], ptr @" + global->name
                + ", i64 0, i64 0");

    const std::expected<std::string, std::string> with_ptr = insert_value(
        type.llvm_type,
        "zeroinitializer",
        "ptr",
        ptr_value,
        0);
    if (!with_ptr.has_value()) {
        return std::unexpected(with_ptr.error());
    }
    const std::expected<std::string, std::string> with_len = insert_value(
        type.llvm_type,
        *with_ptr,
        "i64",
        std::to_string(global->bytes.size()),
        1);
    if (!with_len.has_value()) {
        return std::unexpected(with_len.error());
    }
    return TypedValue{type, *with_len};
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
                                                                       const std::string& element_type,
                                                                       std::size_t field_index) {
    (void)element_type;
    const std::string extracted = next_temp("ext");
    append_line(extracted + " = extractvalue " + aggregate_type + " " + aggregate_value + ", "
                + std::to_string(field_index));
    return extracted;
}

std::expected<std::string, std::string> FunctionEmitter::pack_value_for_storage(const TypedValue& value,
                                                                                const std::string& storage_type,
                                                                                std::size_t storage_size) {
    if (value.type.is_never) {
        return std::unexpected("backend attempted to materialize `Never`");
    }
    if (storage_size == 0 || value.type.size == 0) {
        return std::string("zeroinitializer");
    }
    if (value.type.llvm_type == storage_type) {
        return value.value;
    }

    const std::string storage_slot = next_temp("pack.slot");
    append_line(storage_slot + " = alloca " + storage_type);
    append_line("store " + storage_type + " zeroinitializer, ptr " + storage_slot);
    append_line("store " + value.type.llvm_type + " " + value.value + ", ptr " + storage_slot);

    const std::string packed = next_temp("pack");
    append_line(packed + " = load " + storage_type + ", ptr " + storage_slot);
    return packed;
}

std::expected<FunctionEmitter::TypedValue, std::string> FunctionEmitter::unpack_value_from_storage(
    const std::string& storage_value,
    const std::string& storage_type,
    const ResolvedType& expected_type) {
    if (expected_type.is_never) {
        return std::unexpected("backend attempted to materialize `Never`");
    }
    if (expected_type.size == 0) {
        return TypedValue{expected_type, zero_value(expected_type)};
    }
    if (expected_type.llvm_type == storage_type) {
        return TypedValue{expected_type, storage_value};
    }

    const std::string storage_slot = next_temp("unpack.slot");
    append_line(storage_slot + " = alloca " + storage_type);
    append_line("store " + storage_type + " " + storage_value + ", ptr " + storage_slot);
    const std::string unpacked = next_temp("unpack");
    append_line(unpacked + " = load " + expected_type.llvm_type + ", ptr " + storage_slot);
    return TypedValue{expected_type, unpacked};
}

std::string FunctionEmitter::zero_value(const ResolvedType& type) const {
    if (!type.is_scalar) {
        return "zeroinitializer";
    }
    if (type.builtin == BuiltinKind::Float) {
        return "0.000000e+00";
    }
    return "0";
}

std::expected<FunctionEmitter::TypedValue, std::string> FunctionEmitter::materialize_operand(
    const mir::Operand& operand,
    const ResolvedType& expected_type) {
    switch (operand.kind) {
    case mir::OperandKind::Local: {
        const std::expected<const ResolvedType*, std::string> source_type = local_type(operand.local_id);
        if (!source_type.has_value()) {
            return std::unexpected(source_type.error());
        }
        if ((*source_type)->source_name != expected_type.source_name) {
            return std::unexpected("backend type mismatch: expected '" + expected_type.source_name
                                   + "', got '" + (*source_type)->source_name + "'");
        }
        const std::expected<std::string, std::string> slot = local_slot(operand.local_id);
        if (!slot.has_value()) {
            return std::unexpected(slot.error());
        }
        const std::expected<std::string, std::string> loaded = load_from_slot(*slot, *(*source_type));
        if (!loaded.has_value()) {
            return std::unexpected(loaded.error());
        }
        return TypedValue{expected_type, *loaded};
    }
    case mir::OperandKind::IntLiteral:
        if (!expected_type.is_scalar
            || (expected_type.llvm_type != "i64" && expected_type.llvm_type != "i32" && expected_type.llvm_type != "i8")) {
            return std::unexpected("backend only supports integer literals in integer destinations, got '"
                                   + expected_type.source_name + "'");
        }
        return TypedValue{expected_type, operand.text};
    case mir::OperandKind::StringLiteral: {
        if (expected_type.builtin != BuiltinKind::Text && expected_type.builtin != BuiltinKind::Bytes) {
            return std::unexpected("backend only supports string literals for Text/Bytes values, got '"
                                   + expected_type.source_name + "'");
        }
        return make_text_literal_value(expected_type, operand.text);
    }
    case mir::OperandKind::Unit:
        if (expected_type.builtin != BuiltinKind::Unit) {
            return std::unexpected("backend expected Unit, got '" + expected_type.source_name + "'");
        }
        return TypedValue{expected_type, "0"};
    }
    return std::unexpected("internal backend error: unsupported operand");
}

std::expected<void, std::string> FunctionEmitter::emit_assign_use(mir::LocalId dest, const mir::Operand& operand) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }

    if (operand.kind == mir::OperandKind::StringLiteral) {
        return store_text_literal(*dest_slot, *(*dest_type), operand.text);
    }

    const std::expected<TypedValue, std::string> value = materialize_operand(operand, *(*dest_type));
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    return store_typed_value(*dest_slot, value->type, value->value);
}

std::expected<void, std::string> FunctionEmitter::emit_assign_call(mir::LocalId dest, const mir::Rvalue& value) {
    const hir::FunctionDecl& callee = model_.function(value.function_id);
    if (!callee.generics.empty()) {
        return std::unexpected("backend does not yet support generic function '" + callee.qualified_name + "'");
    }
    if (callee.fails_reason_type_id.has_value()) {
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
        if (callee.params[index].is_compile_time_only) {
            if (value.args[index].kind != mir::OperandKind::Unit) {
                return std::unexpected("internal backend error: compile-time-only parameter "
                                       + std::to_string(index) + " of callee '" + callee.qualified_name
                                       + "' was not erased");
            }
            continue;
        }
        const std::expected<ResolvedType, std::string> param_type = model_.resolve_type_name(callee.params[index].type.text);
        if (!param_type.has_value()) {
            return std::unexpected(param_type.error());
        }
        const std::expected<TypedValue, std::string> arg = materialize_operand(value.args[index], *param_type);
        if (!arg.has_value()) {
            return std::unexpected(arg.error());
        }
        args.push_back(arg->type.llvm_type + " " + arg->value);
    }

    const std::string call_value = next_temp("call");
    std::ostringstream call;
    call << call_value << " = call " << return_type->llvm_type << " @" << model_.function_symbol(callee.id) << '(';
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

std::expected<void, std::string> FunctionEmitter::emit_assign_construct(mir::LocalId dest, const mir::Rvalue& value) {
    const std::expected<const ResolvedType*, std::string> dest_type = local_type(dest);
    if (!dest_type.has_value()) {
        return std::unexpected(dest_type.error());
    }
    const std::expected<std::string, std::string> dest_slot = local_slot(dest);
    if (!dest_slot.has_value()) {
        return std::unexpected(dest_slot.error());
    }
    if (!(*dest_type)->type_id.has_value()) {
        return std::unexpected("backend only supports constructing user-defined records and variants, got '"
                               + (*dest_type)->source_name + "'");
    }

    auto& mutable_model = model_;
    if (*(*dest_type)->type_id != value.owner_type_id) {
        return std::unexpected("internal backend error: construct owner type mismatch for '" + value.qualified_name + "'");
    }
    const std::expected<const TypeLayout*, std::string> owner_layout = mutable_model.ensure_type_layout(value.owner_type_id);
    if (!owner_layout.has_value()) {
        return std::unexpected(owner_layout.error());
    }

    if (value.variant_id.has_value()) {
        const auto variant_it = (*owner_layout)->variants.find(*value.variant_id);
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

        if (!value.fields.empty() && (*owner_layout)->has_payload) {
            std::string payload_value = "zeroinitializer";
            for (const mir::FieldValue& field : value.fields) {
                const auto field_it = variant_layout.field_indices.find(field.name);
                if (field_it == variant_layout.field_indices.end()) {
                    return std::unexpected("backend could not resolve variant field '" + field.name
                                           + "' in '" + value.qualified_name + "'");
                }
                const FieldLayout& field_layout = variant_layout.fields[field_it->second];
                const ResolvedType field_type = resolved_type_from_field(field_layout);
                const std::expected<TypedValue, std::string> field_value = materialize_operand(field.value, field_type);
                if (!field_value.has_value()) {
                    return std::unexpected(field_value.error());
                }
                const std::expected<std::string, std::string> inserted_payload = insert_value(
                    variant_layout.payload_llvm_type,
                    payload_value,
                    field_type.llvm_type,
                    field_value->value,
                    field_layout.index);
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

    std::string record_value = "zeroinitializer";
    for (const mir::FieldValue& field : value.fields) {
        const auto field_it = (*owner_layout)->field_indices.find(field.name);
        if (field_it == (*owner_layout)->field_indices.end()) {
            return std::unexpected("backend could not resolve field '" + field.name + "' in '" + value.qualified_name + "'");
        }
        const FieldLayout& field_layout = (*owner_layout)->fields[field_it->second];
        const ResolvedType field_type = resolved_type_from_field(field_layout);
        const std::expected<TypedValue, std::string> field_value = materialize_operand(field.value, field_type);
        if (!field_value.has_value()) {
            return std::unexpected(field_value.error());
        }
        const std::expected<std::string, std::string> inserted = insert_value(
            (*owner_layout)->llvm_type,
            record_value,
            field_type.llvm_type,
            field_value->value,
            field_layout.index);
        if (!inserted.has_value()) {
            return std::unexpected(inserted.error());
        }
        record_value = *inserted;
    }

    return store_typed_value(*dest_slot, *(*dest_type), record_value);
}

std::expected<void, std::string> FunctionEmitter::emit_assign_project_field(mir::LocalId dest, const mir::Rvalue& value) {
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

    if (value.projection_variant_id.has_value()) {
        auto& mutable_model = model_;
        const std::expected<const TypeLayout*, std::string> owner_layout = mutable_model.ensure_type_layout(value.projection_owner_type_id);
        if (!owner_layout.has_value()) {
            return std::unexpected(owner_layout.error());
        }
        const std::expected<const ResolvedType*, std::string> base_type = local_type(value.base_local);
        if (!base_type.has_value()) {
            return std::unexpected(base_type.error());
        }
        const auto variant_it = (*owner_layout)->variants.find(*value.projection_variant_id);
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
            (*owner_layout)->payload_storage_type,
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
            field_type.llvm_type,
            field_layout.index);
        if (!field_value.has_value()) {
            return std::unexpected(field_value.error());
        }
        return store_typed_value(*dest_slot, *(*dest_type), *field_value);
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
        field_type.llvm_type,
        field_layout.index);
    if (!field_value.has_value()) {
        return std::unexpected(field_value.error());
    }
    return store_typed_value(*dest_slot, *(*dest_type), *field_value);
}

std::expected<void, std::string> FunctionEmitter::emit_statement(const mir::Statement& statement) {
    switch (statement.value.kind) {
    case mir::RvalueKind::Use:
        return emit_assign_use(statement.dest_local, statement.value.operand);
    case mir::RvalueKind::Call:
        return emit_assign_call(statement.dest_local, statement.value);
    case mir::RvalueKind::Construct:
        return emit_assign_construct(statement.dest_local, statement.value);
    case mir::RvalueKind::ProjectField:
        return emit_assign_project_field(statement.dest_local, statement.value);
    }
    return std::unexpected("internal backend error: unsupported MIR statement");
}

std::expected<void, std::string> FunctionEmitter::emit_success_or_failure_return(const mir::Operand& operand,
                                                                                 bool is_failure) {
    auto& mutable_model = model_;
    const std::expected<const YieldLayout*, std::string> yield_layout = mutable_model.ensure_yield_layout(hir_function_.id);
    if (!yield_layout.has_value()) {
        return std::unexpected(yield_layout.error());
    }

    const std::expected<ResolvedType, std::string> payload_type = model_.resolve_type_name(
        is_failure
            ? model_.type(*hir_function_.fails_reason_type_id).qualified_name
            : hir_function_.return_type.text);
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
        is_failure ? "1" : "0",
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
    return {};
}

std::expected<void, std::string> FunctionEmitter::emit_function_return(const mir::Operand& operand) {
    if (hir_function_.fails_reason_type_id.has_value()) {
        return emit_success_or_failure_return(operand, false);
    }

    const std::expected<ResolvedType, std::string> return_type = model_.resolve_type_name(hir_function_.return_type.text);
    if (!return_type.has_value()) {
        return std::unexpected(return_type.error());
    }
    const std::expected<TypedValue, std::string> value = materialize_operand(operand, *return_type);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    append_line("ret " + return_type->llvm_type + " " + value->value);
    return {};
}

std::expected<void, std::string> FunctionEmitter::emit_function_fail(const mir::Operand& operand) {
    if (!hir_function_.fails_reason_type_id.has_value()) {
        return std::unexpected("internal backend error: `fail` terminator in non-yielding function '"
                               + hir_function_.qualified_name + "'");
    }
    return emit_success_or_failure_return(operand, true);
}

std::expected<void, std::string> FunctionEmitter::emit_switch_variant(const mir::Terminator& terminator) {
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
        "i32",
        0);
    if (!tag_value.has_value()) {
        return std::unexpected(tag_value.error());
    }

    const std::string default_block = next_aux_block();
    append_line("switch i32 " + *tag_value + ", label %" + default_block + " [");
    for (const mir::SwitchEdge& edge : terminator.edges) {
        append_line("  i32 " + std::to_string(edge.variant_id) + ", label %" + block_name(edge.target_block));
    }
    append_line("]");

    std::ostringstream unreachable_block;
    unreachable_block << default_block << ":\n";
    unreachable_block << "  unreachable\n";
    extra_blocks_.push_back(unreachable_block.str());
    return {};
}

std::expected<void, std::string> FunctionEmitter::emit_invoke(const mir::Terminator& terminator) {
    const hir::FunctionDecl& callee = model_.function(terminator.function_id);
    auto& mutable_model = model_;
    const std::expected<const YieldLayout*, std::string> yield_layout = mutable_model.ensure_yield_layout(callee.id);
    if (!yield_layout.has_value()) {
        return std::unexpected(yield_layout.error());
    }

    std::vector<std::string> args;
    args.reserve(terminator.args.size());
    for (std::size_t index = 0; index < terminator.args.size(); ++index) {
        if (callee.params[index].is_compile_time_only) {
            if (terminator.args[index].kind != mir::OperandKind::Unit) {
                return std::unexpected("internal backend error: compile-time-only parameter "
                                       + std::to_string(index) + " of callee '" + callee.qualified_name
                                       + "' was not erased");
            }
            continue;
        }
        const std::expected<ResolvedType, std::string> param_type = model_.resolve_type_name(callee.params[index].type.text);
        if (!param_type.has_value()) {
            return std::unexpected(param_type.error());
        }
        const std::expected<TypedValue, std::string> arg = materialize_operand(terminator.args[index], *param_type);
        if (!arg.has_value()) {
            return std::unexpected(arg.error());
        }
        args.push_back(arg->type.llvm_type + " " + arg->value);
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
        "i8",
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
            (*yield_layout)->payload_storage_type,
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
    success_block << "  store " << (*success_type)->llvm_type << ' ' << success_value << ", ptr " << *success_slot << '\n';
    success_block << "  br label %" << block_name(terminator.success_block) << '\n';
    extra_blocks_.push_back(success_block.str());

    std::ostringstream failure_block;
    failure_block << failure_dispatch_block << ":\n";
    failure_block << "  store " << (*failure_type)->llvm_type << ' ' << failure_value << ", ptr " << *failure_slot << '\n';
    failure_block << "  br label %" << block_name(terminator.failure_block) << '\n';
    extra_blocks_.push_back(failure_block.str());

    std::ostringstream unreachable_block;
    unreachable_block << default_block << ":\n";
    unreachable_block << "  unreachable\n";
    extra_blocks_.push_back(unreachable_block.str());
    return {};
}

std::expected<void, std::string> FunctionEmitter::emit_terminator(const mir::Terminator& terminator) {
    switch (terminator.kind) {
    case mir::TerminatorKind::Return:
        return emit_function_return(*terminator.value);
    case mir::TerminatorKind::Fail:
        return emit_function_fail(*terminator.value);
    case mir::TerminatorKind::Goto:
        append_line("br label %" + block_name(terminator.target_block));
        return {};
    case mir::TerminatorKind::SwitchVariant:
        return emit_switch_variant(terminator);
    case mir::TerminatorKind::Invoke:
        return emit_invoke(terminator);
    case mir::TerminatorKind::Unreachable:
        append_line("unreachable");
        return {};
    }
    return std::unexpected("internal backend error: unknown terminator");
}

std::expected<std::string, std::string> FunctionEmitter::emit() {
    if (const std::expected<void, std::string> prepared = prepare_locals(); !prepared.has_value()) {
        return std::unexpected(prepared.error());
    }

    std::expected<ResolvedType, std::string> signature_return_type = model_.resolve_type_name(hir_function_.return_type.text);
    if (hir_function_.fails_reason_type_id.has_value()) {
        const std::expected<const YieldLayout*, std::string> yield_layout = model_.ensure_yield_layout(hir_function_.id);
        if (!yield_layout.has_value()) {
            return std::unexpected(yield_layout.error());
        }
        signature_return_type = ResolvedType{
            "",
            (*yield_layout)->llvm_type,
            0,
            1,
            false,
            false,
            std::nullopt,
            std::nullopt,
        };
    }
    if (!signature_return_type.has_value()) {
        return std::unexpected(signature_return_type.error());
    }

    std::ostringstream out;
    if (hir_function_.is_foreign) {
        out << "declare " << signature_return_type->llvm_type << " @" << model_.function_symbol(hir_function_.id) << '(';
        bool first = true;
        for (std::size_t index = 0; index < hir_function_.params.size(); ++index) {
            if (hir_function_.params[index].is_compile_time_only) {
                continue;
            }
            const std::expected<ResolvedType, std::string> param_type = model_.resolve_type_name(hir_function_.params[index].type.text);
            if (!param_type.has_value()) {
                return std::unexpected(param_type.error());
            }
            if (!first) {
                out << ", ";
            }
            first = false;
            out << param_type->llvm_type;
        }
        out << ")\n";
        return out.str();
    }

    const std::string linkage = hir_function_.visibility == ast::Visibility::Public ? "" : "internal ";
    out << "define " << linkage << signature_return_type->llvm_type << " @" << model_.function_symbol(hir_function_.id)
        << '(';
    bool first = true;
    std::size_t runtime_arg_index = 0;
    for (std::size_t index = 0; index < hir_function_.params.size(); ++index) {
        if (hir_function_.params[index].is_compile_time_only) {
            continue;
        }
        const std::expected<ResolvedType, std::string> param_type = model_.resolve_type_name(hir_function_.params[index].type.text);
        if (!param_type.has_value()) {
            return std::unexpected(param_type.error());
        }
        if (!first) {
            out << ", ";
        }
        first = false;
        out << param_type->llvm_type << " %arg" << runtime_arg_index++;
    }
    out << ") {\n";
    out << "entry:\n";

    for (const mir::Local& local : mir_function_.locals) {
        if (local.is_compile_time_only) {
            continue;
        }
        const std::expected<const ResolvedType*, std::string> local_type_result = local_type(local.id);
        if (!local_type_result.has_value()) {
            return std::unexpected(local_type_result.error());
        }
        emit_instruction(out, *local_slot(local.id) + " = alloca " + (*local_type_result)->llvm_type);
    }
    runtime_arg_index = 0;
    for (std::size_t index = 0; index < hir_function_.params.size(); ++index) {
        if (hir_function_.params[index].is_compile_time_only) {
            continue;
        }
        const mir::LocalId local_id = mir_function_.locals[index].id;
        const std::expected<const ResolvedType*, std::string> param_type = local_type(local_id);
        if (!param_type.has_value()) {
            return std::unexpected(param_type.error());
        }
        emit_instruction(out, "store " + (*param_type)->llvm_type + " %arg" + std::to_string(runtime_arg_index++)
                              + ", ptr " + *local_slot(local_id));
    }

    if (mir_function_.blocks.empty()) {
        emit_instruction(out, "unreachable");
        out << "}\n";
        return out.str();
    }

    emit_instruction(out, "br label %" + block_name(mir_function_.blocks.front().id));
    out << '\n';

    for (const mir::BasicBlock& block : mir_function_.blocks) {
        out << block_name(block.id) << ":\n";
        emit_block_lines(out, block_preludes_.at(block.id));
        current_block_lines_.clear();
        for (const mir::Statement& statement : block.statements) {
            if (const std::expected<void, std::string> emitted = emit_statement(statement); !emitted.has_value()) {
                return std::unexpected(emitted.error());
            }
        }
        if (block.has_terminator) {
            if (const std::expected<void, std::string> emitted = emit_terminator(block.terminator); !emitted.has_value()) {
                return std::unexpected(emitted.error());
            }
        } else {
            current_block_lines_.push_back("unreachable");
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
    const bool needs_quotes = arg.empty() || arg.find_first_of(" \t\"") != std::string_view::npos;
    if (!needs_quotes) {
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
        return std::unexpected("failed to launch process '" + display_command(args) + "'");
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

std::string display_command(const std::vector<std::string>& args) {
    std::ostringstream out;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            out << ' ';
        }
        out << args[index];
    }
    return out.str();
}

std::expected<std::pair<int, std::string>, std::string> run_process_capture(const std::vector<std::string>& args,
                                                                             const std::filesystem::path&) {
    std::ostringstream command;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            command << ' ';
        }
        command << args[index];
    }
    command << " 2>&1";

    FILE* pipe = popen(command.str().c_str(), "r");
    if (pipe == nullptr) {
        return std::unexpected("failed to launch process '" + display_command(args) + "'");
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (const std::size_t count = std::fread(buffer.data(), 1, buffer.size(), pipe)) {
        output.append(buffer.data(), count);
    }
    const int exit_code = pclose(pipe);
    return std::pair<int, std::string>{exit_code, output};
}

#endif

std::expected<void, std::string> run_toolchain_command(const std::vector<std::string>& args,
                                                       const std::filesystem::path& log_path,
                                                       const std::string& temp_input_path) {
    const std::expected<std::pair<int, std::string>, std::string> result = run_process_capture(args, log_path);
    if (!result.has_value()) {
        return std::unexpected(result.error());
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
        return std::unexpected(error.str());
    }
    return {};
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

std::expected<std::string, std::string> emit_llvm_ir(const hir::Package& hir_package,
                                                     const mir::Package& mir_package,
                                                     const std::string& target_triple,
                                                     bool include_entry_wrapper) {
    if (target_triple != kSupportedTarget) {
        return std::unexpected("backend currently supports only target '" + std::string(kSupportedTarget) + "'");
    }

    BackendModel validation_model(hir_package);
    if (const std::expected<void, std::string> validated = validate_backend_package(
            validation_model,
            hir_package,
            mir_package,
            include_entry_wrapper);
        !validated.has_value()) {
        return std::unexpected(validated.error());
    }

    BackendModel model(hir_package);
    ModuleEmitter emitter(model, hir_package, mir_package, target_triple, include_entry_wrapper);
    return emitter.emit();
}

std::expected<void, std::string> emit_artifact(const hir::Package& hir_package,
                                               const mir::Package& mir_package,
                                               const EmitOptions& options) {
    const bool include_entry_wrapper = options.kind == EmitKind::Executable;
    const std::expected<std::string, std::string> llvm_ir = emit_llvm_ir(
        hir_package,
        mir_package,
        options.target_triple,
        include_entry_wrapper);
    if (!llvm_ir.has_value()) {
        return std::unexpected(llvm_ir.error());
    }

    if (options.kind == EmitKind::Llvm) {
        if (!write_text_file(options.output_path, *llvm_ir)) {
            return std::unexpected("failed to write LLVM IR output to '" + options.output_path + "'");
        }
        return {};
    }

    const std::filesystem::path output_path(options.output_path);
    const std::filesystem::path temp_ir_path = output_path.string() + ".tmp.ll";
    const std::filesystem::path log_path = output_path.string() + ".tool.log";

    if (!write_text_file(temp_ir_path.string(), *llvm_ir)) {
        return std::unexpected("failed to write temporary LLVM IR to '" + temp_ir_path.string() + "'");
    }

    std::vector<std::string> command = {"clang", "-target", options.target_triple};
    switch (options.kind) {
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
    command.push_back("-o");
    command.push_back(output_path.string());

    const std::expected<void, std::string> command_result = run_toolchain_command(
        command,
        log_path,
        temp_ir_path.string());
    if (!command_result.has_value()) {
        return std::unexpected(command_result.error());
    }

    std::error_code remove_error;
    std::filesystem::remove(temp_ir_path, remove_error);
    std::filesystem::remove(log_path, remove_error);
    return {};
}

} // namespace evident::backend
