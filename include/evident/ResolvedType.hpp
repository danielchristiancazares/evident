#pragma once

#include "evident/Ast.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace evident::typesys {

enum class TypeFlavor {
    Error,
    Builtin,
    Generic,
    Named,
};

enum class UseDiscipline {
    Copyable,
    Affine,
    ScopedAuthority,
};

enum class TypeErrorState {
    CarriesTypeFacts,
    SuppressesFollowupDiagnostics,
};

enum class NeverTypeState {
    ProducesValue,
    DivergesBeforeFollowingCode,
};

enum class TypeEquivalence {
    Different,
    Equivalent,
};

enum class DisciplineMovement {
    Copyable,
    Affine,
};

enum class DisciplineMaterialization {
    RuntimeMaterialized,
    CompileTimeOnly,
};

struct Type {
    TypeFlavor flavor = TypeFlavor::Error;
    std::string name;
    const ast::Decl* decl = nullptr;
    std::vector<Type> args;
};

using ResolveTypeRefFn = std::function<Type(const ast::Decl* owner_decl,
                                            const std::vector<Type>& owner_args,
                                            const ast::TypeRef& type_ref)>;

[[nodiscard]] Type error_type();
[[nodiscard]] Type builtin_type(std::string name, std::vector<Type> args = {});
[[nodiscard]] Type generic_type(std::string name);
[[nodiscard]] Type named_type(std::string name, const ast::Decl* decl, std::vector<Type> args = {});

[[nodiscard]] TypeErrorState type_error_state(const Type& type);
[[nodiscard]] NeverTypeState never_type_state(const Type& type);
[[nodiscard]] TypeEquivalence type_equivalence(const Type& lhs, const Type& rhs);
[[nodiscard]] std::string type_name(const Type& type);

[[nodiscard]] DisciplineMovement discipline_movement(UseDiscipline discipline);
[[nodiscard]] DisciplineMaterialization discipline_materialization(UseDiscipline discipline);
[[nodiscard]] UseDiscipline merge_discipline(UseDiscipline lhs, UseDiscipline rhs);

class DisciplineClassifier {
public:
    explicit DisciplineClassifier(ResolveTypeRefFn resolve_type_ref);

    [[nodiscard]] UseDiscipline classify(const Type& type) const;

private:
    ResolveTypeRefFn resolve_type_ref_;
    mutable std::unordered_map<std::string, UseDiscipline> cache_;
    mutable std::unordered_set<std::string> active_;

    [[nodiscard]] UseDiscipline classify_impl(const Type& type) const;
    [[nodiscard]] std::string cache_key(const Type& type) const;
};

} // namespace evident::typesys
