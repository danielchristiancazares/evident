#include "evident/Hir.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace evident::hir {

TypeIdentity TypeIdentity::from_type_flavor() {
    return TypeIdentity(TypeIdentitySource::TypeFlavor, TypeId{});
}

TypeIdentity TypeIdentity::from_package_type(TypeId type_id) {
    return TypeIdentity(TypeIdentitySource::PackageTypeDeclaration, type_id);
}

TypeIdentity::TypeIdentity(TypeIdentitySource source, TypeId package_type_id)
    : source_(source), package_type_id_(package_type_id) {}

TypeDisciplineResolution TypeDisciplineResolution::requires_concrete_instantiation() {
    return TypeDisciplineResolution(TypeDisciplineResolutionState::RequiresConcreteInstantiation,
                                    typesys::UseDiscipline::Copyable);
}

TypeDisciplineResolution TypeDisciplineResolution::concrete_use_discipline(typesys::UseDiscipline discipline) {
    return TypeDisciplineResolution(TypeDisciplineResolutionState::ConcreteUseDisciplineKnown, discipline);
}

TypeDisciplineResolution::TypeDisciplineResolution(TypeDisciplineResolutionState state,
                                                   typesys::UseDiscipline discipline)
    : state_(state), discipline_(discipline) {}

FunctionFailureContract FunctionFailureContract::returns_declared_value() {
    return FunctionFailureContract(FunctionFailureBehavior::ReturnsDeclaredValue, TypeId{});
}

FunctionFailureContract FunctionFailureContract::yields_reason(TypeId reason_type_id) {
    return FunctionFailureContract(FunctionFailureBehavior::YieldsReason, reason_type_id);
}

FunctionFailureContract::FunctionFailureContract(FunctionFailureBehavior behavior, TypeId reason_type_id)
    : behavior_(behavior), reason_type_id_(reason_type_id) {}

FunctionAuthorityContract FunctionAuthorityContract::ordinary_call() {
    return FunctionAuthorityContract(FunctionAuthorityEffect::OrdinaryCall, TypeId{});
}

FunctionAuthorityContract FunctionAuthorityContract::grants_scoped_permit(TypeId permit_type_id) {
    return FunctionAuthorityContract(FunctionAuthorityEffect::GrantsScopedPermit, permit_type_id);
}

FunctionAuthorityContract::FunctionAuthorityContract(FunctionAuthorityEffect effect, TypeId permit_type_id)
    : effect_(effect), permit_type_id_(permit_type_id) {}

ExprFailureContract ExprFailureContract::returns_value_only() {
    return ExprFailureContract(ExprFailureEffect::ReturnsValueOnly, TypeId{});
}

ExprFailureContract ExprFailureContract::yields_reason(TypeId reason_type_id) {
    return ExprFailureContract(ExprFailureEffect::YieldsReason, reason_type_id);
}

ExprFailureContract::ExprFailureContract(ExprFailureEffect effect, TypeId reason_type_id)
    : effect_(effect), reason_type_id_(reason_type_id) {}

ConstructVariantTarget ConstructVariantTarget::constructs_named_type() {
    return ConstructVariantTarget(ConstructVariantTargetState::ConstructsNamedType, VariantId{});
}

ConstructVariantTarget ConstructVariantTarget::constructs_named_variant(VariantId variant_id) {
    return ConstructVariantTarget(ConstructVariantTargetState::ConstructsNamedVariant, variant_id);
}

ConstructVariantTarget::ConstructVariantTarget(ConstructVariantTargetState state, VariantId variant_id)
    : state_(state), variant_id_(variant_id) {}

namespace {

const std::unordered_set<std::string_view> kBuiltins = {
    "Int",   "Nat",   "Float",  "Char",  "Text",    "Bytes",
    "Never", "List",  "NonEmptyList",  "Map",   "NonEmptyMap",    "CString",
    "CInt",  "CSize", "Byte",   "Unit",
};

const std::unordered_set<std::string_view> kCompilerOwnedCollectionCompanionRecordNames = {
    "ListFirstAndRest",
    "MapEntry",
    "MapFirstEntryAndRest",
    "MapBoundValueAndRest",
};

struct Scope;

struct Symbol {
    const ast::Decl* decl = nullptr;
    ast::DeclKind kind = ast::DeclKind::Record;
    ast::Visibility visibility = ast::Visibility::Private;
    std::unique_ptr<Scope> child_scope;
};

struct Scope {
    Scope* parent = nullptr;
    std::unordered_map<std::string, Symbol> symbols;
};

struct ValueEnv {
    ValueEnv* parent = nullptr;
    std::unordered_map<std::string, TypeRef> values;
};

enum class VariantOwnerKind {
    StateDeclaration,
    ReasonDeclaration,
};

enum class VariantResolutionState {
    NoMatchingVariant,
    UniqueVariant,
    AmbiguousVariant,
};

enum class ArgumentLoweringMode {
    RuntimeValue,
    PreserveCompileTimePath,
};

enum class CompilerOwnedTypeNameState {
    UserDeclaredTypeName,
    CompilerOwnedTypeName,
};

enum class StringLiteralTypingState {
    RequiresTextDefault,
    AcceptsStringLiteral,
};

enum class TypeEquivalenceState {
    DifferentTypes,
    SameType,
};

struct VariantResolution {
    const ast::Decl* owner_decl = nullptr;
    VariantId variant_id = 0;
    VariantResolutionState state = VariantResolutionState::NoMatchingVariant;
};

struct FunctionContext {
    const Scope& scope;
    const FunctionDecl& function;
    std::vector<std::string> generics;
};

CompilerOwnedTypeNameState compiler_owned_type_name_state(std::string_view name) {
    return kBuiltins.contains(name) || kCompilerOwnedCollectionCompanionRecordNames.contains(name)
        ? CompilerOwnedTypeNameState::CompilerOwnedTypeName
        : CompilerOwnedTypeNameState::UserDeclaredTypeName;
}

ArgumentLoweringMode argument_lowering_mode(const TypeRef* concrete_arg_type,
                                            const TypeRef* declared_arg_type) {
    if (concrete_arg_type != nullptr
        && typesys::discipline_materialization(concrete_arg_type->discipline)
            == typesys::DisciplineMaterialization::CompileTimeOnly) {
        return ArgumentLoweringMode::PreserveCompileTimePath;
    }
    if (declared_arg_type != nullptr
        && typesys::discipline_materialization(declared_arg_type->discipline)
            == typesys::DisciplineMaterialization::CompileTimeOnly) {
        return ArgumentLoweringMode::PreserveCompileTimePath;
    }
    return ArgumentLoweringMode::RuntimeValue;
}

TypeKind type_kind_from_decl(ast::DeclKind kind) {
    switch (kind) {
    case ast::DeclKind::Record:
        return TypeKind::Record;
    case ast::DeclKind::State:
        return TypeKind::State;
    case ast::DeclKind::Reason:
        return TypeKind::Reason;
    case ast::DeclKind::Proof:
        return TypeKind::Proof;
    case ast::DeclKind::Permit:
        return TypeKind::Permit;
    case ast::DeclKind::Phase:
        return TypeKind::Phase;
    default:
        return TypeKind::Record;
    }
}

std::string format_path(const std::vector<std::string>& path) {
    std::ostringstream out;
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (index > 0) {
            out << "::";
        }
        out << path[index];
    }
    return out.str();
}

std::string type_base_name(const std::string& text) {
    const std::size_t generic_start = text.find('<');
    return generic_start == std::string::npos ? text : text.substr(0, generic_start);
}

std::string format_type_args(const std::string& base, const std::vector<TypeRef>& args) {
    if (args.empty()) {
        return base;
    }
    std::ostringstream out;
    out << base << '<';
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << args[index].text;
    }
    out << '>';
    return out.str();
}

TypeRef substitute_type_ref(const TypeRef& type, const std::unordered_map<std::string, TypeRef>& substitutions) {
    if (type.flavor == typesys::TypeFlavor::Generic) {
        if (const auto it = substitutions.find(type.text); it != substitutions.end()) {
            return it->second;
        }
    }

    TypeRef substituted = type;
    substituted.args.clear();
    substituted.args.reserve(type.args.size());
    typesys::UseDiscipline discipline = type.discipline;
    for (const TypeRef& arg : type.args) {
        TypeRef substituted_arg = substitute_type_ref(arg, substitutions);
        discipline = typesys::merge_discipline(discipline, substituted_arg.discipline);
        substituted.args.push_back(std::move(substituted_arg));
    }
    if (!substituted.args.empty()) {
        substituted.text = format_type_args(type_base_name(type.text), substituted.args);
    }
    substituted.discipline = discipline;
    return substituted;
}

void indent(std::ostream& out, std::size_t depth) {
    for (std::size_t index = 0; index < depth; ++index) {
        out << "  ";
    }
}

std::string dump_type_ref(const TypeRef& type) {
    std::string text = type.text;
    if (typesys::discipline_movement(type.discipline) == typesys::DisciplineMovement::Affine) {
        text += " [affine]";
    }
    return text;
}

typesys::TypeErrorState type_ref_error_state(const TypeRef& type) {
    return type.flavor == typesys::TypeFlavor::Error
        ? typesys::TypeErrorState::SuppressesFollowupDiagnostics
        : typesys::TypeErrorState::CarriesTypeFacts;
}

struct Lowerer {
    Package package;
    Scope root;
    std::unordered_map<const ast::Decl*, std::string> qualified_names;
    std::unordered_map<const ast::Decl*, const Scope*> decl_scopes;
    std::unordered_map<const ast::Decl*, TypeId> type_ids;
    std::unordered_map<TypeId, const ast::Decl*> decl_by_type_id;
    std::unordered_map<std::string, TypeId> qualified_type_ids;
    std::unordered_map<const ast::Decl*, FunctionId> function_ids;
    typesys::DisciplineClassifier discipline_classifier;

    using ExpectedFieldTypes = std::unordered_map<std::string, TypeRef>;

    Lowerer()
        : discipline_classifier([this](const ast::Decl* owner_decl,
                                       const std::vector<typesys::Type>& owner_args,
                                       const ast::TypeRef& type_ref) {
              return resolve_member_type(owner_decl, owner_args, type_ref);
          }) {}

    [[nodiscard]] Package lower(const ast::TranslationUnit& unit);

private:
    void declare_decls(const std::vector<std::unique_ptr<ast::Decl>>& decls,
                       Scope& scope,
                       const std::string& prefix);
    void populate_types(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope);
    void populate_functions(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope);
    void lower_function_bodies(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope);
    [[nodiscard]] VariantId add_variant(TypeId owner_type_id, const ast::Variant& variant, const Scope& scope);
    [[nodiscard]] const std::vector<ast::GenericParam>* generic_params_for(const ast::Decl* decl) const;
    [[nodiscard]] const Symbol* resolve_symbol(const Scope& scope,
                                               const std::vector<std::string>& generics,
                                               const std::vector<std::string>& path) const;
    [[nodiscard]] const FunctionDecl* resolve_function(const Scope& scope,
                                                       const std::vector<std::string>& path) const;
    [[nodiscard]] VariantResolution resolve_variant(const Scope& scope,
                                                    const std::vector<std::string>& path,
                                                    VariantOwnerKind owner_kind) const;
    [[nodiscard]] TypeRef builtin_type(std::string name) const;
    [[nodiscard]] TypeRef named_type(TypeId type_id) const;
    [[nodiscard]] TypeRef generic_type(std::string name) const;
    [[nodiscard]] TypeRef lower_resolved_type(const typesys::Type& type) const;
    [[nodiscard]] typesys::Type resolve_type(const Scope& scope,
                                            const std::vector<std::string>& generics,
                                            const std::vector<std::pair<std::string, typesys::Type>>& substitutions,
                                            const ast::TypeRef& type_ref) const;
    [[nodiscard]] typesys::Type resolve_member_type(const ast::Decl* owner_decl,
                                                    const std::vector<typesys::Type>& owner_args,
                                                    const ast::TypeRef& type_ref) const;
    [[nodiscard]] TypeRef lower_type_ref(const Scope& scope,
                                         const std::vector<std::string>& generics,
                                         const ast::TypeRef& type_ref) const;
    [[nodiscard]] std::vector<std::string> generic_names_for(
        const std::vector<ast::GenericParam>& generic_params) const;
    [[nodiscard]] std::vector<std::pair<std::string, typesys::Type>> build_call_substitutions(
        const ast::FunctionDecl& function_decl,
        const ast::CallExpr& call,
        const FunctionContext& context) const;
    [[nodiscard]] StringLiteralTypingState string_literal_typing_state(const TypeRef& type) const;
    [[nodiscard]] const ast::PathExpr* path_expr(const ast::Expr& expr) const;
    [[nodiscard]] const ast::CallExpr* call_expr(const ast::Expr& expr) const;
    [[nodiscard]] const TypeRef* lookup_binding(const ValueEnv& env, const std::string& name) const;
    void bind_value(ValueEnv& env, const std::string& name, TypeRef type) const;
    [[nodiscard]] TypeEquivalenceState type_equivalence_state(const TypeRef& lhs,
                                                              const TypeRef& rhs) const;
    [[nodiscard]] TypeRef unify_types(const TypeRef& lhs, const TypeRef& rhs) const;
    [[nodiscard]] ExpectedFieldTypes expected_field_types_from_hir_fields(const std::vector<FieldDecl>& fields) const;
    [[nodiscard]] ExpectedFieldTypes expected_field_types_from_ast_fields(const ast::Decl* owner_decl,
                                                                          const std::vector<ast::Field>& fields,
                                                                          const std::vector<typesys::Type>& owner_args = {}) const;
    [[nodiscard]] std::vector<FieldInit> lower_field_inits(const std::vector<ast::RecordFieldInit>& fields,
                                                           const FunctionContext& context,
                                                           ValueEnv& env,
                                                           const ExpectedFieldTypes* expected_types = nullptr);
    [[nodiscard]] std::unique_ptr<Expr> lower_path_expr(const ast::PathExpr& expr,
                                                        const FunctionContext& context,
                                                        ValueEnv& env);
    [[nodiscard]] std::unique_ptr<Expr> lower_call_expr(const ast::CallExpr& expr,
                                                        const FunctionContext& context,
                                                        ValueEnv& env);
    [[nodiscard]] std::unique_ptr<Expr> lower_construct_expr(const ast::ConstructExpr& expr,
                                                             const FunctionContext& context,
                                                             ValueEnv& env);
    [[nodiscard]] std::unique_ptr<VariantPattern> lower_variant_pattern(const ast::VariantPattern& pattern,
                                                                        VariantId variant_id);
    void bind_variant_fields(ValueEnv& env,
                             const VariantPattern& pattern,
                             const VariantDecl& variant) const;
    [[nodiscard]] std::unique_ptr<Expr> lower_match_expr(const ast::MatchExpr& expr,
                                                         const FunctionContext& context,
                                                         ValueEnv& env,
                                                         const TypeRef* expected_type = nullptr);
    [[nodiscard]] std::unique_ptr<BlockExpr> lower_block_expr(const ast::BlockExpr& expr,
                                                              const FunctionContext& context,
                                                              ValueEnv& env,
                                                              const TypeRef* expected_type = nullptr);
    [[nodiscard]] std::unique_ptr<Expr> lower_fail_expr(const ast::FailExpr& expr,
                                                        const FunctionContext& context,
                                                        ValueEnv& env);
    [[nodiscard]] std::unique_ptr<Expr> lower_grant_expr(const ast::GrantExpr& expr,
                                                         const FunctionContext& context,
                                                         ValueEnv& env,
                                                         const TypeRef* expected_type = nullptr);
    [[nodiscard]] std::unique_ptr<Expr> lower_field_access_expr(const ast::FieldAccessExpr& expr,
                                                                const FunctionContext& context,
                                                                ValueEnv& env);
    [[nodiscard]] std::unique_ptr<Expr> lower_prove_expr(const ast::ProveExpr& expr,
                                                         const FunctionContext& context,
                                                         ValueEnv& env);
    [[nodiscard]] std::unique_ptr<Expr> lower_expr(const ast::Expr& expr,
                                                   const FunctionContext& context,
                                                   ValueEnv& env,
                                                   const TypeRef* expected_type = nullptr);
};

Package Lowerer::lower(const ast::TranslationUnit& unit) {
    declare_decls(unit.decls, root, "");
    populate_types(unit.decls, root);
    populate_functions(unit.decls, root);
    lower_function_bodies(unit.decls, root);
    return std::move(package);
}

void Lowerer::declare_decls(const std::vector<std::unique_ptr<ast::Decl>>& decls,
                            Scope& scope,
                            const std::string& prefix) {
    for (const auto& decl_ptr : decls) {
        const ast::Decl& decl = *decl_ptr;
        const std::string qualified = prefix.empty() ? decl.name : prefix + "::" + decl.name;
        qualified_names[&decl] = qualified;
        decl_scopes[&decl] = &scope;

        auto [it, inserted] = scope.symbols.try_emplace(
            decl.name, Symbol{&decl, decl.kind, decl.visibility, nullptr});
        if (!inserted) {
            if (decl.kind == ast::DeclKind::Module && it->second.kind == ast::DeclKind::Module
                && it->second.child_scope != nullptr) {
                const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
                declare_decls(module_decl.members, *it->second.child_scope, qualified);
            }
            continue;
        }

        switch (decl.kind) {
        case ast::DeclKind::Module: {
            it->second.child_scope = std::make_unique<Scope>();
            it->second.child_scope->parent = &scope;
            const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
            declare_decls(module_decl.members, *it->second.child_scope, qualified);
            break;
        }
        case ast::DeclKind::Record:
        case ast::DeclKind::State:
        case ast::DeclKind::Reason:
        case ast::DeclKind::Proof:
        case ast::DeclKind::Permit: {
            TypeDecl type;
            type.id = package.types.size();
            type.kind = type_kind_from_decl(decl.kind);
            type.visibility = decl.visibility;
            type.qualified_name = qualified;
            package.types.push_back(std::move(type));
            type_ids[&decl] = package.types.back().id;
            decl_by_type_id[package.types.back().id] = &decl;
            qualified_type_ids[qualified] = package.types.back().id;
            break;
        }
        case ast::DeclKind::Phase: {
            const auto& phase_decl = static_cast<const ast::PhaseDecl&>(decl);
            for (const std::string& pos : phase_decl.positions) {
                const std::string phase_qualified_name = qualified + "::" + pos;
                TypeDecl type;
                type.id = package.types.size();
                type.kind = TypeKind::Phase;
                type.visibility = decl.visibility;
                type.qualified_name = phase_qualified_name;
                package.types.push_back(std::move(type));
                qualified_type_ids[phase_qualified_name] = package.types.back().id;
                decl_by_type_id[package.types.back().id] = &decl;
            }
            break;
        }
        case ast::DeclKind::Function:
        case ast::DeclKind::ForeignFunction: {
            FunctionDecl function;
            function.id = package.functions.size();
            function.visibility = decl.visibility;
            function.qualified_name = qualified;
            const auto& function_decl = static_cast<const ast::FunctionDecl&>(decl);
            function.implementation = function_decl.implementation();
            package.functions.push_back(std::move(function));
            function_ids[&decl] = package.functions.back().id;
            break;
        }
        }
    }
}

void Lowerer::populate_types(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope) {
    for (const auto& decl_ptr : decls) {
        const ast::Decl& decl = *decl_ptr;
        if (decl.kind == ast::DeclKind::Module) {
            const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
            const auto scope_it = scope.symbols.find(module_decl.name);
            if (scope_it != scope.symbols.end() && scope_it->second.child_scope != nullptr) {
                populate_types(module_decl.members, *scope_it->second.child_scope);
            }
            continue;
        }

        if (decl.kind == ast::DeclKind::Phase) {
            const auto& phase_decl = static_cast<const ast::PhaseDecl&>(decl);
            for (const std::string& pos : phase_decl.positions) {
                const std::string qualified_name = qualified_names.at(&decl) + "::" + pos;
                const TypeId tid = qualified_type_ids.at(qualified_name);
                TypeDecl& phase_type = package.types[tid];
                for (const ast::Field& field : phase_decl.fields) {
                    phase_type.fields.push_back(FieldDecl{
                        field.name,
                        lower_type_ref(scope, {}, field.type),
                    });
                }
                phase_type.discipline = TypeDisciplineResolution::concrete_use_discipline(
                    discipline_classifier.classify(typesys::named_type(phase_type.qualified_name, &decl)));
            }
            continue;
        }

        const auto type_it = type_ids.find(&decl);
        if (type_it == type_ids.end()) {
            continue;
        }

        TypeDecl& type = package.types[type_it->second];
        switch (decl.kind) {
        case ast::DeclKind::Record: {
            const auto& record_decl = static_cast<const ast::RecordDecl&>(decl);
            for (const ast::GenericParam& generic : record_decl.generic_params) {
                type.generics.push_back(generic.name);
            }
            for (const ast::Field& field : record_decl.fields) {
                type.fields.push_back(FieldDecl{
                    field.name,
                    lower_type_ref(scope, type.generics, field.type),
                });
            }
            break;
        }
        case ast::DeclKind::State: {
            const auto& state_decl = static_cast<const ast::StateDecl&>(decl);
            for (const ast::Variant& variant : state_decl.variants) {
                type.variants.push_back(add_variant(type.id, variant, scope));
            }
            break;
        }
        case ast::DeclKind::Reason: {
            const auto& reason_decl = static_cast<const ast::ReasonDecl&>(decl);
            for (const ast::Variant& variant : reason_decl.variants) {
                type.variants.push_back(add_variant(type.id, variant, scope));
            }
            break;
        }
        case ast::DeclKind::Proof: {
            const auto& proof_decl = static_cast<const ast::ProofDecl&>(decl);
            for (const ast::Field& field : proof_decl.fields) {
                type.fields.push_back(FieldDecl{
                    field.name,
                    lower_type_ref(scope, {}, field.type),
                });
            }
            break;
        }
        case ast::DeclKind::Permit:
        default:
            break;
        }

        if (type.generics.empty()) {
            type.discipline = TypeDisciplineResolution::concrete_use_discipline(
                discipline_classifier.classify(typesys::named_type(type.qualified_name, &decl)));
        }
    }
}

const std::vector<ast::GenericParam>* Lowerer::generic_params_for(const ast::Decl* decl) const {
    if (decl == nullptr) {
        return nullptr;
    }
    switch (decl->kind) {
    case ast::DeclKind::Record:
        return &static_cast<const ast::RecordDecl*>(decl)->generic_params;
    case ast::DeclKind::State:
        return nullptr;
    case ast::DeclKind::Function:
    case ast::DeclKind::ForeignFunction:
        return &static_cast<const ast::FunctionDecl*>(decl)->signature.generic_params;
    case ast::DeclKind::Reason:
    case ast::DeclKind::Proof:
    case ast::DeclKind::Permit:
    case ast::DeclKind::Phase:
    case ast::DeclKind::Module:
        return nullptr;
    }
    return nullptr;
}

void Lowerer::populate_functions(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope) {
    for (const auto& decl_ptr : decls) {
        const ast::Decl& decl = *decl_ptr;
        if (decl.kind == ast::DeclKind::Module) {
            const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
            const auto scope_it = scope.symbols.find(module_decl.name);
            if (scope_it != scope.symbols.end() && scope_it->second.child_scope != nullptr) {
                populate_functions(module_decl.members, *scope_it->second.child_scope);
            }
            continue;
        }

        const auto function_it = function_ids.find(&decl);
        if (function_it == function_ids.end()) {
            continue;
        }

        const auto& function_decl = static_cast<const ast::FunctionDecl&>(decl);
        FunctionDecl& function = package.functions[function_it->second];
        for (const ast::GenericParam& generic : function_decl.signature.generic_params) {
            function.generics.push_back(generic.name);
        }
        for (const ast::Parameter& param : function_decl.signature.params) {
            const TypeRef lowered_type = lower_type_ref(scope, function.generics, param.type);
            function.params.push_back(Parameter{
                param.name,
                lowered_type,
                param.authority,
            });
        }
        function.return_type = lower_type_ref(scope, function.generics, function_decl.signature.return_type);
        if (function_decl.signature.failure.behavior() == ast::FunctionFailureBehavior::YieldsReason) {
            TypeRef fails_type = lower_type_ref(scope, function.generics, function_decl.signature.failure.reason_type());
            if (fails_type.identity.source() == TypeIdentitySource::PackageTypeDeclaration) {
                function.failure = FunctionFailureContract::yields_reason(fails_type.identity.package_type_id());
            }
        }
        if (function_decl.signature.authority.effect() == ast::FunctionAuthorityEffect::GrantsScopedPermit) {
            TypeRef grants_type = lower_type_ref(scope, function.generics, function_decl.signature.authority.permit_type());
            if (grants_type.identity.source() == TypeIdentitySource::PackageTypeDeclaration) {
                function.authority = FunctionAuthorityContract::grants_scoped_permit(grants_type.identity.package_type_id());
            }
        }
        for (const ast::TypeRef& proves_ast : function_decl.signature.proves_types) {
            TypeRef proves_type = lower_type_ref(scope, function.generics, proves_ast);
            if (proves_type.identity.source() == TypeIdentitySource::PackageTypeDeclaration) {
                function.proves_proof_type_ids.push_back(proves_type.identity.package_type_id());
            }
        }
    }
}

void Lowerer::lower_function_bodies(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope) {
    for (const auto& decl_ptr : decls) {
        const ast::Decl& decl = *decl_ptr;
        if (decl.kind == ast::DeclKind::Module) {
            const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
            const auto scope_it = scope.symbols.find(module_decl.name);
            if (scope_it != scope.symbols.end() && scope_it->second.child_scope != nullptr) {
                lower_function_bodies(module_decl.members, *scope_it->second.child_scope);
            }
            continue;
        }

        const auto function_it = function_ids.find(&decl);
        if (function_it == function_ids.end()) {
            continue;
        }

        const auto& function_decl = static_cast<const ast::FunctionDecl&>(decl);
        if (!function_decl.body) {
            continue;
        }

        FunctionDecl& function = package.functions[function_it->second];
        FunctionContext context{scope, function, function.generics};
        ValueEnv env{nullptr, {}};
        for (const Parameter& param : function.params) {
            env.values.emplace(param.name, param.type);
        }
        function.body = lower_block_expr(*function_decl.body, context, env, &function.return_type);
    }
}

VariantId Lowerer::add_variant(TypeId owner_type_id, const ast::Variant& variant, const Scope& scope) {
    const TypeDecl& owner = package.types[owner_type_id];
    VariantDecl lowered;
    lowered.id = package.variants.size();
    lowered.owner_type = owner_type_id;
    lowered.name = variant.name;
    lowered.qualified_name = owner.qualified_name + "::" + variant.name;
    for (const ast::Field& field : variant.fields) {
        lowered.fields.push_back(FieldDecl{
            field.name,
            lower_type_ref(scope, owner.generics, field.type),
        });
    }
    package.variants.push_back(std::move(lowered));
    return package.variants.back().id;
}

const Symbol* Lowerer::resolve_symbol(const Scope& scope,
                                      const std::vector<std::string>& generics,
                                      const std::vector<std::string>& path) const {
    if (path.empty()) {
        return nullptr;
    }

    if (path.size() == 1) {
        const std::string& name = path.front();
        if (std::find(generics.begin(), generics.end(), name) != generics.end()
            || compiler_owned_type_name_state(name) == CompilerOwnedTypeNameState::CompilerOwnedTypeName) {
            return nullptr;
        }
        for (const Scope* current = &scope; current != nullptr; current = current->parent) {
            const auto it = current->symbols.find(name);
            if (it != current->symbols.end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    const Symbol* symbol = nullptr;
    for (const Scope* current = &scope; current != nullptr && symbol == nullptr; current = current->parent) {
        const auto it = current->symbols.find(path.front());
        if (it != current->symbols.end()) {
            symbol = &it->second;
        }
    }
    if (symbol == nullptr) {
        return nullptr;
    }

    for (std::size_t index = 1; index < path.size(); ++index) {
        if (symbol->kind != ast::DeclKind::Module || symbol->child_scope == nullptr) {
            return nullptr;
        }
        const auto it = symbol->child_scope->symbols.find(path[index]);
        if (it == symbol->child_scope->symbols.end()) {
            return nullptr;
        }
        symbol = &it->second;
    }
    return symbol;
}

const FunctionDecl* Lowerer::resolve_function(const Scope& scope, const std::vector<std::string>& path) const {
    const Symbol* symbol = resolve_symbol(scope, {}, path);
    if (symbol == nullptr) {
        return nullptr;
    }
    if (symbol->kind != ast::DeclKind::Function && symbol->kind != ast::DeclKind::ForeignFunction) {
        return nullptr;
    }
    const auto function_it = function_ids.find(symbol->decl);
    return function_it != function_ids.end() ? &package.functions[function_it->second] : nullptr;
}

VariantResolution Lowerer::resolve_variant(const Scope& scope,
                                           const std::vector<std::string>& path,
                                           VariantOwnerKind owner_kind) const {
    VariantResolution result;
    if (path.empty()) {
        return result;
    }

    auto owner_kind_matches = [owner_kind](TypeKind candidate) {
        switch (owner_kind) {
        case VariantOwnerKind::StateDeclaration:
            return candidate == TypeKind::State;
        case VariantOwnerKind::ReasonDeclaration:
            return candidate == TypeKind::Reason;
        }
        return false;
    };

    auto try_owner = [&](const ast::Decl* owner_decl, std::string_view variant_name) {
        if (owner_decl == nullptr) {
            return;
        }
        const auto type_it = type_ids.find(owner_decl);
        if (type_it == type_ids.end()) {
            return;
        }
        const TypeDecl& owner = package.types[type_it->second];
        if (!owner_kind_matches(owner.kind)) {
            return;
        }
        for (VariantId variant_id : owner.variants) {
            const VariantDecl& variant = package.variants[variant_id];
            if (variant.name == variant_name) {
                if (result.owner_decl != nullptr && result.owner_decl != owner_decl) {
                    result.state = VariantResolutionState::AmbiguousVariant;
                    return;
                }
                result.owner_decl = owner_decl;
                result.variant_id = variant_id;
                result.state = VariantResolutionState::UniqueVariant;
                return;
            }
        }
    };

    if (path.size() > 1) {
        std::vector<std::string> owner_path(path.begin(), path.end() - 1);
        if (const Symbol* symbol = resolve_symbol(scope, {}, owner_path); symbol != nullptr) {
            try_owner(symbol->decl, path.back());
        }
        return result;
    }

    for (const Scope* current = &scope; current != nullptr; current = current->parent) {
        for (const auto& [_, symbol] : current->symbols) {
            try_owner(symbol.decl, path.front());
            if (result.state == VariantResolutionState::AmbiguousVariant) {
                return result;
            }
        }
    }
    return result;
}

TypeRef Lowerer::builtin_type(std::string name) const {
    return TypeRef{
        std::move(name),
        TypeIdentity::from_type_flavor(),
        typesys::TypeFlavor::Builtin,
        typesys::UseDiscipline::Copyable,
        {},
    };
}

TypeRef Lowerer::named_type(TypeId type_id) const {
    if (type_id >= package.types.size()) {
        return {};
    }
    const TypeDecl& type = package.types[type_id];
    TypeRef lowered;
    lowered.text = type.qualified_name;
    lowered.identity = TypeIdentity::from_package_type(type_id);
    lowered.flavor = typesys::TypeFlavor::Named;
    if (type.discipline.state() == TypeDisciplineResolutionState::ConcreteUseDisciplineKnown) {
        lowered.discipline = type.discipline.discipline();
    }
    return lowered;
}

TypeRef Lowerer::generic_type(std::string name) const {
    return TypeRef{
        std::move(name),
        TypeIdentity::from_type_flavor(),
        typesys::TypeFlavor::Generic,
        typesys::UseDiscipline::Copyable,
        {},
    };
}

TypeRef Lowerer::lower_resolved_type(const typesys::Type& type) const {
    TypeRef lowered;
    lowered.text = typesys::type_name(type);
    lowered.flavor = type.flavor;
    lowered.discipline = discipline_classifier.classify(type);
    lowered.args.reserve(type.args.size());
    for (const typesys::Type& arg : type.args) {
        lowered.args.push_back(lower_resolved_type(arg));
    }
    if (type.flavor == typesys::TypeFlavor::Named && type.decl != nullptr) {
        if (type.decl->kind == ast::DeclKind::Phase) {
            if (const auto phase_it = qualified_type_ids.find(type.name); phase_it != qualified_type_ids.end()) {
                lowered.identity = TypeIdentity::from_package_type(phase_it->second);
            }
        } else if (const auto type_it = type_ids.find(type.decl); type_it != type_ids.end()) {
            lowered.identity = TypeIdentity::from_package_type(type_it->second);
        }
    }
    return lowered;
}

typesys::Type Lowerer::resolve_type(const Scope& scope,
                                    const std::vector<std::string>& generics,
                                    const std::vector<std::pair<std::string, typesys::Type>>& substitutions,
                                    const ast::TypeRef& type_ref) const {
    if (type_ref.path.empty()) {
        return typesys::error_type();
    }

    std::vector<typesys::Type> args;
    args.reserve(type_ref.args.size());
    for (const ast::TypeRef& arg : type_ref.args) {
        args.push_back(resolve_type(scope, generics, substitutions, arg));
    }

    if (type_ref.path.size() == 1) {
        const std::string& name = type_ref.path.front();
        for (const auto& [generic_name, actual] : substitutions) {
            if (generic_name == name) {
                return actual;
            }
        }
        if (std::find(generics.begin(), generics.end(), name) != generics.end()) {
            return typesys::generic_type(name);
        }
        if (compiler_owned_type_name_state(name) == CompilerOwnedTypeNameState::CompilerOwnedTypeName) {
            return typesys::builtin_type(name, std::move(args));
        }
    }
    if (type_ref.path.size() >= 2) {
        std::vector<std::string> family_path(type_ref.path.begin(), type_ref.path.end() - 1);
        const std::string& position_name = type_ref.path.back();
        if (const Symbol* symbol = resolve_symbol(scope, generics, family_path); symbol != nullptr
            && symbol->decl->kind == ast::DeclKind::Phase) {
            const auto& phase_decl = static_cast<const ast::PhaseDecl&>(*symbol->decl);
            if (std::find(phase_decl.positions.begin(), phase_decl.positions.end(), position_name)
                != phase_decl.positions.end()) {
                const auto qualified_it = qualified_names.find(symbol->decl);
                if (qualified_it != qualified_names.end()) {
                    return typesys::named_type(qualified_it->second + "::" + position_name,
                                               symbol->decl,
                                               std::move(args));
                }
            }
        }
    }

    if (const Symbol* symbol = resolve_symbol(scope, generics, type_ref.path); symbol != nullptr) {
        if (symbol->decl->kind == ast::DeclKind::Phase) {
            return typesys::error_type();
        }
        if (const auto qualified_it = qualified_names.find(symbol->decl); qualified_it != qualified_names.end()) {
            return typesys::named_type(qualified_it->second, symbol->decl, std::move(args));
        }
    }
    return typesys::error_type();
}

typesys::Type Lowerer::resolve_member_type(const ast::Decl* owner_decl,
                                           const std::vector<typesys::Type>& owner_args,
                                           const ast::TypeRef& type_ref) const {
    std::vector<std::pair<std::string, typesys::Type>> substitutions;
    std::vector<std::string> generics;
    if (const auto* generic_params = generic_params_for(owner_decl); generic_params != nullptr) {
        generics.reserve(generic_params->size());
        substitutions.reserve(std::min(generic_params->size(), owner_args.size()));
        for (std::size_t index = 0; index < generic_params->size(); ++index) {
            generics.push_back((*generic_params)[index].name);
            if (index < owner_args.size()) {
                substitutions.emplace_back((*generic_params)[index].name, owner_args[index]);
            }
        }
    }
    return resolve_type(*decl_scopes.at(owner_decl), generics, substitutions, type_ref);
}

TypeRef Lowerer::lower_type_ref(const Scope& scope,
                                const std::vector<std::string>& generics,
                                const ast::TypeRef& type_ref) const {
    return lower_resolved_type(resolve_type(scope, generics, {}, type_ref));
}

std::vector<std::string> Lowerer::generic_names_for(const std::vector<ast::GenericParam>& generic_params) const {
    std::vector<std::string> generics;
    generics.reserve(generic_params.size());
    for (const ast::GenericParam& generic : generic_params) {
        generics.push_back(generic.name);
    }
    return generics;
}

std::vector<std::pair<std::string, typesys::Type>> Lowerer::build_call_substitutions(
    const ast::FunctionDecl& function_decl,
    const ast::CallExpr& call,
    const FunctionContext& context) const {
    std::vector<std::pair<std::string, typesys::Type>> substitutions;
    const std::size_t count = std::min(function_decl.signature.generic_params.size(), call.type_args.size());
    substitutions.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        substitutions.emplace_back(function_decl.signature.generic_params[index].name,
                                   resolve_type(context.scope, context.generics, {}, call.type_args[index]));
    }
    return substitutions;
}

StringLiteralTypingState Lowerer::string_literal_typing_state(const TypeRef& type) const {
    if (type.flavor == typesys::TypeFlavor::Builtin
        && type.identity.source() == TypeIdentitySource::TypeFlavor
        && type.args.empty()
        && (type.text == "Text" || type.text == "Bytes" || type.text == "CString")) {
        return StringLiteralTypingState::AcceptsStringLiteral;
    }
    return StringLiteralTypingState::RequiresTextDefault;
}

const ast::PathExpr* Lowerer::path_expr(const ast::Expr& expr) const {
    return expr.kind == ast::ExprKind::Path ? &static_cast<const ast::PathExpr&>(expr) : nullptr;
}

const ast::CallExpr* Lowerer::call_expr(const ast::Expr& expr) const {
    return expr.kind == ast::ExprKind::Call ? &static_cast<const ast::CallExpr&>(expr) : nullptr;
}

const TypeRef* Lowerer::lookup_binding(const ValueEnv& env, const std::string& name) const {
    for (const ValueEnv* current = &env; current != nullptr; current = current->parent) {
        const auto it = current->values.find(name);
        if (it != current->values.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

void Lowerer::bind_value(ValueEnv& env, const std::string& name, TypeRef type) const {
    env.values[name] = std::move(type);
}

TypeEquivalenceState Lowerer::type_equivalence_state(const TypeRef& lhs, const TypeRef& rhs) const {
    if (lhs.text != rhs.text
        || lhs.identity.source() != rhs.identity.source()
        || lhs.identity.package_type_id() != rhs.identity.package_type_id()
        || lhs.flavor != rhs.flavor
        || lhs.discipline != rhs.discipline || lhs.args.size() != rhs.args.size()) {
        return TypeEquivalenceState::DifferentTypes;
    }
    for (std::size_t index = 0; index < lhs.args.size(); ++index) {
        if (type_equivalence_state(lhs.args[index], rhs.args[index]) == TypeEquivalenceState::DifferentTypes) {
            return TypeEquivalenceState::DifferentTypes;
        }
    }
    return TypeEquivalenceState::SameType;
}

TypeRef Lowerer::unify_types(const TypeRef& lhs, const TypeRef& rhs) const {
    if (type_ref_error_state(lhs) == typesys::TypeErrorState::SuppressesFollowupDiagnostics) {
        return rhs;
    }
    if (type_ref_error_state(rhs) == typesys::TypeErrorState::SuppressesFollowupDiagnostics) {
        return lhs;
    }
    if (lhs.text == "Never") {
        return rhs;
    }
    if (rhs.text == "Never") {
        return lhs;
    }
    if (type_equivalence_state(lhs, rhs) == TypeEquivalenceState::SameType) {
        return lhs;
    }
    return lhs;
}

Lowerer::ExpectedFieldTypes Lowerer::expected_field_types_from_hir_fields(const std::vector<FieldDecl>& fields) const {
    ExpectedFieldTypes expected;
    expected.reserve(fields.size());
    for (const FieldDecl& field : fields) {
        expected.emplace(field.name, field.type);
    }
    return expected;
}

Lowerer::ExpectedFieldTypes Lowerer::expected_field_types_from_ast_fields(
    const ast::Decl* owner_decl,
    const std::vector<ast::Field>& fields,
    const std::vector<typesys::Type>& owner_args) const {
    ExpectedFieldTypes expected;
    expected.reserve(fields.size());
    for (const ast::Field& field : fields) {
        expected.emplace(field.name, lower_resolved_type(resolve_member_type(owner_decl, owner_args, field.type)));
    }
    return expected;
}

std::vector<FieldInit> Lowerer::lower_field_inits(const std::vector<ast::RecordFieldInit>& fields,
                                                  const FunctionContext& context,
                                                  ValueEnv& env,
                                                  const ExpectedFieldTypes* expected_types) {
    std::vector<FieldInit> lowered;
    lowered.reserve(fields.size());
    for (const ast::RecordFieldInit& field : fields) {
        FieldInit init;
        init.name = field.name;
        init.spelling = field.spelling;
        const TypeRef* expected_type = nullptr;
        if (expected_types != nullptr) {
            const auto expected_it = expected_types->find(field.name);
            if (expected_it != expected_types->end()) {
                expected_type = &expected_it->second;
            }
        }
        init.value = lower_expr(*field.value, context, env, expected_type);
        lowered.push_back(std::move(init));
    }
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_path_expr(const ast::PathExpr& expr,
                                               const FunctionContext& context,
                                               ValueEnv& env) {
    if (expr.path.size() == 1) {
        if (const TypeRef* binding = lookup_binding(env, expr.path.front()); binding != nullptr) {
            return std::make_unique<LocalRefExpr>(expr.path.front(), *binding);
        }
    }

    if (VariantResolution state_variant = resolve_variant(
            context.scope,
            expr.path,
            VariantOwnerKind::StateDeclaration);
        state_variant.state == VariantResolutionState::UniqueVariant) {
        const VariantDecl& variant = package.variants[state_variant.variant_id];
        if (variant.fields.empty()) {
            auto lowered = std::make_unique<ConstructExpr>(named_type(variant.owner_type));
            lowered->construct_kind = ConstructKind::StateVariant;
            lowered->owner_type_id = variant.owner_type;
            lowered->variant_target = ConstructVariantTarget::constructs_named_variant(variant.id);
            lowered->qualified_name = variant.qualified_name;
            return lowered;
        }
    }

    if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
        const auto type_it = type_ids.find(symbol->decl);
        if (type_it != type_ids.end()) {
            const TypeDecl& type = package.types[type_it->second];
            if (type.kind == TypeKind::Record && type.fields.empty()) {
                auto lowered = std::make_unique<ConstructExpr>(named_type(type.id));
                lowered->construct_kind = ConstructKind::Record;
                lowered->owner_type_id = type.id;
                lowered->qualified_name = type.qualified_name;
                return lowered;
            }
        }
    }

    if (expr.path.size() >= 2) {
        std::vector<std::string> family_path(expr.path.begin(), expr.path.end() - 1);
        const std::string& position_name = expr.path.back();
        if (const Symbol* sym = resolve_symbol(context.scope, context.generics, family_path); sym != nullptr
            && sym->decl->kind == ast::DeclKind::Phase) {
            const auto& pd = static_cast<const ast::PhaseDecl&>(*sym->decl);
            if (std::find(pd.positions.begin(), pd.positions.end(), position_name) != pd.positions.end()) {
                const std::string qn = qualified_names.at(sym->decl) + "::" + position_name;
                const auto tid_it = qualified_type_ids.find(qn);
                if (tid_it != qualified_type_ids.end()) {
                    return std::make_unique<LocalRefExpr>(format_path(expr.path), named_type(tid_it->second));
                }
            }
        }
    }

    return std::make_unique<LocalRefExpr>(format_path(expr.path), TypeRef{});
}

std::unique_ptr<Expr> Lowerer::lower_call_expr(const ast::CallExpr& expr,
                                               const FunctionContext& context,
                                               ValueEnv& env) {
    auto lowered = std::make_unique<CallExpr>();
    const FunctionDecl* function = nullptr;
    const ast::FunctionDecl* ast_function = nullptr;
    std::vector<TypeRef> concrete_param_types;
    if (const ast::PathExpr* callee_path = path_expr(*expr.callee)) {
        const Symbol* symbol = resolve_symbol(context.scope, {}, callee_path->path);
        if (symbol != nullptr
            && (symbol->kind == ast::DeclKind::Function || symbol->kind == ast::DeclKind::ForeignFunction)) {
            ast_function = static_cast<const ast::FunctionDecl*>(symbol->decl);
        }
        if (const FunctionDecl* resolved = resolve_function(context.scope, callee_path->path); resolved != nullptr) {
            function = resolved;
            lowered->function_id = function->id;
            lowered->callee_name = function->qualified_name;
            lowered->result_type = function->return_type;
            if (function->failure.behavior() == FunctionFailureBehavior::YieldsReason) {
                lowered->failure = ExprFailureContract::yields_reason(function->failure.reason_type_id());
            }
            if (ast_function != nullptr) {
                const Scope& callee_scope = *decl_scopes.at(ast_function);
                const std::vector<std::string> callee_generics = generic_names_for(
                    ast_function->signature.generic_params);
                const std::vector<std::pair<std::string, typesys::Type>> substitutions
                    = build_call_substitutions(*ast_function, expr, context);
                for (const auto& [_, actual_type] : substitutions) {
                    lowered->type_args.push_back(lower_resolved_type(actual_type));
                }
                lowered->result_type = lower_resolved_type(resolve_type(callee_scope,
                                                                        callee_generics,
                                                                        substitutions,
                                                                        ast_function->signature.return_type));
                if (ast_function->signature.failure.behavior() == ast::FunctionFailureBehavior::YieldsReason) {
                    TypeRef fails_type = lower_resolved_type(resolve_type(callee_scope,
                                                                         callee_generics,
                                                                         substitutions,
                                                                         ast_function->signature.failure.reason_type()));
                    if (fails_type.identity.source() == TypeIdentitySource::PackageTypeDeclaration) {
                        lowered->failure = ExprFailureContract::yields_reason(fails_type.identity.package_type_id());
                    }
                }
                concrete_param_types.reserve(ast_function->signature.params.size());
                for (const ast::Parameter& param : ast_function->signature.params) {
                    concrete_param_types.push_back(lower_resolved_type(resolve_type(callee_scope,
                                                                                   callee_generics,
                                                                                   substitutions,
                                                                                   param.type)));
                }
            }
        } else {
            lowered->callee_name = format_path(callee_path->path);
        }
    }
    for (std::size_t index = 0; index < expr.args.size(); ++index) {
        const TypeRef* concrete_arg_type = nullptr;
        const TypeRef* declared_arg_type = nullptr;
        if (index < concrete_param_types.size()) {
            concrete_arg_type = &concrete_param_types[index];
        }
        if (function != nullptr && index < function->params.size()) {
            declared_arg_type = &function->params[index].type;
        }
        const TypeRef* expected_arg_type = concrete_arg_type != nullptr ? concrete_arg_type : declared_arg_type;
        if (argument_lowering_mode(concrete_arg_type, declared_arg_type)
            == ArgumentLoweringMode::PreserveCompileTimePath) {
            if (const ast::PathExpr* path_arg = path_expr(*expr.args[index])) {
                lowered->args.push_back(lower_path_expr(*path_arg, context, env));
            } else {
                lowered->args.push_back(lower_expr(*expr.args[index], context, env, expected_arg_type));
            }
            continue;
        }
        lowered->args.push_back(lower_expr(*expr.args[index], context, env, expected_arg_type));
    }
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_construct_expr(const ast::ConstructExpr& expr,
                                                    const FunctionContext& context,
                                                    ValueEnv& env) {
    auto lowered = std::make_unique<ConstructExpr>();

    if (expr.path.size() >= 2) {
        std::vector<std::string> family_path(expr.path.begin(), expr.path.end() - 1);
        const std::string& position_name = expr.path.back();
        if (const Symbol* sym = resolve_symbol(context.scope, context.generics, family_path); sym != nullptr
            && sym->decl->kind == ast::DeclKind::Phase) {
            const auto& pd = static_cast<const ast::PhaseDecl&>(*sym->decl);
            if (std::find(pd.positions.begin(), pd.positions.end(), position_name) != pd.positions.end()) {
                const std::string qn = qualified_names.at(sym->decl) + "::" + position_name;
                const auto tid_it = qualified_type_ids.find(qn);
                if (tid_it != qualified_type_ids.end()) {
                    const TypeDecl& type = package.types[tid_it->second];
                    lowered->result_type = named_type(type.id);
                    lowered->construct_kind = ConstructKind::Record;
                    lowered->owner_type_id = type.id;
                    lowered->qualified_name = type.qualified_name;
                    const ExpectedFieldTypes expected = expected_field_types_from_hir_fields(type.fields);
                    lowered->fields = lower_field_inits(expr.fields, context, env, &expected);
                    return lowered;
                }
            }
        }
    }

    if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
        const auto type_it = type_ids.find(symbol->decl);
        if (type_it != type_ids.end()) {
            const TypeDecl& type = package.types[type_it->second];
            if (type.kind == TypeKind::Record) {
                const auto& record_decl = static_cast<const ast::RecordDecl&>(*symbol->decl);
                std::vector<typesys::Type> type_args;
                type_args.reserve(expr.type_args.size());
                for (const ast::TypeRef& type_arg : expr.type_args) {
                    type_args.push_back(resolve_type(context.scope, context.generics, {}, type_arg));
                }
                lowered->result_type = lower_resolved_type(typesys::named_type(type.qualified_name,
                                                                                symbol->decl,
                                                                                type_args));
                lowered->construct_kind = ConstructKind::Record;
                lowered->owner_type_id = type.id;
                lowered->qualified_name = type_ref_error_state(lowered->result_type)
                        == typesys::TypeErrorState::SuppressesFollowupDiagnostics
                    ? type.qualified_name
                    : lowered->result_type.text;
                const ExpectedFieldTypes expected = expected_field_types_from_ast_fields(symbol->decl,
                                                                                         record_decl.fields,
                                                                                         type_args);
                lowered->fields = lower_field_inits(expr.fields, context, env, &expected);
                return lowered;
            }
        }
    }

    if (VariantResolution state_variant = resolve_variant(
            context.scope,
            expr.path,
            VariantOwnerKind::StateDeclaration);
        state_variant.state == VariantResolutionState::UniqueVariant) {
        const VariantDecl& variant = package.variants[state_variant.variant_id];
        lowered->result_type = named_type(variant.owner_type);
        lowered->construct_kind = ConstructKind::StateVariant;
        lowered->owner_type_id = variant.owner_type;
        lowered->variant_target = ConstructVariantTarget::constructs_named_variant(variant.id);
        lowered->qualified_name = variant.qualified_name;
        const ExpectedFieldTypes expected = expected_field_types_from_hir_fields(variant.fields);
        lowered->fields = lower_field_inits(expr.fields, context, env, &expected);
        return lowered;
    }

    lowered->fields = lower_field_inits(expr.fields, context, env);
    return lowered;
}

std::unique_ptr<VariantPattern> Lowerer::lower_variant_pattern(const ast::VariantPattern& pattern,
                                                               VariantId variant_id) {
    const VariantDecl& variant = package.variants[variant_id];
    auto lowered = std::make_unique<VariantPattern>();
    lowered->owner_type_id = variant.owner_type;
    lowered->variant_id = variant.id;
    lowered->owner_name = package.types[variant.owner_type].qualified_name;
    lowered->variant_name = variant.name;
    switch (pattern.payload_mode) {
    case ast::VariantPattern::PayloadMode::None:
        lowered->payload_mode = VariantPattern::PayloadMode::None;
        break;
    case ast::VariantPattern::PayloadMode::Bindings:
        lowered->payload_mode = VariantPattern::PayloadMode::Bindings;
        break;
    case ast::VariantPattern::PayloadMode::Ignore:
        lowered->payload_mode = VariantPattern::PayloadMode::Ignore;
        break;
    }
    for (const auto& binding : pattern.bindings) {
        lowered->bindings.push_back(Binding{binding.field_name, binding.binding_name});
    }
    return lowered;
}

void Lowerer::bind_variant_fields(ValueEnv& env,
                                  const VariantPattern& pattern,
                                  const VariantDecl& variant) const {
    if (pattern.payload_mode != VariantPattern::PayloadMode::Bindings) {
        return;
    }
    for (const Binding& binding : pattern.bindings) {
        for (const FieldDecl& field : variant.fields) {
            if (field.name == binding.field_name) {
                bind_value(env, binding.binding_name, field.type);
                break;
            }
        }
    }
}

std::unique_ptr<Expr> Lowerer::lower_match_expr(const ast::MatchExpr& expr,
                                                const FunctionContext& context,
                                                ValueEnv& env,
                                                const TypeRef* expected_type) {
    auto lowered = std::make_unique<MatchExpr>();
    lowered->scrutinee = lower_expr(*expr.scrutinee, context, env);
    TypeRef result_type;

    for (const ast::MatchArm& arm : expr.arms) {
        MatchArm lowered_arm;
        ValueEnv arm_env{&env, {}};

        if (lowered->scrutinee->failure.effect() == ExprFailureEffect::YieldsReason) {
            switch (arm.pattern->kind) {
            case ast::PatternKind::Succeeded: {
                const auto& pattern = static_cast<const ast::SucceededPattern&>(*arm.pattern);
                auto lowered_pattern = std::make_unique<SucceededPattern>();
                lowered_pattern->binding_name = pattern.binding_name;
                lowered_pattern->binding = pattern.binding;
                if (pattern.binding == ast::SuccessPatternBinding::NamedBinding) {
                    bind_value(arm_env, pattern.binding_name, lowered->scrutinee->result_type);
                }
                lowered_arm.pattern = std::move(lowered_pattern);
                break;
            }
            case ast::PatternKind::Failed: {
                const auto& pattern = static_cast<const ast::FailedPattern&>(*arm.pattern);
                VariantResolution resolution = resolve_variant(
                    context.scope,
                    pattern.variant->path,
                    VariantOwnerKind::ReasonDeclaration);
                auto lowered_pattern = std::make_unique<FailedPattern>();
                if (resolution.state == VariantResolutionState::UniqueVariant) {
                    lowered_pattern->variant = lower_variant_pattern(*pattern.variant, resolution.variant_id);
                    bind_variant_fields(arm_env, *lowered_pattern->variant, package.variants[resolution.variant_id]);
                }
                lowered_arm.pattern = std::move(lowered_pattern);
                break;
            }
            case ast::PatternKind::Variant:
                break;
            }
        } else if (arm.pattern->kind == ast::PatternKind::Variant) {
            const auto& pattern = static_cast<const ast::VariantPattern&>(*arm.pattern);
            VariantResolution resolution = resolve_variant(
                context.scope,
                pattern.path,
                VariantOwnerKind::StateDeclaration);
            if (resolution.state == VariantResolutionState::UniqueVariant) {
                auto lowered_pattern = lower_variant_pattern(pattern, resolution.variant_id);
                bind_variant_fields(arm_env,
                                    static_cast<const VariantPattern&>(*lowered_pattern),
                                    package.variants[resolution.variant_id]);
                lowered_arm.pattern = std::move(lowered_pattern);
            }
        }

        lowered_arm.body = lower_expr(*arm.body, context, arm_env, expected_type);
        result_type = unify_types(result_type, lowered_arm.body->result_type);
        lowered->arms.push_back(std::move(lowered_arm));
    }

    lowered->result_type = type_ref_error_state(result_type) == typesys::TypeErrorState::SuppressesFollowupDiagnostics
        ? builtin_type("Unit")
        : result_type;
    return lowered;
}

std::unique_ptr<BlockExpr> Lowerer::lower_block_expr(const ast::BlockExpr& expr,
                                                     const FunctionContext& context,
                                                     ValueEnv& env,
                                                     const TypeRef* expected_type) {
    auto lowered = std::make_unique<BlockExpr>();
    ValueEnv local{&env, {}};

    for (const auto& stmt_ptr : expr.statements) {
        const ast::Stmt& stmt = *stmt_ptr;
        switch (stmt.kind) {
        case ast::StmtKind::Let: {
            const auto& let_stmt = static_cast<const ast::LetStmt&>(stmt);
            auto lowered_stmt = std::make_unique<LetStmt>();
            lowered_stmt->name = let_stmt.name;
            lowered_stmt->initializer = lower_expr(*let_stmt.initializer, context, local);
            lowered_stmt->type = lowered_stmt->initializer->result_type;
            bind_value(local, lowered_stmt->name, lowered_stmt->type);
            lowered->statements.push_back(std::move(lowered_stmt));
            break;
        }
        case ast::StmtKind::Expr: {
            const auto& expr_stmt = static_cast<const ast::ExprStmt&>(stmt);
            auto lowered_stmt = std::make_unique<ExprStmt>();
            lowered_stmt->expr = lower_expr(*expr_stmt.expr, context, local);
            lowered->statements.push_back(std::move(lowered_stmt));
            break;
        }
        }
    }

    if (expr.result != nullptr) {
        lowered->result = lower_expr(*expr.result, context, local, expected_type);
        lowered->result_type = lowered->result->result_type;
        lowered->failure = lowered->result->failure;
    } else {
        lowered->result = std::make_unique<UnitExpr>(builtin_type("Unit"));
        lowered->result_type = builtin_type("Unit");
    }
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_fail_expr(const ast::FailExpr& expr,
                                               const FunctionContext& context,
                                               ValueEnv& env) {
    auto lowered = std::make_unique<FailExpr>(builtin_type("Never"));
    if (VariantResolution resolution = resolve_variant(
            context.scope,
            expr.path,
            VariantOwnerKind::ReasonDeclaration);
        resolution.state == VariantResolutionState::UniqueVariant) {
        lowered->reason_type_id = type_ids.at(resolution.owner_decl);
        lowered->variant_id = resolution.variant_id;
        lowered->reason_name = package.types[lowered->reason_type_id].qualified_name;
        lowered->variant_name = package.variants[resolution.variant_id].name;
        const ExpectedFieldTypes expected = expected_field_types_from_hir_fields(package.variants[resolution.variant_id].fields);
        lowered->fields = lower_field_inits(expr.fields, context, env, &expected);
        return lowered;
    }
    lowered->fields = lower_field_inits(expr.fields, context, env);
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_field_access_expr(const ast::FieldAccessExpr& expr,
                                                       const FunctionContext& context,
                                                       ValueEnv& env) {
    auto lowered = std::make_unique<FieldAccessExpr>();
    lowered->object = lower_expr(*expr.object, context, env);
    lowered->field_name = expr.field_name;
    lowered->result_type = TypeRef{};
    if (lowered->object->result_type.identity.source() == TypeIdentitySource::PackageTypeDeclaration) {
        const TypeDecl& owner = package.types[lowered->object->result_type.identity.package_type_id()];
        std::unordered_map<std::string, TypeRef> substitutions;
        const std::size_t substitution_count = std::min(owner.generics.size(), lowered->object->result_type.args.size());
        for (std::size_t index = 0; index < substitution_count; ++index) {
            substitutions.emplace(owner.generics[index], lowered->object->result_type.args[index]);
        }
        for (const FieldDecl& field : owner.fields) {
            if (field.name == expr.field_name) {
                lowered->result_type = substitute_type_ref(field.type, substitutions);
                break;
            }
        }
    }
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_grant_expr(const ast::GrantExpr& expr,
                                                const FunctionContext& context,
                                                ValueEnv& env,
                                                const TypeRef* expected_type) {
    auto lowered = std::make_unique<GrantExpr>();
    lowered->result_type = builtin_type("Unit");

    if (const ast::CallExpr* grant_call = call_expr(*expr.grant_call)) {
        if (const ast::PathExpr* callee_path = path_expr(*grant_call->callee)) {
            if (const FunctionDecl* function = resolve_function(context.scope, callee_path->path); function != nullptr) {
                lowered->grant_function_id = function->id;
                lowered->grant_name = function->qualified_name;
                if (function->failure.behavior() == FunctionFailureBehavior::YieldsReason) {
                    lowered->failure = ExprFailureContract::yields_reason(function->failure.reason_type_id());
                }
                if (function->authority.effect() == FunctionAuthorityEffect::GrantsScopedPermit) {
                    lowered->permit_type_id = function->authority.permit_type_id();
                    lowered->permit_name = package.types[lowered->permit_type_id].qualified_name;
                }
                for (std::size_t index = 0; index < grant_call->args.size(); ++index) {
                    const TypeRef* expected_arg_type = index < function->params.size()
                        ? &function->params[index].type
                        : nullptr;
                    if (argument_lowering_mode(nullptr, expected_arg_type)
                        == ArgumentLoweringMode::PreserveCompileTimePath) {
                        if (const ast::PathExpr* path_arg = path_expr(*grant_call->args[index])) {
                            lowered->args.push_back(lower_path_expr(*path_arg, context, env));
                        } else {
                            lowered->args.push_back(lower_expr(*grant_call->args[index], context, env, expected_arg_type));
                        }
                    } else {
                        lowered->args.push_back(lower_expr(*grant_call->args[index], context, env, expected_arg_type));
                    }
                }
            }
        }
    }

    ValueEnv body_env{&env, {}};
    if (!lowered->permit_name.empty()) {
        bind_value(body_env, expr.binder_name, named_type(lowered->permit_type_id));
    }
    lowered->binder_name = expr.binder_name;
    lowered->body = lower_block_expr(*expr.body, context, body_env, expected_type);
    lowered->result_type = lowered->body->result_type;
    if (lowered->failure.effect() != ExprFailureEffect::YieldsReason) {
        lowered->failure = lowered->body->failure;
    }
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_prove_expr(const ast::ProveExpr& expr,
                                                const FunctionContext& context,
                                                ValueEnv& env) {
    auto lowered = std::make_unique<ProveExpr>();
    if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
        const auto type_it = type_ids.find(symbol->decl);
        if (type_it != type_ids.end()) {
            const TypeDecl& type = package.types[type_it->second];
            if (type.kind == TypeKind::Proof) {
                lowered->proof_type_id = type.id;
                lowered->qualified_name = type.qualified_name;
                lowered->result_type = named_type(type.id);
                const ExpectedFieldTypes expected = expected_field_types_from_hir_fields(type.fields);
                lowered->fields = lower_field_inits(expr.fields, context, env, &expected);
                return lowered;
            }
        }
    }
    lowered->fields = lower_field_inits(expr.fields, context, env);
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_expr(const ast::Expr& expr,
                                          const FunctionContext& context,
                                          ValueEnv& env,
                                          const TypeRef* expected_type) {
    switch (expr.kind) {
    case ast::ExprKind::NumberLiteral:
        return std::make_unique<NumberLiteralExpr>(
            static_cast<const ast::NumberLiteralExpr&>(expr).lexeme, builtin_type("Int"));
    case ast::ExprKind::StringLiteral: {
        const auto& literal_expr = static_cast<const ast::StringLiteralExpr&>(expr);
        if (expected_type != nullptr
            && string_literal_typing_state(*expected_type) == StringLiteralTypingState::AcceptsStringLiteral) {
            return std::make_unique<StringLiteralExpr>(literal_expr.lexeme, *expected_type);
        }
        return std::make_unique<StringLiteralExpr>(literal_expr.lexeme, builtin_type("Text"));
    }
    case ast::ExprKind::Path:
        return lower_path_expr(static_cast<const ast::PathExpr&>(expr), context, env);
    case ast::ExprKind::Call:
        return lower_call_expr(static_cast<const ast::CallExpr&>(expr), context, env);
    case ast::ExprKind::Construct:
        return lower_construct_expr(static_cast<const ast::ConstructExpr&>(expr), context, env);
    case ast::ExprKind::Try: {
        const auto& try_expr = static_cast<const ast::TryExpr&>(expr);
        auto lowered = std::make_unique<TryExpr>();
        lowered->operand = lower_expr(*try_expr.operand, context, env);
        lowered->result_type = lowered->operand->result_type;
        return lowered;
    }
    case ast::ExprKind::Match:
        return lower_match_expr(static_cast<const ast::MatchExpr&>(expr), context, env, expected_type);
    case ast::ExprKind::Block:
        return lower_block_expr(static_cast<const ast::BlockExpr&>(expr), context, env, expected_type);
    case ast::ExprKind::Fail:
        return lower_fail_expr(static_cast<const ast::FailExpr&>(expr), context, env);
    case ast::ExprKind::Grant:
        return lower_grant_expr(static_cast<const ast::GrantExpr&>(expr), context, env, expected_type);
    case ast::ExprKind::FieldAccess:
        return lower_field_access_expr(static_cast<const ast::FieldAccessExpr&>(expr), context, env);
    case ast::ExprKind::Prove:
        return lower_prove_expr(static_cast<const ast::ProveExpr&>(expr), context, env);
    }
    return std::make_unique<UnitExpr>(builtin_type("Unit"));
}

void dump_pattern(std::ostream& out, const Pattern& pattern, std::size_t depth);
void dump_expr(std::ostream& out, const Expr& expr, std::size_t depth);

std::string function_specialization_name(const FunctionDecl& function, const std::vector<TypeRef>& type_args) {
    std::ostringstream out;
    out << function.qualified_name << '$';
    for (std::size_t index = 0; index < type_args.size(); ++index) {
        if (index > 0) {
            out << '$';
        }
        for (char ch : type_args[index].text) {
            if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '$') {
                out << ch;
            } else {
                out << '_';
            }
        }
    }
    return out.str();
}

std::string type_specialization_name(const TypeDecl& type, const std::vector<TypeRef>& type_args) {
    std::ostringstream out;
    out << type.qualified_name << '$';
    for (std::size_t index = 0; index < type_args.size(); ++index) {
        if (index > 0) {
            out << '$';
        }
        for (char ch : type_args[index].text) {
            if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '$') {
                out << ch;
            } else {
                out << '_';
            }
        }
    }
    return out.str();
}

std::string specialization_key(std::string_view prefix, std::size_t source_id, const std::vector<TypeRef>& type_args) {
    std::ostringstream out;
    out << prefix << source_id << '<';
    for (std::size_t index = 0; index < type_args.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << type_args[index].text;
    }
    out << '>';
    return out.str();
}

enum class DeclarationRemapState {
    RequiresBaseOutputSignature,
    CopiedToBaseOutput,
    SpecializedOnDemand,
};

template <typename Id>
class DeclarationRemap final {
public:
    [[nodiscard]] static DeclarationRemap requires_base_output_signature() {
        return DeclarationRemap(DeclarationRemapState::RequiresBaseOutputSignature, Id{});
    }

    [[nodiscard]] static DeclarationRemap copied_to_base_output(Id id) {
        return DeclarationRemap(DeclarationRemapState::CopiedToBaseOutput, id);
    }

    [[nodiscard]] static DeclarationRemap specialized_on_demand() {
        return DeclarationRemap(DeclarationRemapState::SpecializedOnDemand, Id{});
    }

    [[nodiscard]] DeclarationRemapState state() const noexcept { return state_; }
    [[nodiscard]] Id output_id() const noexcept { return output_id_; }

private:
    DeclarationRemapState state_;
    Id output_id_;

    DeclarationRemap(DeclarationRemapState state, Id output_id)
        : state_(state), output_id_(output_id) {}
};

class Monomorphizer {
public:
    explicit Monomorphizer(const Package& source_package)
        : source(source_package) {
        base_type_ids.reserve(source.types.size());
        for (const TypeDecl& type : source.types) {
            base_type_ids.push_back(type.generics.empty()
                ? DeclarationRemap<TypeId>::requires_base_output_signature()
                : DeclarationRemap<TypeId>::specialized_on_demand());
        }

        base_variant_ids.reserve(source.variants.size());
        for (const VariantDecl& variant : source.variants) {
            const DeclarationRemapState owner_state = variant.owner_type < source.types.size()
                ? base_type_ids[variant.owner_type].state()
                : DeclarationRemapState::SpecializedOnDemand;
            base_variant_ids.push_back(owner_state == DeclarationRemapState::RequiresBaseOutputSignature
                ? DeclarationRemap<VariantId>::requires_base_output_signature()
                : DeclarationRemap<VariantId>::specialized_on_demand());
        }

        base_function_ids.reserve(source.functions.size());
        for (const FunctionDecl& function : source.functions) {
            base_function_ids.push_back(function.generics.empty()
                ? DeclarationRemap<FunctionId>::requires_base_output_signature()
                : DeclarationRemap<FunctionId>::specialized_on_demand());
        }
    }

    [[nodiscard]] Package run() {
        for (const TypeDecl& type : source.types) {
            if (!type.generics.empty()) {
                continue;
            }
            const TypeId new_id = add_type_signature(type, type.qualified_name);
            base_type_ids[type.id] = DeclarationRemap<TypeId>::copied_to_base_output(new_id);
        }

        for (const VariantDecl& variant : source.variants) {
            if (variant.owner_type >= base_type_ids.size()
                || base_type_ids[variant.owner_type].state() != DeclarationRemapState::CopiedToBaseOutput) {
                continue;
            }
            const VariantId new_id = add_variant_signature(variant, base_type_ids[variant.owner_type].output_id());
            base_variant_ids[variant.id] = DeclarationRemap<VariantId>::copied_to_base_output(new_id);
        }

        for (const TypeDecl& type : source.types) {
            if (type.id >= base_type_ids.size()
                || base_type_ids[type.id].state() != DeclarationRemapState::CopiedToBaseOutput) {
                continue;
            }
            const TypeId mapped_id = base_type_ids[type.id].output_id();
            output.types[mapped_id].fields = clone_fields(type.fields, {});
            output.types[mapped_id].discipline = type.discipline;
        }

        for (const VariantDecl& variant : source.variants) {
            if (variant.id >= base_variant_ids.size()
                || base_variant_ids[variant.id].state() != DeclarationRemapState::CopiedToBaseOutput) {
                continue;
            }
            output.variants[base_variant_ids[variant.id].output_id()].fields = clone_fields(variant.fields, {});
        }

        for (const FunctionDecl& function : source.functions) {
            if (!function.generics.empty()) {
                continue;
            }
            const FunctionId new_id = add_signature(function, function.qualified_name, {});
            base_function_ids[function.id] = DeclarationRemap<FunctionId>::copied_to_base_output(new_id);
        }

        for (const FunctionDecl& function : source.functions) {
            if (!function.generics.empty()) {
                continue;
            }
            if (base_function_ids[function.id].state() != DeclarationRemapState::CopiedToBaseOutput
                || function.body == nullptr) {
                continue;
            }
            output.functions[base_function_ids[function.id].output_id()].body = clone_block(*function.body, {});
        }

        return std::move(output);
    }

private:
    const Package& source;
    Package output;
    std::vector<DeclarationRemap<TypeId>> base_type_ids;
    std::vector<DeclarationRemap<VariantId>> base_variant_ids;
    std::vector<DeclarationRemap<FunctionId>> base_function_ids;
    std::unordered_map<std::string, TypeId> type_specializations;
    std::unordered_map<std::string, FunctionId> function_specializations;

    using Substitutions = std::unordered_map<std::string, TypeRef>;

    [[nodiscard]] TypeId add_type_signature(const TypeDecl& source_type, std::string qualified_name) {
        TypeDecl cloned;
        cloned.id = output.types.size();
        cloned.kind = source_type.kind;
        cloned.visibility = source_type.visibility;
        cloned.qualified_name = std::move(qualified_name);
        cloned.discipline = source_type.discipline;
        output.types.push_back(std::move(cloned));
        return output.types.back().id;
    }

    [[nodiscard]] VariantId add_variant_signature(const VariantDecl& source_variant, TypeId owner_type) {
        VariantDecl cloned;
        cloned.id = output.variants.size();
        cloned.owner_type = owner_type;
        cloned.qualified_name = source_variant.qualified_name;
        cloned.name = source_variant.name;
        output.variants.push_back(std::move(cloned));
        output.types[owner_type].variants.push_back(output.variants.back().id);
        return output.variants.back().id;
    }

    [[nodiscard]] std::vector<FieldDecl> clone_fields(const std::vector<FieldDecl>& fields,
                                                      const Substitutions& substitutions) {
        std::vector<FieldDecl> cloned;
        cloned.reserve(fields.size());
        for (const FieldDecl& field : fields) {
            cloned.push_back(FieldDecl{
                field.name,
                substitute_type(field.type, substitutions),
            });
        }
        return cloned;
    }

    [[nodiscard]] typesys::UseDiscipline discipline_for_fields(const std::vector<FieldDecl>& fields) const {
        typesys::UseDiscipline discipline = typesys::UseDiscipline::Copyable;
        for (const FieldDecl& field : fields) {
            discipline = typesys::merge_discipline(discipline, field.type.discipline);
        }
        return discipline;
    }

    [[nodiscard]] ExprFailureContract map_expr_failure(const ExprFailureContract& source,
                                                       const Substitutions& substitutions) {
        if (source.effect() != ExprFailureEffect::YieldsReason) {
            return ExprFailureContract::returns_value_only();
        }
        return ExprFailureContract::yields_reason(map_type_id(source.reason_type_id(), {}, substitutions));
    }

    [[nodiscard]] VariantId map_variant_id(VariantId source_id) const {
        if (source_id >= base_variant_ids.size()) {
            return source_id;
        }
        const DeclarationRemap<VariantId>& mapped = base_variant_ids[source_id];
        if (mapped.state() == DeclarationRemapState::CopiedToBaseOutput) {
            return mapped.output_id();
        }
        return source_id;
    }

    [[nodiscard]] TypeId map_type_id(TypeId source_id,
                                     const std::vector<TypeRef>& type_args,
                                     const Substitutions& substitutions) {
        if (source_id >= source.types.size()) {
            return source_id;
        }
        const TypeDecl& type = source.types[source_id];
        if (type.generics.empty()) {
            const DeclarationRemap<TypeId>& mapped = base_type_ids[source_id];
            if (mapped.state() == DeclarationRemapState::CopiedToBaseOutput) {
                return mapped.output_id();
            }
            return source_id;
        }

        std::vector<TypeRef> concrete_args;
        concrete_args.reserve(type_args.size());
        for (const TypeRef& arg : type_args) {
            concrete_args.push_back(substitute_type(arg, substitutions));
        }

        const std::string key = specialization_key("type:", source_id, concrete_args);
        if (const auto it = type_specializations.find(key); it != type_specializations.end()) {
            return it->second;
        }

        Substitutions instance_substitutions;
        const std::size_t count = std::min(type.generics.size(), concrete_args.size());
        for (std::size_t index = 0; index < count; ++index) {
            instance_substitutions.emplace(type.generics[index], concrete_args[index]);
        }

        const TypeId instance_id = add_type_signature(type, type_specialization_name(type, concrete_args));
        type_specializations.emplace(key, instance_id);
        output.types[instance_id].fields = clone_fields(type.fields, instance_substitutions);
        output.types[instance_id].discipline = TypeDisciplineResolution::concrete_use_discipline(
            discipline_for_fields(output.types[instance_id].fields));
        return instance_id;
    }

    [[nodiscard]] FunctionId add_signature(const FunctionDecl& source_function,
                                           std::string qualified_name,
                                           const Substitutions& substitutions) {
        FunctionDecl cloned;
        cloned.id = output.functions.size();
        cloned.visibility = source_function.visibility;
        cloned.qualified_name = std::move(qualified_name);
        cloned.return_type = substitute_type(source_function.return_type, substitutions);
        if (source_function.failure.behavior() == FunctionFailureBehavior::YieldsReason) {
            cloned.failure = FunctionFailureContract::yields_reason(
                map_type_id(source_function.failure.reason_type_id(), {}, substitutions));
        }
        if (source_function.authority.effect() == FunctionAuthorityEffect::GrantsScopedPermit) {
            cloned.authority = FunctionAuthorityContract::grants_scoped_permit(
                map_type_id(source_function.authority.permit_type_id(), {}, substitutions));
        }
        cloned.proves_proof_type_ids.reserve(source_function.proves_proof_type_ids.size());
        for (TypeId proof_type_id : source_function.proves_proof_type_ids) {
            cloned.proves_proof_type_ids.push_back(map_type_id(proof_type_id, {}, substitutions));
        }
        cloned.implementation = source_function.implementation;
        cloned.params.reserve(source_function.params.size());
        for (const Parameter& param : source_function.params) {
            const TypeRef param_type = substitute_type(param.type, substitutions);
            cloned.params.push_back(Parameter{
                param.name,
                param_type,
                param.authority,
            });
        }
        output.functions.push_back(std::move(cloned));
        return output.functions.back().id;
    }

    [[nodiscard]] FunctionId map_function_id(FunctionId source_id,
                                             const std::vector<TypeRef>& type_args,
                                             const Substitutions& substitutions) {
        if (source_id >= source.functions.size()) {
            return source_id;
        }
        const FunctionDecl& function = source.functions[source_id];
        if (function.generics.empty()) {
            const DeclarationRemap<FunctionId>& mapped = base_function_ids[source_id];
            if (mapped.state() == DeclarationRemapState::CopiedToBaseOutput) {
                return mapped.output_id();
            }
            return source_id;
        }

        std::vector<TypeRef> concrete_args;
        concrete_args.reserve(type_args.size());
        for (const TypeRef& arg : type_args) {
            concrete_args.push_back(substitute_type(arg, substitutions));
        }

        const std::string key = specialization_key("fn:", source_id, concrete_args);
        if (const auto it = function_specializations.find(key); it != function_specializations.end()) {
            return it->second;
        }

        Substitutions instance_substitutions;
        const std::size_t count = std::min(function.generics.size(), concrete_args.size());
        for (std::size_t index = 0; index < count; ++index) {
            instance_substitutions.emplace(function.generics[index], concrete_args[index]);
        }

        const std::string qualified_name = function_specialization_name(function, concrete_args);
        const FunctionId instance_id = add_signature(function, qualified_name, instance_substitutions);
        function_specializations.emplace(key, instance_id);
        if (function.body != nullptr) {
            output.functions[instance_id].body = clone_block(*function.body, instance_substitutions);
        }
        return instance_id;
    }

    [[nodiscard]] TypeRef substitute_type(const TypeRef& type, const Substitutions& substitutions) {
        if (type.flavor == typesys::TypeFlavor::Generic) {
            if (const auto it = substitutions.find(type.text); it != substitutions.end()) {
                return it->second;
            }
        }

        TypeRef substituted = type;
        substituted.args.clear();
        substituted.args.reserve(type.args.size());
        typesys::UseDiscipline discipline = type.discipline;
        for (const TypeRef& arg : type.args) {
            TypeRef substituted_arg = substitute_type(arg, substitutions);
            discipline = typesys::merge_discipline(discipline, substituted_arg.discipline);
            substituted.args.push_back(std::move(substituted_arg));
        }
        if (!substituted.args.empty()) {
            substituted.text = format_type_args(type_base_name(type.text), substituted.args);
        }
        substituted.discipline = discipline;
        if (type.identity.source() == TypeIdentitySource::PackageTypeDeclaration) {
            const TypeId mapped_id = map_type_id(type.identity.package_type_id(), type.args, substitutions);
            substituted.identity = TypeIdentity::from_package_type(mapped_id);
            if (mapped_id < output.types.size()) {
                const TypeDecl& mapped_type = output.types[mapped_id];
                substituted.text = mapped_type.qualified_name;
                substituted.args.clear();
                substituted.flavor = typesys::TypeFlavor::Named;
                if (mapped_type.discipline.state() == TypeDisciplineResolutionState::ConcreteUseDisciplineKnown) {
                    substituted.discipline = mapped_type.discipline.discipline();
                }
            }
        }
        return substituted;
    }

    [[nodiscard]] std::vector<FieldInit> clone_field_inits(const std::vector<FieldInit>& fields,
                                                           const Substitutions& substitutions) {
        std::vector<FieldInit> cloned;
        cloned.reserve(fields.size());
        for (const FieldInit& field : fields) {
            FieldInit copy;
            copy.name = field.name;
            copy.spelling = field.spelling;
            copy.value = clone_expr(*field.value, substitutions);
            cloned.push_back(std::move(copy));
        }
        return cloned;
    }

    [[nodiscard]] std::unique_ptr<Pattern> clone_pattern(const Pattern& pattern,
                                                         const Substitutions& substitutions) {
        switch (pattern.kind) {
        case PatternKind::Variant: {
            const auto& variant = static_cast<const VariantPattern&>(pattern);
            auto cloned = std::make_unique<VariantPattern>();
            cloned->owner_type_id = map_type_id(variant.owner_type_id, {}, substitutions);
            cloned->variant_id = map_variant_id(variant.variant_id);
            cloned->owner_name = cloned->owner_type_id < output.types.size()
                ? output.types[cloned->owner_type_id].qualified_name
                : variant.owner_name;
            cloned->variant_name = variant.variant_name;
            cloned->payload_mode = variant.payload_mode;
            cloned->bindings = variant.bindings;
            return cloned;
        }
        case PatternKind::Succeeded: {
            const auto& succeeded = static_cast<const SucceededPattern&>(pattern);
            auto cloned = std::make_unique<SucceededPattern>();
            cloned->binding_name = succeeded.binding_name;
            cloned->binding = succeeded.binding;
            return cloned;
        }
        case PatternKind::Failed: {
            const auto& failed = static_cast<const FailedPattern&>(pattern);
            auto cloned = std::make_unique<FailedPattern>();
            if (failed.variant != nullptr) {
                cloned->variant = std::unique_ptr<VariantPattern>(
                    static_cast<VariantPattern*>(clone_pattern(*failed.variant, substitutions).release()));
            }
            return cloned;
        }
        }
        return std::make_unique<SucceededPattern>();
    }

    [[nodiscard]] MatchArm clone_match_arm(const MatchArm& arm, const Substitutions& substitutions) {
        MatchArm cloned;
        cloned.pattern = clone_pattern(*arm.pattern, substitutions);
        cloned.body = clone_expr(*arm.body, substitutions);
        return cloned;
    }

    [[nodiscard]] std::unique_ptr<Stmt> clone_stmt(const Stmt& stmt, const Substitutions& substitutions) {
        switch (stmt.kind) {
        case StatementKind::Let: {
            const auto& let = static_cast<const LetStmt&>(stmt);
            auto cloned = std::make_unique<LetStmt>();
            cloned->name = let.name;
            cloned->type = substitute_type(let.type, substitutions);
            cloned->initializer = clone_expr(*let.initializer, substitutions);
            return cloned;
        }
        case StatementKind::Expr: {
            const auto& expr_stmt = static_cast<const ExprStmt&>(stmt);
            auto cloned = std::make_unique<ExprStmt>();
            cloned->expr = clone_expr(*expr_stmt.expr, substitutions);
            return cloned;
        }
        }
        return std::make_unique<ExprStmt>();
    }

    [[nodiscard]] std::unique_ptr<BlockExpr> clone_block(const BlockExpr& block,
                                                        const Substitutions& substitutions) {
        auto cloned = std::make_unique<BlockExpr>(substitute_type(block.result_type, substitutions));
        cloned->failure = map_expr_failure(block.failure, substitutions);
        cloned->statements.reserve(block.statements.size());
        for (const auto& stmt : block.statements) {
            cloned->statements.push_back(clone_stmt(*stmt, substitutions));
        }
        if (block.result != nullptr) {
            cloned->result = clone_expr(*block.result, substitutions);
        }
        return cloned;
    }

    [[nodiscard]] std::unique_ptr<Expr> clone_expr(const Expr& expr, const Substitutions& substitutions) {
        switch (expr.kind) {
        case ExprKind::NumberLiteral: {
            const auto& number = static_cast<const NumberLiteralExpr&>(expr);
            auto cloned = std::make_unique<NumberLiteralExpr>(number.lexeme, substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            return cloned;
        }
        case ExprKind::StringLiteral: {
            const auto& string = static_cast<const StringLiteralExpr&>(expr);
            auto cloned = std::make_unique<StringLiteralExpr>(string.lexeme, substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            return cloned;
        }
        case ExprKind::Unit: {
            auto cloned = std::make_unique<UnitExpr>(substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            return cloned;
        }
        case ExprKind::LocalRef: {
            const auto& local = static_cast<const LocalRefExpr&>(expr);
            auto cloned = std::make_unique<LocalRefExpr>(local.name, substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            return cloned;
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            const FunctionId mapped_id = map_function_id(call.function_id, call.type_args, substitutions);
            auto cloned = std::make_unique<CallExpr>(substitute_type(expr.result_type, substitutions));
            cloned->function_id = mapped_id;
            cloned->callee_name = mapped_id < output.functions.size() ? output.functions[mapped_id].qualified_name
                                                                       : call.callee_name;
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            cloned->args.reserve(call.args.size());
            for (const auto& arg : call.args) {
                cloned->args.push_back(clone_expr(*arg, substitutions));
            }
            return cloned;
        }
        case ExprKind::Construct: {
            const auto& construct = static_cast<const ConstructExpr&>(expr);
            auto cloned = std::make_unique<ConstructExpr>(substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            cloned->construct_kind = construct.construct_kind;
            if (cloned->result_type.identity.source() == TypeIdentitySource::PackageTypeDeclaration) {
                cloned->owner_type_id = cloned->result_type.identity.package_type_id();
            } else {
                cloned->owner_type_id = map_type_id(construct.owner_type_id, {}, substitutions);
            }
            if (construct.variant_target.state() == ConstructVariantTargetState::ConstructsNamedVariant) {
                cloned->variant_target = ConstructVariantTarget::constructs_named_variant(
                    map_variant_id(construct.variant_target.variant_id()));
            }
            if (cloned->variant_target.state() == ConstructVariantTargetState::ConstructsNamedVariant
                && cloned->variant_target.variant_id() < output.variants.size()) {
                cloned->qualified_name = output.variants[cloned->variant_target.variant_id()].qualified_name;
            } else if (cloned->owner_type_id < output.types.size()) {
                cloned->qualified_name = output.types[cloned->owner_type_id].qualified_name;
            } else {
                cloned->qualified_name = construct.qualified_name;
            }
            cloned->fields = clone_field_inits(construct.fields, substitutions);
            return cloned;
        }
        case ExprKind::Try: {
            const auto& try_expr = static_cast<const TryExpr&>(expr);
            auto cloned = std::make_unique<TryExpr>(substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            cloned->operand = clone_expr(*try_expr.operand, substitutions);
            return cloned;
        }
        case ExprKind::Match: {
            const auto& match = static_cast<const MatchExpr&>(expr);
            auto cloned = std::make_unique<MatchExpr>(substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            cloned->scrutinee = clone_expr(*match.scrutinee, substitutions);
            cloned->arms.reserve(match.arms.size());
            for (const MatchArm& arm : match.arms) {
                cloned->arms.push_back(clone_match_arm(arm, substitutions));
            }
            return cloned;
        }
        case ExprKind::Block:
            return clone_block(static_cast<const BlockExpr&>(expr), substitutions);
        case ExprKind::Fail: {
            const auto& fail = static_cast<const FailExpr&>(expr);
            auto cloned = std::make_unique<FailExpr>(substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            cloned->reason_type_id = map_type_id(fail.reason_type_id, {}, substitutions);
            cloned->variant_id = map_variant_id(fail.variant_id);
            cloned->reason_name = cloned->reason_type_id < output.types.size()
                ? output.types[cloned->reason_type_id].qualified_name
                : fail.reason_name;
            cloned->variant_name = cloned->variant_id < output.variants.size()
                ? output.variants[cloned->variant_id].name
                : fail.variant_name;
            cloned->fields = clone_field_inits(fail.fields, substitutions);
            return cloned;
        }
        case ExprKind::Grant: {
            const auto& grant = static_cast<const GrantExpr&>(expr);
            auto cloned = std::make_unique<GrantExpr>(substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            cloned->grant_function_id = map_function_id(grant.grant_function_id, {}, substitutions);
            cloned->permit_type_id = map_type_id(grant.permit_type_id, {}, substitutions);
            cloned->grant_name = cloned->grant_function_id < output.functions.size()
                ? output.functions[cloned->grant_function_id].qualified_name
                : grant.grant_name;
            cloned->permit_name = cloned->permit_type_id < output.types.size()
                ? output.types[cloned->permit_type_id].qualified_name
                : grant.permit_name;
            cloned->binder_name = grant.binder_name;
            cloned->args.reserve(grant.args.size());
            for (const auto& arg : grant.args) {
                cloned->args.push_back(clone_expr(*arg, substitutions));
            }
            cloned->body = clone_block(*grant.body, substitutions);
            return cloned;
        }
        case ExprKind::Prove: {
            const auto& prove = static_cast<const ProveExpr&>(expr);
            auto cloned = std::make_unique<ProveExpr>(substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            cloned->proof_type_id = map_type_id(prove.proof_type_id, {}, substitutions);
            cloned->qualified_name = cloned->proof_type_id < output.types.size()
                ? output.types[cloned->proof_type_id].qualified_name
                : prove.qualified_name;
            cloned->fields = clone_field_inits(prove.fields, substitutions);
            return cloned;
        }
        case ExprKind::FieldAccess: {
            const auto& field = static_cast<const FieldAccessExpr&>(expr);
            auto cloned = std::make_unique<FieldAccessExpr>(substitute_type(expr.result_type, substitutions));
            cloned->failure = map_expr_failure(expr.failure, substitutions);
            cloned->object = clone_expr(*field.object, substitutions);
            cloned->field_name = field.field_name;
            return cloned;
        }
        }
        return std::make_unique<UnitExpr>(TypeRef{});
    }
};

void dump_fields(std::ostream& out, const std::vector<FieldDecl>& fields, std::size_t depth) {
    for (const FieldDecl& field : fields) {
        indent(out, depth);
        out << "field " << field.name << ": " << dump_type_ref(field.type) << '\n';
    }
}

void dump_field_inits(std::ostream& out, const std::vector<FieldInit>& fields, std::size_t depth) {
    for (const FieldInit& field : fields) {
        indent(out, depth);
        out << "init " << field.name;
        if (field.spelling == ast::FieldInitSpelling::ShorthandBinding) {
            out << " [shorthand]";
        }
        out << '\n';
        dump_expr(out, *field.value, depth + 1);
    }
}

void dump_stmt(std::ostream& out, const Stmt& stmt, std::size_t depth) {
    switch (stmt.kind) {
    case StatementKind::Let: {
        const auto& let_stmt = static_cast<const LetStmt&>(stmt);
        indent(out, depth);
        out << "let " << let_stmt.name << ": " << dump_type_ref(let_stmt.type) << '\n';
        dump_expr(out, *let_stmt.initializer, depth + 1);
        break;
    }
    case StatementKind::Expr: {
        const auto& expr_stmt = static_cast<const ExprStmt&>(stmt);
        indent(out, depth);
        out << "expr-stmt\n";
        dump_expr(out, *expr_stmt.expr, depth + 1);
        break;
    }
    }
}

void dump_expr(std::ostream& out, const Expr& expr, std::size_t depth) {
    indent(out, depth);
    switch (expr.kind) {
    case ExprKind::NumberLiteral:
        out << "number " << static_cast<const NumberLiteralExpr&>(expr).lexeme << " : " << dump_type_ref(expr.result_type) << '\n';
        break;
    case ExprKind::StringLiteral:
        out << "string " << static_cast<const StringLiteralExpr&>(expr).lexeme << " : " << dump_type_ref(expr.result_type) << '\n';
        break;
    case ExprKind::Unit:
        out << "unit\n";
        break;
    case ExprKind::LocalRef:
        out << "local " << static_cast<const LocalRefExpr&>(expr).name << " : " << dump_type_ref(expr.result_type) << '\n';
        break;
    case ExprKind::Call: {
        const auto& call = static_cast<const CallExpr&>(expr);
        out << "call " << call.callee_name;
        if (!call.type_args.empty()) {
            out << '<';
            for (std::size_t index = 0; index < call.type_args.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << dump_type_ref(call.type_args[index]);
            }
            out << '>';
        }
        out << " -> " << dump_type_ref(expr.result_type);
        if (expr.failure.effect() == ExprFailureEffect::YieldsReason) {
            out << " fails";
        }
        out << '\n';
        for (const auto& arg : call.args) {
            dump_expr(out, *arg, depth + 1);
        }
        break;
    }
    case ExprKind::Construct: {
        const auto& construct = static_cast<const ConstructExpr&>(expr);
        out << "construct " << construct.qualified_name << " : " << dump_type_ref(expr.result_type) << '\n';
        dump_field_inits(out, construct.fields, depth + 1);
        break;
    }
    case ExprKind::Try: {
        out << "try\n";
        dump_expr(out, *static_cast<const TryExpr&>(expr).operand, depth + 1);
        break;
    }
    case ExprKind::Match: {
        const auto& match = static_cast<const MatchExpr&>(expr);
        out << "match -> " << dump_type_ref(expr.result_type) << '\n';
        indent(out, depth + 1);
        out << "scrutinee\n";
        dump_expr(out, *match.scrutinee, depth + 2);
        for (const MatchArm& arm : match.arms) {
            indent(out, depth + 1);
            out << "arm\n";
            if (arm.pattern) {
                dump_pattern(out, *arm.pattern, depth + 2);
            }
            dump_expr(out, *arm.body, depth + 2);
        }
        break;
    }
    case ExprKind::Block: {
        const auto& block = static_cast<const BlockExpr&>(expr);
        out << "block -> " << dump_type_ref(expr.result_type) << '\n';
        for (const auto& stmt : block.statements) {
            dump_stmt(out, *stmt, depth + 1);
        }
        if (block.result) {
            indent(out, depth + 1);
            out << "result\n";
            dump_expr(out, *block.result, depth + 2);
        }
        break;
    }
    case ExprKind::Fail: {
        const auto& fail = static_cast<const FailExpr&>(expr);
        out << "fail " << fail.reason_name << "::" << fail.variant_name << '\n';
        dump_field_inits(out, fail.fields, depth + 1);
        break;
    }
    case ExprKind::Grant: {
        const auto& grant_expr = static_cast<const GrantExpr&>(expr);
        out << "grant " << grant_expr.grant_name << " as " << grant_expr.binder_name
            << " -> " << dump_type_ref(expr.result_type) << '\n';
        for (const auto& arg : grant_expr.args) {
            dump_expr(out, *arg, depth + 1);
        }
        dump_expr(out, *grant_expr.body, depth + 1);
        break;
    }
    case ExprKind::FieldAccess: {
        const auto& fa = static_cast<const FieldAccessExpr&>(expr);
        out << "field-access ." << fa.field_name << " : " << dump_type_ref(expr.result_type) << '\n';
        dump_expr(out, *fa.object, depth + 1);
        break;
    }
    case ExprKind::Prove: {
        const auto& prove = static_cast<const ProveExpr&>(expr);
        out << "prove " << prove.qualified_name << " : " << dump_type_ref(expr.result_type) << '\n';
        dump_field_inits(out, prove.fields, depth + 1);
        break;
    }
    }
}

void dump_pattern(std::ostream& out, const Pattern& pattern, std::size_t depth) {
    indent(out, depth);
    switch (pattern.kind) {
    case PatternKind::Variant: {
        const auto& variant = static_cast<const VariantPattern&>(pattern);
        out << "pattern " << variant.owner_name << "::" << variant.variant_name << '\n';
        if (variant.payload_mode == VariantPattern::PayloadMode::Ignore) {
            indent(out, depth + 1);
            out << "payload { .. }\n";
        } else {
            for (const Binding& binding : variant.bindings) {
                indent(out, depth + 1);
                out << "bind " << binding.field_name << " -> " << binding.binding_name << '\n';
            }
        }
        break;
    }
    case PatternKind::Succeeded: {
        const auto& succeeded = static_cast<const SucceededPattern&>(pattern);
        out << "pattern succeeded(";
        if (succeeded.binding == ast::SuccessPatternBinding::DiscardedValue) {
            out << '_';
        } else {
            out << succeeded.binding_name;
        }
        out << ")\n";
        break;
    }
    case PatternKind::Failed: {
        out << "pattern failed\n";
        const auto& failed = static_cast<const FailedPattern&>(pattern);
        if (failed.variant) {
            dump_pattern(out, *failed.variant, depth + 1);
        }
        break;
    }
    }
}

} // namespace

std::string type_kind_name(TypeKind kind) {
    switch (kind) {
    case TypeKind::Record:
        return "record";
    case TypeKind::State:
        return "state";
    case TypeKind::Reason:
        return "reason";
    case TypeKind::Proof:
        return "proof";
    case TypeKind::Permit:
        return "permit";
    case TypeKind::Phase:
        return "phase";
    }
    return "type";
}

Package lower(const ast::TranslationUnit& unit) {
    Lowerer lowerer;
    return lowerer.lower(unit);
}

Package monomorphize_for_backend(const Package& package) {
    Monomorphizer monomorphizer(package);
    return monomorphizer.run();
}

const TypeDecl& lookup_type(const Package& package, TypeId id) {
    return package.types.at(id);
}

const VariantDecl& lookup_variant(const Package& package, VariantId id) {
    return package.variants.at(id);
}

const FunctionDecl& lookup_function(const Package& package, FunctionId id) {
    return package.functions.at(id);
}

std::string dump(const Package& package) {
    std::ostringstream out;
    out << "types:\n";
    for (const TypeDecl& type : package.types) {
        out << "  - " << ast::visibility_name(type.visibility) << ' ' << type_kind_name(type.kind)
            << ' ' << type.qualified_name;
        if (!type.generics.empty()) {
            out << '<';
            for (std::size_t index = 0; index < type.generics.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << type.generics[index];
            }
            out << '>';
        }
        if (type.discipline.state() == TypeDisciplineResolutionState::ConcreteUseDisciplineKnown
            && typesys::discipline_movement(type.discipline.discipline()) == typesys::DisciplineMovement::Affine) {
            out << " [affine]";
        }
        out << '\n';
        if (!type.fields.empty()) {
            dump_fields(out, type.fields, 3);
        }
        for (VariantId variant_id : type.variants) {
            const VariantDecl& variant = package.variants[variant_id];
            indent(out, 3);
            out << "variant " << variant.name << '\n';
            dump_fields(out, variant.fields, 4);
        }
    }

    out << "functions:\n";
    for (const FunctionDecl& function : package.functions) {
        out << "  - " << ast::visibility_name(function.visibility) << ' '
            << (function.implementation == ast::FunctionImplementation::ForeignImport ? "foreign-fn " : "fn ")
            << function.qualified_name;
        if (!function.generics.empty()) {
            out << '<';
            for (std::size_t index = 0; index < function.generics.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << function.generics[index];
            }
            out << '>';
        }
        out << '(';
        for (std::size_t index = 0; index < function.params.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            out << function.params[index].name << ": " << dump_type_ref(function.params[index].type);
        }
        out << ") -> " << dump_type_ref(function.return_type);
        if (function.failure.behavior() == FunctionFailureBehavior::YieldsReason) {
            out << " fails " << package.types[function.failure.reason_type_id()].qualified_name;
        }
        if (function.authority.effect() == FunctionAuthorityEffect::GrantsScopedPermit) {
            out << " grants " << package.types[function.authority.permit_type_id()].qualified_name;
        }
        for (TypeId proof_id : function.proves_proof_type_ids) {
            out << " proves " << package.types[proof_id].qualified_name;
        }
        out << '\n';
        if (function.body) {
            dump_expr(out, *function.body, 3);
        }
    }
    return out.str();
}

std::string emit_stub_backend(const Package& package) {
    std::ostringstream out;
    out << "# Evident stub backend output\n\n";
    out << "## Types\n";
    for (const TypeDecl& type : package.types) {
        out << "- " << type_kind_name(type.kind) << ' ' << type.qualified_name << '\n';
    }

    out << "\n## Functions\n";
    for (const FunctionDecl& function : package.functions) {
        out << "- " << function.qualified_name << " -> " << dump_type_ref(function.return_type);
        if (function.failure.behavior() == FunctionFailureBehavior::YieldsReason) {
            out << " fails " << package.types[function.failure.reason_type_id()].qualified_name;
        }
        if (function.authority.effect() == FunctionAuthorityEffect::GrantsScopedPermit) {
            out << " grants " << package.types[function.authority.permit_type_id()].qualified_name;
        }
        for (TypeId proof_id : function.proves_proof_type_ids) {
            out << " proves " << package.types[proof_id].qualified_name;
        }
        if (function.implementation == ast::FunctionImplementation::ForeignImport) {
            out << " [foreign]";
        } else if (function.body) {
            out << " [body]";
        }
        out << '\n';
    }

    out << "\n## Backend Status\n";
    out << "- HIR now carries typed function bodies, local bindings, calls, constructors, and match patterns\n";
    out << "- MIR lowering is available through --dump-mir\n";
    out << "- Native code emission and linking remain future work\n";
    return out.str();
}

} // namespace evident::hir
