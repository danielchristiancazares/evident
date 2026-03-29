#pragma once

#include "evident/Ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace evident::hir {

enum class TypeKind {
    Struct,
    State,
    Reason,
    Proof,
    Permit,
    Trait,
};

struct TypeDecl {
    TypeKind kind{};
    ast::Visibility visibility = ast::Visibility::Private;
    std::string qualified_name;
    std::vector<std::string> generics;
    std::vector<std::string> members;
};

struct FunctionDecl {
    ast::Visibility visibility = ast::Visibility::Private;
    std::string qualified_name;
    std::vector<std::string> generics;
    std::vector<std::string> params;
    std::string return_type;
    std::optional<std::string> yields_type;
    bool is_foreign = false;
    bool has_body = false;
};

struct Package {
    std::vector<TypeDecl> types;
    std::vector<FunctionDecl> functions;
};

[[nodiscard]] Package lower(const ast::TranslationUnit& unit);
[[nodiscard]] std::string dump(const Package& package);
[[nodiscard]] std::string emit_stub_backend(const Package& package);

} // namespace evident::hir
