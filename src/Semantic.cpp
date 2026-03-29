#include "evident/Semantic.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace evident {

namespace {

struct Scope;

struct Symbol {
    const ast::Decl* decl = nullptr;
    ast::DeclKind kind = ast::DeclKind::Struct;
    ast::Visibility visibility = ast::Visibility::Private;
    std::unique_ptr<Scope> child_scope;
};

struct Scope {
    Scope* parent = nullptr;
    std::unordered_map<std::string, Symbol> symbols;
};

const std::unordered_set<std::string_view> kReservedPublicNames = {
    "Yes",         "No",           "True",       "False",        "Some",
    "None",        "Present",      "Absent",     "Missing",      "Ready",
    "Unavailable", "Connected",    "Disconnected", "Default",      "Other",
    "Unknown",     "Invalid",      "Unset",      "Any",          "All",
    "Unrestricted", "AllowAll",
};

const std::unordered_set<std::string_view> kPseudoOptionalNames = {
    "Present", "Absent", "Missing", "Some", "None",
};

const std::unordered_set<std::string_view> kBuiltins = {
    "Int",   "Nat",   "Float",  "Char",  "Text",    "Bytes",
    "Never", "List",  "List1",  "Map",   "Map1",    "CString",
    "CInt",  "CSize", "Byte",   "Unit",
};

bool is_reserved_public_name(std::string_view name) {
    return name.size() == 1 || kReservedPublicNames.contains(name);
}

bool is_builtin(std::string_view name) {
    return kBuiltins.contains(name);
}

const ast::StateDecl* as_state(const ast::Decl* decl) {
    return decl != nullptr && decl->kind == ast::DeclKind::State
        ? static_cast<const ast::StateDecl*>(decl)
        : nullptr;
}


struct TypeUseRules {
    bool require_public = false;
    bool allow_reason = false;
    bool allow_permit = false;
    std::string_view context;
};

class Analyzer {
public:
    explicit Analyzer(DiagnosticSink& diagnostics)
        : diagnostics_(diagnostics) {}

    void analyze(const ast::TranslationUnit& unit) {
        build_scope(unit.decls, root_, "");
        analyze_scope(unit.decls, root_, true);
    }

private:
    enum class TypeFlavor {
        Error,
        Builtin,
        Generic,
        Named,
    };

    struct Type {
        TypeFlavor flavor = TypeFlavor::Error;
        std::string name;
        const ast::Decl* decl = nullptr;
        std::vector<Type> args;
    };

    struct ExprType {
        Type value;
        const ast::ReasonDecl* yielded_reason = nullptr;
    };

    struct ValueEnv {
        ValueEnv* parent = nullptr;
        std::unordered_map<std::string, Type> values;
    };

    struct FunctionContext {
        const Scope& scope;
        const ast::FunctionDecl& function;
        std::vector<std::string> generics;
        Type return_type;
        const ast::ReasonDecl* yields_reason = nullptr;
    };

    struct VariantResolution {
        const ast::Decl* owner_decl = nullptr;
        const ast::Variant* variant = nullptr;
        bool ambiguous = false;
    };

    DiagnosticSink& diagnostics_;
    Scope root_;
    std::unordered_map<const ast::Decl*, std::string> qualified_names_;

    void build_scope(const std::vector<std::unique_ptr<ast::Decl>>& decls, Scope& scope, const std::string& prefix) {
        for (const auto& decl_ptr : decls) {
            const ast::Decl& decl = *decl_ptr;
            auto [it, inserted] = scope.symbols.try_emplace(decl.name, Symbol{&decl, decl.kind, decl.visibility, nullptr});
            if (!inserted) {
                diagnostics_.error(decl.span, "duplicate declaration for '" + decl.name + "'");
                continue;
            }

            const std::string qualified = prefix.empty() ? decl.name : prefix + "::" + decl.name;
            qualified_names_[&decl] = qualified;

            if (decl.kind == ast::DeclKind::Module) {
                it->second.child_scope = std::make_unique<Scope>();
                it->second.child_scope->parent = &scope;
                const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
                build_scope(module_decl.members, *it->second.child_scope, qualified);
            }
        }
    }

    const Symbol* resolve_symbol(const Scope& current_scope,
                                 const std::vector<std::string>& local_generics,
                                 const std::vector<std::string>& path) const {
        if (path.empty()) {
            return nullptr;
        }

        if (path.size() == 1) {
            const std::string& name = path.front();
            if (std::find(local_generics.begin(), local_generics.end(), name) != local_generics.end() || is_builtin(name)) {
                return nullptr;
            }
            for (const Scope* scope = &current_scope; scope != nullptr; scope = scope->parent) {
                if (const auto it = scope->symbols.find(name); it != scope->symbols.end()) {
                    return &it->second;
                }
            }
            return nullptr;
        }

        const Symbol* symbol = nullptr;
        for (const Scope* scope = &current_scope; scope != nullptr && symbol == nullptr; scope = scope->parent) {
            if (const auto it = scope->symbols.find(path.front()); it != scope->symbols.end()) {
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

    VariantResolution resolve_variant(const Scope& current_scope,
                                      const std::vector<std::string>& path,
                                      bool allow_state,
                                      bool allow_reason) const {
        VariantResolution result;
        if (path.empty()) {
            return result;
        }

        auto try_owner = [&](const ast::Decl* owner, std::string_view variant_name) {
            if (owner == nullptr) {
                return;
            }
            if ((owner->kind == ast::DeclKind::State && !allow_state)
                || (owner->kind == ast::DeclKind::Reason && !allow_reason)) {
                return;
            }

            const auto* variants = owner->kind == ast::DeclKind::State
                ? &static_cast<const ast::StateDecl*>(owner)->variants
                : &static_cast<const ast::ReasonDecl*>(owner)->variants;
            for (const ast::Variant& variant : *variants) {
                if (variant.name == variant_name) {
                    if (result.variant != nullptr && result.owner_decl != owner) {
                        result.ambiguous = true;
                        return;
                    }
                    result.owner_decl = owner;
                    result.variant = &variant;
                    return;
                }
            }
        };

        if (path.size() > 1) {
            std::vector<std::string> owner_path(path.begin(), path.end() - 1);
            if (const Symbol* symbol = resolve_symbol(current_scope, {}, owner_path); symbol != nullptr) {
                try_owner(symbol->decl, path.back());
            }
            return result;
        }

        for (const Scope* scope = &current_scope; scope != nullptr; scope = scope->parent) {
            for (const auto& [_, symbol] : scope->symbols) {
                if ((symbol.kind == ast::DeclKind::State && allow_state)
                    || (symbol.kind == ast::DeclKind::Reason && allow_reason)) {
                    try_owner(symbol.decl, path.front());
                    if (result.ambiguous) {
                        return result;
                    }
                }
            }
        }
        return result;
    }

    static bool is_type_decl_kind(ast::DeclKind kind) {
        switch (kind) {
        case ast::DeclKind::Struct:
        case ast::DeclKind::State:
        case ast::DeclKind::Reason:
        case ast::DeclKind::Proof:
        case ast::DeclKind::Permit:
        case ast::DeclKind::Trait:
            return true;
        default:
            return false;
        }
    }

    Type error_type() const {
        return Type{TypeFlavor::Error, "<error>", nullptr, {}};
    }

    Type builtin_type(std::string name, std::vector<Type> args = {}) const {
        return Type{TypeFlavor::Builtin, std::move(name), nullptr, std::move(args)};
    }

    Type generic_type(std::string name) const {
        return Type{TypeFlavor::Generic, std::move(name), nullptr, {}};
    }

    Type named_type(const ast::Decl* decl, std::vector<Type> args = {}) const {
        auto it = qualified_names_.find(decl);
        const std::string name = it != qualified_names_.end() ? it->second : decl->name;
        return Type{TypeFlavor::Named, name, decl, std::move(args)};
    }

    bool is_error(const Type& type) const {
        return type.flavor == TypeFlavor::Error;
    }

    bool is_never(const Type& type) const {
        return type.flavor == TypeFlavor::Builtin && type.name == "Never" && type.args.empty();
    }

    std::string type_name(const Type& type) const {
        if (type.flavor == TypeFlavor::Error) {
            return "<error>";
        }
        std::ostringstream out;
        out << type.name;
        if (!type.args.empty()) {
            out << '<';
            for (std::size_t index = 0; index < type.args.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << type_name(type.args[index]);
            }
            out << '>';
        }
        return out.str();
    }

    bool types_equal(const Type& lhs, const Type& rhs) const {
        if (is_error(lhs) || is_error(rhs)) {
            return true;
        }
        if (lhs.flavor != rhs.flavor) {
            return false;
        }
        if (lhs.name != rhs.name || lhs.decl != rhs.decl || lhs.args.size() != rhs.args.size()) {
            return false;
        }
        for (std::size_t index = 0; index < lhs.args.size(); ++index) {
            if (!types_equal(lhs.args[index], rhs.args[index])) {
                return false;
            }
        }
        return true;
    }

    bool assignable_to(const Type& target, const Type& actual) const {
        if (is_error(target) || is_error(actual) || is_never(actual)) {
            return true;
        }
        return types_equal(target, actual);
    }

    Type resolve_type(const Scope& scope,
                      const std::vector<std::string>& generics,
                      const ast::TypeRef& type_ref) const {
        if (type_ref.path.empty()) {
            return error_type();
        }

        std::vector<Type> args;
        args.reserve(type_ref.args.size());
        for (const ast::TypeRef& arg : type_ref.args) {
            args.push_back(resolve_type(scope, generics, arg));
        }

        if (type_ref.path.size() == 1) {
            const std::string& name = type_ref.path.front();
            if (std::find(generics.begin(), generics.end(), name) != generics.end()) {
                return generic_type(name);
            }
            if (is_builtin(name)) {
                return builtin_type(name, std::move(args));
            }
        }

        if (const Symbol* symbol = resolve_symbol(scope, generics, type_ref.path); symbol != nullptr) {
            if (is_type_decl_kind(symbol->kind)) {
                return named_type(symbol->decl, std::move(args));
            }
        }
        return error_type();
    }

    void check_public_name(const std::string& name, SourceSpan span) {
        if (is_reserved_public_name(name)) {
            diagnostics_.error(span, "public name '" + name + "' is reserved or too generic");
        }
    }

    void check_duplicate_generic_params(const std::vector<ast::GenericParam>& generics, std::string_view context) {
        std::unordered_set<std::string> seen;
        for (const ast::GenericParam& generic : generics) {
            if (!seen.insert(generic.name).second) {
                diagnostics_.error(generic.span, "duplicate generic parameter '" + generic.name + "' in " + std::string(context));
            }
        }
    }

    void check_duplicate_fields(const std::vector<ast::Field>& fields, std::string_view context) {
        std::unordered_set<std::string> seen;
        for (const ast::Field& field : fields) {
            if (!seen.insert(field.name).second) {
                diagnostics_.error(field.span, "duplicate field '" + field.name + "' in " + std::string(context));
            }
        }
    }

    void check_duplicate_variants(const std::vector<ast::Variant>& variants, std::string_view context) {
        std::unordered_set<std::string> seen;
        for (const ast::Variant& variant : variants) {
            if (!seen.insert(variant.name).second) {
                diagnostics_.error(variant.span, "duplicate variant '" + variant.name + "' in " + std::string(context));
            }
        }
    }

    void check_type_ref_usage(const Scope& scope,
                              const std::vector<std::string>& generics,
                              const ast::TypeRef& type,
                              const TypeUseRules& rules) {
        const bool is_single_generic = type.path.size() == 1
            && std::find(generics.begin(), generics.end(), type.path.front()) != generics.end();

        if (!type.path.empty() && !is_builtin(type.path.front()) && !is_single_generic) {
            const Symbol* symbol = resolve_symbol(scope, generics, type.path);
            if (symbol == nullptr) {
                diagnostics_.error(type.span,
                                  "unknown type '" + ast::format_type(type) + "' in " + std::string(rules.context));
            } else {
                if (!is_type_decl_kind(symbol->kind)) {
                    diagnostics_.error(type.span,
                                      "expected a type in " + std::string(rules.context) + ", found '"
                                          + std::string(ast::decl_kind_name(symbol->kind)) + "'");
                } else {
                    if (rules.require_public && symbol->visibility != ast::Visibility::Public) {
                        diagnostics_.error(type.span,
                                          "public API cannot reference private type '" + ast::format_type(type) + "'");
                    }
                    if (!rules.allow_reason && symbol->kind == ast::DeclKind::Reason) {
                        diagnostics_.error(type.span,
                                          "reason type '" + ast::format_type(type) + "' may not appear in "
                                              + std::string(rules.context));
                    }
                    if (!rules.allow_permit && symbol->kind == ast::DeclKind::Permit) {
                        diagnostics_.error(type.span,
                                          "permit type '" + ast::format_type(type) + "' may not appear in "
                                              + std::string(rules.context));
                    }
                }
            }
        }

        for (const ast::TypeRef& arg : type.args) {
            check_type_ref_usage(scope, generics, arg, rules);
        }
    }

    void check_function_signature(const Scope& scope,
                                  const ast::FunctionSignature& signature,
                                  bool require_public,
                                  bool is_foreign,
                                  const std::vector<std::string>& outer_generics) {
        std::vector<std::string> generics = outer_generics;
        check_duplicate_generic_params(signature.generic_params, "function signature");
        for (const ast::GenericParam& generic : signature.generic_params) {
            generics.push_back(generic.name);
        }

        std::unordered_set<std::string> param_names;
        for (const ast::Parameter& param : signature.params) {
            if (!param_names.insert(param.name).second) {
                diagnostics_.error(param.span, "duplicate parameter '" + param.name + "'");
            }
            check_type_ref_usage(scope,
                                 generics,
                                 param.type,
                                 TypeUseRules{require_public, false, true, "function parameter"});
        }

        check_type_ref_usage(scope,
                             generics,
                             signature.return_type,
                             TypeUseRules{require_public, false, false, "function return type"});

        if (signature.yields_type.has_value()) {
            check_type_ref_usage(scope,
                                 generics,
                                 *signature.yields_type,
                                 TypeUseRules{require_public, true, false, "yield reason"});
            if (const Symbol* symbol = resolve_symbol(scope, generics, signature.yields_type->path); symbol != nullptr) {
                if (symbol->kind != ast::DeclKind::Reason) {
                    diagnostics_.error(signature.yields_type->span,
                                      "'yields' must reference a reason type, not '"
                                          + std::string(ast::decl_kind_name(symbol->kind)) + "'");
                }
            }
            if (is_foreign) {
                diagnostics_.error(signature.yields_type->span, "foreign functions may not use 'yields'");
            }
        }
    }

    void analyze_scope(const std::vector<std::unique_ptr<ast::Decl>>& decls,
                       const Scope& scope,
                       bool enclosing_public) {
        for (const auto& decl_ptr : decls) {
            const ast::Decl& decl = *decl_ptr;
            const bool effectively_public = enclosing_public && decl.visibility == ast::Visibility::Public;

            if (effectively_public) {
                check_public_name(decl.name, decl.span);
            }

            switch (decl.kind) {
            case ast::DeclKind::Module: {
                const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
                const auto it = scope.symbols.find(module_decl.name);
                if (it != scope.symbols.end() && it->second.child_scope != nullptr) {
                    analyze_scope(module_decl.members, *it->second.child_scope, effectively_public);
                }
                break;
            }
            case ast::DeclKind::Struct: {
                const auto& struct_decl = static_cast<const ast::StructDecl&>(decl);
                check_duplicate_generic_params(struct_decl.generic_params, "struct");
                check_duplicate_fields(struct_decl.fields, "struct");
                std::vector<std::string> generics;
                for (const ast::GenericParam& generic : struct_decl.generic_params) {
                    generics.push_back(generic.name);
                }
                for (const ast::Field& field : struct_decl.fields) {
                    check_type_ref_usage(scope,
                                         generics,
                                         field.type,
                                         TypeUseRules{effectively_public, false, false, "struct field"});
                }
                break;
            }
            case ast::DeclKind::State: {
                const auto& state_decl = static_cast<const ast::StateDecl&>(decl);
                if (state_decl.variants.empty()) {
                    diagnostics_.error(state_decl.span, "state declarations must define at least one variant");
                }
                if (state_decl.variants.size() == 2) {
                    const bool first_pseudo = kPseudoOptionalNames.contains(state_decl.variants[0].name);
                    const bool second_pseudo = kPseudoOptionalNames.contains(state_decl.variants[1].name);
                    if (first_pseudo || second_pseudo) {
                        diagnostics_.error(state_decl.span,
                                          "pseudo-optional state detected; model inhabited domain alternatives instead");
                    }
                }
                check_duplicate_generic_params(state_decl.generic_params, "state");
                check_duplicate_variants(state_decl.variants, "state");
                std::vector<std::string> generics;
                for (const ast::GenericParam& generic : state_decl.generic_params) {
                    generics.push_back(generic.name);
                }
                for (const ast::Variant& variant : state_decl.variants) {
                    if (effectively_public) {
                        check_public_name(variant.name, variant.span);
                    }
                    check_duplicate_fields(variant.fields, "state variant");
                    for (const ast::Field& field : variant.fields) {
                        check_type_ref_usage(scope,
                                             generics,
                                             field.type,
                                             TypeUseRules{effectively_public, false, false, "state variant payload"});
                    }
                }
                break;
            }
            case ast::DeclKind::Reason: {
                const auto& reason_decl = static_cast<const ast::ReasonDecl&>(decl);
                if (reason_decl.variants.empty()) {
                    diagnostics_.error(reason_decl.span, "reason declarations must define at least one variant");
                }
                check_duplicate_variants(reason_decl.variants, "reason");
                for (const ast::Variant& variant : reason_decl.variants) {
                    if (effectively_public) {
                        check_public_name(variant.name, variant.span);
                    }
                    check_duplicate_fields(variant.fields, "reason variant");
                    for (const ast::Field& field : variant.fields) {
                        check_type_ref_usage(scope,
                                             {},
                                             field.type,
                                             TypeUseRules{effectively_public, false, false, "reason payload"});
                    }
                }
                break;
            }
            case ast::DeclKind::Proof: {
                const auto& proof_decl = static_cast<const ast::ProofDecl&>(decl);
                check_duplicate_fields(proof_decl.fields, "proof");
                for (const ast::Field& field : proof_decl.fields) {
                    check_type_ref_usage(scope,
                                         {},
                                         field.type,
                                         TypeUseRules{effectively_public, false, false, "proof field"});
                }
                break;
            }
            case ast::DeclKind::Permit:
                break;
            case ast::DeclKind::Trait: {
                const auto& trait_decl = static_cast<const ast::TraitDecl&>(decl);
                check_duplicate_generic_params(trait_decl.generic_params, "trait");
                std::vector<std::string> generics;
                for (const ast::GenericParam& generic : trait_decl.generic_params) {
                    generics.push_back(generic.name);
                }
                std::unordered_set<std::string> method_names;
                for (const ast::FunctionSignature& method : trait_decl.methods) {
                    if (!method_names.insert(method.name).second) {
                        diagnostics_.error(method.span, "duplicate trait method '" + method.name + "'");
                    }
                    check_function_signature(scope, method, effectively_public, false, generics);
                }
                break;
            }
            case ast::DeclKind::Function:
            case ast::DeclKind::ForeignFunction: {
                const auto& function_decl = static_cast<const ast::FunctionDecl&>(decl);
                check_function_signature(scope,
                                         function_decl.signature,
                                         effectively_public,
                                         function_decl.is_foreign(),
                                         {});
                if (function_decl.is_foreign() && function_decl.body != nullptr) {
                    diagnostics_.error(function_decl.body->span, "foreign functions may not define a body");
                }
                if (function_decl.body != nullptr && !function_decl.is_foreign()) {
                    analyze_function_body(function_decl, scope);
                }
                break;
            }
            }
        }
    }

    const Type* lookup_binding(const ValueEnv& env, const std::string& name) const {
        for (const ValueEnv* current = &env; current != nullptr; current = current->parent) {
            if (const auto it = current->values.find(name); it != current->values.end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    void bind_value(ValueEnv& env, const std::string& name, Type type, SourceSpan span) {
        if (!env.values.emplace(name, std::move(type)).second) {
            diagnostics_.error(span, "duplicate local binding '" + name + "'");
        }
    }

    const ast::FunctionDecl* resolve_function(const Scope& scope, const std::vector<std::string>& path) const {
        if (const Symbol* symbol = resolve_symbol(scope, {}, path); symbol != nullptr) {
            if (symbol->kind == ast::DeclKind::Function || symbol->kind == ast::DeclKind::ForeignFunction) {
                return static_cast<const ast::FunctionDecl*>(symbol->decl);
            }
        }
        return nullptr;
    }

    bool check_initializer_fields(const std::vector<ast::Field>& expected_fields,
                                  const std::vector<ast::RecordFieldInit>& actual_fields,
                                  const FunctionContext& context,
                                  ValueEnv& env) {
        bool ok = true;
        std::unordered_map<std::string, const ast::Field*> expected;
        for (const ast::Field& field : expected_fields) {
            expected.emplace(field.name, &field);
        }

        std::unordered_set<std::string> seen;
        for (const ast::RecordFieldInit& init : actual_fields) {
            if (!seen.insert(init.name).second) {
                diagnostics_.error(init.span, "duplicate initializer for field '" + init.name + "'");
                ok = false;
                continue;
            }
            const auto it = expected.find(init.name);
            if (it == expected.end()) {
                diagnostics_.error(init.span, "unknown field '" + init.name + "' in initializer");
                ok = false;
                continue;
            }
            ExprType init_type = type_expr(*init.value, context, env);
            if (init_type.yielded_reason != nullptr) {
                diagnostics_.error(init.span, "yielded call must be handled with 'try' or 'match'");
                ok = false;
            }
            const Type expected_type = resolve_type(context.scope, context.generics, it->second->type);
            if (!assignable_to(expected_type, init_type.value)) {
                diagnostics_.error(init.span,
                                  "initializer for field '" + init.name + "' has type '" + type_name(init_type.value)
                                      + "', expected '" + type_name(expected_type) + "'");
                ok = false;
            }
        }

        for (const ast::Field& field : expected_fields) {
            if (!seen.contains(field.name)) {
                diagnostics_.error(field.span, "missing initializer for field '" + field.name + "'");
                ok = false;
            }
        }
        return ok;
    }

    ExprType type_path_expr(const ast::PathExpr& expr, const FunctionContext& context, ValueEnv& env) {
        if (expr.path.size() == 1) {
            if (const Type* binding = lookup_binding(env, expr.path.front()); binding != nullptr) {
                return ExprType{*binding, nullptr};
            }
        }

        VariantResolution state_variant = resolve_variant(context.scope, expr.path, true, false);
        if (state_variant.ambiguous) {
            diagnostics_.error(expr.span, "ambiguous variant reference '" + expr.path.back() + "'");
            return ExprType{error_type(), nullptr};
        }
        if (state_variant.variant != nullptr) {
            if (!state_variant.variant->fields.empty()) {
                diagnostics_.error(expr.span,
                                  "variant '" + state_variant.variant->name + "' requires payload construction");
                return ExprType{error_type(), nullptr};
            }
            return ExprType{named_type(state_variant.owner_decl), nullptr};
        }

        VariantResolution reason_variant = resolve_variant(context.scope, expr.path, false, true);
        if (reason_variant.ambiguous) {
            diagnostics_.error(expr.span, "ambiguous reason variant reference '" + expr.path.back() + "'");
            return ExprType{error_type(), nullptr};
        }
        if (reason_variant.variant != nullptr) {
            diagnostics_.error(expr.span, "reason variants may appear only in 'fail' or failed(...) patterns");
            return ExprType{error_type(), nullptr};
        }

        if (const ast::FunctionDecl* function = resolve_function(context.scope, expr.path); function != nullptr) {
            diagnostics_.error(expr.span, "function values are not first-class; call '" + function->name + "' with '(...)'");
            return ExprType{error_type(), nullptr};
        }

        if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
            if (symbol->kind == ast::DeclKind::Proof) {
                const auto* proof_decl = static_cast<const ast::ProofDecl*>(symbol->decl);
                if (proof_decl->fields.empty()) {
                    return ExprType{named_type(symbol->decl), nullptr};
                }
                diagnostics_.error(expr.span, "proof '" + proof_decl->name + "' requires field initialization");
                return ExprType{error_type(), nullptr};
            }
            if (symbol->kind == ast::DeclKind::Struct) {
                const auto* struct_decl = static_cast<const ast::StructDecl*>(symbol->decl);
                if (struct_decl->fields.empty()) {
                    return ExprType{named_type(symbol->decl), nullptr};
                }
                diagnostics_.error(expr.span, "struct '" + struct_decl->name + "' requires field initialization");
                return ExprType{error_type(), nullptr};
            }
            diagnostics_.error(expr.span, "type name '" + expr.path.back() + "' is not a value");
            return ExprType{error_type(), nullptr};
        }

        diagnostics_.error(expr.span, "unknown value '" + expr.path.back() + "'");
        return ExprType{error_type(), nullptr};
    }

    ExprType type_call_expr(const ast::CallExpr& expr, const FunctionContext& context, ValueEnv& env) {
        const auto* callee_path = dynamic_cast<const ast::PathExpr*>(expr.callee.get());
        if (callee_path == nullptr) {
            diagnostics_.error(expr.span, "callee must be a named function");
            return ExprType{error_type(), nullptr};
        }

        const ast::FunctionDecl* function = resolve_function(context.scope, callee_path->path);
        if (function == nullptr) {
            diagnostics_.error(expr.span, "unknown function '" + (callee_path->path.empty() ? std::string{} : callee_path->path.back()) + "'");
            return ExprType{error_type(), nullptr};
        }
        if (!function->signature.generic_params.empty()) {
            diagnostics_.error(expr.span, "calls to generic functions are not supported yet");
            return ExprType{error_type(), nullptr};
        }
        if (function->signature.params.size() != expr.args.size()) {
            diagnostics_.error(expr.span,
                              "function '" + function->name + "' expects "
                                  + std::to_string(function->signature.params.size()) + " argument(s), got "
                                  + std::to_string(expr.args.size()));
        }

        const std::size_t arg_count = std::min(function->signature.params.size(), expr.args.size());
        for (std::size_t index = 0; index < arg_count; ++index) {
            ExprType arg_type = type_expr(*expr.args[index], context, env);
            if (arg_type.yielded_reason != nullptr) {
                diagnostics_.error(expr.args[index]->span, "yielded call must be handled with 'try' or 'match'");
            }
            const Type param_type = resolve_type(context.scope, {}, function->signature.params[index].type);
            if (!assignable_to(param_type, arg_type.value)) {
                diagnostics_.error(expr.args[index]->span,
                                  "argument " + std::to_string(index + 1) + " to '" + function->name
                                      + "' has type '" + type_name(arg_type.value) + "', expected '"
                                      + type_name(param_type) + "'");
            }
        }

        ExprType result;
        result.value = resolve_type(context.scope, {}, function->signature.return_type);
        if (function->signature.yields_type.has_value()) {
            const Type yields_type = resolve_type(context.scope, {}, *function->signature.yields_type);
            result.yielded_reason = yields_type.decl != nullptr ? static_cast<const ast::ReasonDecl*>(yields_type.decl) : nullptr;
        }
        return result;
    }

    ExprType type_construct_expr(const ast::ConstructExpr& expr, const FunctionContext& context, ValueEnv& env) {
        if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
            switch (symbol->kind) {
            case ast::DeclKind::Struct: {
                const auto* struct_decl = static_cast<const ast::StructDecl*>(symbol->decl);
                check_initializer_fields(struct_decl->fields, expr.fields, context, env);
                return ExprType{named_type(symbol->decl), nullptr};
            }
            case ast::DeclKind::Proof: {
                const auto* proof_decl = static_cast<const ast::ProofDecl*>(symbol->decl);
                check_initializer_fields(proof_decl->fields, expr.fields, context, env);
                return ExprType{named_type(symbol->decl), nullptr};
            }
            case ast::DeclKind::Permit:
                diagnostics_.error(expr.span, "permit values may not be constructed directly");
                return ExprType{error_type(), nullptr};
            case ast::DeclKind::Reason:
                diagnostics_.error(expr.span, "reason values may not be constructed directly; use 'fail'");
                return ExprType{error_type(), nullptr};
            default:
                break;
            }
        }

        VariantResolution state_variant = resolve_variant(context.scope, expr.path, true, false);
        if (state_variant.ambiguous) {
            diagnostics_.error(expr.span, "ambiguous variant constructor '" + expr.path.back() + "'");
            return ExprType{error_type(), nullptr};
        }
        if (state_variant.variant != nullptr) {
            check_initializer_fields(state_variant.variant->fields, expr.fields, context, env);
            return ExprType{named_type(state_variant.owner_decl), nullptr};
        }

        VariantResolution reason_variant = resolve_variant(context.scope, expr.path, false, true);
        if (reason_variant.variant != nullptr) {
            diagnostics_.error(expr.span, "reason values may not be constructed directly; use 'fail'");
            return ExprType{error_type(), nullptr};
        }

        diagnostics_.error(expr.span, "unknown constructor '" + expr.path.back() + "'");
        return ExprType{error_type(), nullptr};
    }

    ExprType type_try_expr(const ast::TryExpr& expr, const FunctionContext& context, ValueEnv& env) {
        ExprType operand = type_expr(*expr.operand, context, env);
        if (operand.yielded_reason == nullptr) {
            diagnostics_.error(expr.span, "'try' requires a yielded call");
            return ExprType{error_type(), nullptr};
        }
        if (context.yields_reason == nullptr) {
            diagnostics_.error(expr.span, "'try' is only valid inside a function with 'yields'");
            return ExprType{error_type(), nullptr};
        }
        if (operand.yielded_reason != context.yields_reason) {
            diagnostics_.error(expr.span,
                              "'try' expects yielded reason '" + context.yields_reason->name + "', got '"
                                  + operand.yielded_reason->name + "'");
            return ExprType{error_type(), nullptr};
        }
        return ExprType{operand.value, nullptr};
    }

    ExprType type_fail_expr(const ast::FailExpr& expr, const FunctionContext& context, ValueEnv& env) {
        if (context.yields_reason == nullptr) {
            diagnostics_.error(expr.span, "'fail' is only valid inside a function with 'yields'");
            return ExprType{error_type(), nullptr};
        }

        VariantResolution variant = resolve_variant(context.scope, expr.path, false, true);
        if (variant.ambiguous) {
            diagnostics_.error(expr.span, "ambiguous reason variant '" + expr.path.back() + "'");
            return ExprType{error_type(), nullptr};
        }
        if (variant.variant == nullptr || variant.owner_decl == nullptr) {
            diagnostics_.error(expr.span, "unknown reason variant '" + expr.path.back() + "'");
            return ExprType{error_type(), nullptr};
        }
        if (variant.owner_decl != context.yields_reason) {
            diagnostics_.error(expr.span,
                              "'fail' must use a variant of yielded reason '" + context.yields_reason->name + "'");
        }
        check_initializer_fields(variant.variant->fields, expr.fields, context, env);
        return ExprType{builtin_type("Never"), nullptr};
    }

    Type unify_match_result(const Type& current, const Type& next, SourceSpan span) {
        if (is_error(current) || is_error(next)) {
            return error_type();
        }
        if (is_never(current)) {
            return next;
        }
        if (is_never(next)) {
            return current;
        }
        if (!types_equal(current, next)) {
            diagnostics_.error(span,
                              "match arms have incompatible types '" + type_name(current) + "' and '"
                                  + type_name(next) + "'");
            return error_type();
        }
        return current;
    }

    void bind_variant_pattern(const ast::VariantPattern& pattern,
                              const ast::Variant& variant,
                              const FunctionContext& context,
                              ValueEnv& env) {
        if (pattern.path.size() == 1 && pattern.path.front() == "_") {
            diagnostics_.error(pattern.span, "wildcard patterns are not allowed");
            return;
        }

        if (variant.fields.empty()) {
            if (pattern.payload_mode != ast::VariantPattern::PayloadMode::None) {
                diagnostics_.error(pattern.span, "payload pattern used for variant without payload");
            }
            return;
        }

        if (pattern.payload_mode == ast::VariantPattern::PayloadMode::None) {
            diagnostics_.error(pattern.span, "payload-bearing variants must bind fields or use '{ .. }'");
            return;
        }
        if (pattern.payload_mode == ast::VariantPattern::PayloadMode::Ignore) {
            return;
        }

        std::unordered_map<std::string, const ast::Field*> expected;
        for (const ast::Field& field : variant.fields) {
            expected.emplace(field.name, &field);
        }
        if (pattern.bindings.size() != variant.fields.size()) {
            diagnostics_.error(pattern.span, "payload pattern must bind every field or use '{ .. }'");
        }

        std::unordered_set<std::string> seen_fields;
        for (const auto& binding : pattern.bindings) {
            if (binding.binding_name == "_") {
                diagnostics_.error(binding.span, "use '{ .. }' to ignore payloads; '_' is not a field binding");
                continue;
            }
            if (!seen_fields.insert(binding.field_name).second) {
                diagnostics_.error(binding.span, "duplicate field binding '" + binding.field_name + "'");
                continue;
            }
            const auto it = expected.find(binding.field_name);
            if (it == expected.end()) {
                diagnostics_.error(binding.span, "unknown field '" + binding.field_name + "' in pattern");
                continue;
            }
            const Type field_type = resolve_type(context.scope, context.generics, it->second->type);
            bind_value(env, binding.binding_name, field_type, binding.span);
        }
    }

    ExprType type_match_expr(const ast::MatchExpr& expr, const FunctionContext& context, ValueEnv& env) {
        ExprType scrutinee = type_expr(*expr.scrutinee, context, env);
        Type result_type = builtin_type("Never");
        bool has_arm = false;

        if (scrutinee.yielded_reason != nullptr) {
            bool seen_success = false;
            std::unordered_set<std::string> seen_failed;
            for (const ast::MatchArm& arm : expr.arms) {
                ValueEnv arm_env{&env, {}};
                switch (arm.pattern->kind) {
                case ast::PatternKind::Succeeded: {
                    const auto& pattern = static_cast<const ast::SucceededPattern&>(*arm.pattern);
                    if (seen_success) {
                        diagnostics_.error(arm.pattern->span, "duplicate succeeded(...) arm");
                    }
                    seen_success = true;
                    if (!pattern.ignore && pattern.binding_name.has_value()) {
                        bind_value(arm_env, *pattern.binding_name, scrutinee.value, arm.pattern->span);
                    }
                    break;
                }
                case ast::PatternKind::Failed: {
                    const auto& pattern = static_cast<const ast::FailedPattern&>(*arm.pattern);
                    if (pattern.variant->path.size() == 1 && pattern.variant->path.front() == "_") {
                        diagnostics_.error(pattern.span, "wildcard patterns are not allowed");
                        break;
                    }
                    VariantResolution failure = resolve_variant(context.scope, pattern.variant->path, false, true);
                    if (failure.ambiguous) {
                        diagnostics_.error(pattern.span, "ambiguous failed(...) pattern");
                    } else if (failure.variant == nullptr || failure.owner_decl != scrutinee.yielded_reason) {
                        diagnostics_.error(pattern.span,
                                          "failed(...) arm must match a variant of reason '"
                                              + scrutinee.yielded_reason->name + "'");
                    } else {
                        if (!seen_failed.insert(failure.variant->name).second) {
                            diagnostics_.error(pattern.span,
                                              "duplicate failed arm for variant '" + failure.variant->name + "'");
                        }
                        bind_variant_pattern(*pattern.variant, *failure.variant, context, arm_env);
                    }
                    break;
                }
                case ast::PatternKind::Variant:
                    diagnostics_.error(arm.pattern->span,
                                      "match over a yielded call requires succeeded(...) and failed(...) patterns");
                    break;
                }

                ExprType arm_type = type_expr(*arm.body, context, arm_env);
                if (arm_type.yielded_reason != nullptr) {
                    diagnostics_.error(arm.body->span, "yielded call must be handled with 'try' or 'match'");
                }
                result_type = unify_match_result(result_type, arm_type.value, arm.span);
                has_arm = true;
            }

            if (!seen_success) {
                diagnostics_.error(expr.span, "match over yielded call must include a succeeded(...) arm");
            }
            for (const ast::Variant& variant : scrutinee.yielded_reason->variants) {
                if (!seen_failed.contains(variant.name)) {
                    diagnostics_.error(expr.span,
                                      "non-exhaustive failed(...) coverage; missing '" + variant.name + "'");
                }
            }
            return ExprType{has_arm ? result_type : builtin_type("Unit"), nullptr};
        }

        const ast::StateDecl* state_decl = as_state(scrutinee.value.decl);
        if (state_decl == nullptr) {
            diagnostics_.error(expr.scrutinee->span, "match requires a state value or yielded call");
            return ExprType{error_type(), nullptr};
        }

        std::unordered_set<std::string> seen_variants;
        for (const ast::MatchArm& arm : expr.arms) {
            if (arm.pattern->kind != ast::PatternKind::Variant) {
                diagnostics_.error(arm.pattern->span, "match over a state requires variant patterns");
                continue;
            }

            const auto& pattern = static_cast<const ast::VariantPattern&>(*arm.pattern);
            if (pattern.path.size() == 1 && pattern.path.front() == "_") {
                diagnostics_.error(pattern.span, "wildcard patterns are not allowed");
                continue;
            }
            VariantResolution resolution = resolve_variant(context.scope, pattern.path, true, false);
            if (resolution.ambiguous) {
                diagnostics_.error(pattern.span, "ambiguous variant pattern '" + pattern.path.back() + "'");
                continue;
            }
            if (resolution.variant == nullptr || resolution.owner_decl != state_decl) {
                diagnostics_.error(pattern.span,
                                  "pattern must match a variant of state '" + state_decl->name + "'");
                continue;
            }
            if (!seen_variants.insert(resolution.variant->name).second) {
                diagnostics_.error(pattern.span,
                                  "duplicate match arm for variant '" + resolution.variant->name + "'");
            }

            ValueEnv arm_env{&env, {}};
            bind_variant_pattern(pattern, *resolution.variant, context, arm_env);
            ExprType arm_type = type_expr(*arm.body, context, arm_env);
            if (arm_type.yielded_reason != nullptr) {
                diagnostics_.error(arm.body->span, "yielded call must be handled with 'try' or 'match'");
            }
            result_type = unify_match_result(result_type, arm_type.value, arm.span);
            has_arm = true;
        }

        for (const ast::Variant& variant : state_decl->variants) {
            if (!seen_variants.contains(variant.name)) {
                diagnostics_.error(expr.span,
                                  "non-exhaustive match over state '" + state_decl->name + "'; missing '"
                                      + variant.name + "'");
            }
        }
        return ExprType{has_arm ? result_type : builtin_type("Unit"), nullptr};
    }

    ExprType type_block(const ast::BlockExpr& block, const FunctionContext& context, ValueEnv& parent_env) {
        ValueEnv local{&parent_env, {}};
        for (const auto& stmt_ptr : block.statements) {
            const ast::Stmt& stmt = *stmt_ptr;
            switch (stmt.kind) {
            case ast::StmtKind::Let: {
                const auto& let_stmt = static_cast<const ast::LetStmt&>(stmt);
                ExprType init_type = type_expr(*let_stmt.initializer, context, local);
                if (init_type.yielded_reason != nullptr) {
                    diagnostics_.error(let_stmt.initializer->span, "yielded call must be handled with 'try' or 'match'");
                }
                bind_value(local, let_stmt.name, init_type.value, let_stmt.span);
                break;
            }
            case ast::StmtKind::Expr: {
                const auto& expr_stmt = static_cast<const ast::ExprStmt&>(stmt);
                ExprType expr_type = type_expr(*expr_stmt.expr, context, local);
                if (expr_type.yielded_reason != nullptr) {
                    diagnostics_.error(expr_stmt.expr->span, "yielded call must be handled with 'try' or 'match'");
                }
                break;
            }
            }
        }

        if (block.result != nullptr) {
            ExprType result = type_expr(*block.result, context, local);
            if (result.yielded_reason != nullptr) {
                diagnostics_.error(block.result->span, "yielded call must be handled with 'try' or 'match'");
                return ExprType{error_type(), nullptr};
            }
            return result;
        }
        return ExprType{builtin_type("Unit"), nullptr};
    }

    ExprType type_expr(const ast::Expr& expr, const FunctionContext& context, ValueEnv& env) {
        switch (expr.kind) {
        case ast::ExprKind::NumberLiteral:
            return ExprType{builtin_type("Int"), nullptr};
        case ast::ExprKind::StringLiteral:
            return ExprType{builtin_type("Text"), nullptr};
        case ast::ExprKind::Path:
            return type_path_expr(static_cast<const ast::PathExpr&>(expr), context, env);
        case ast::ExprKind::Call:
            return type_call_expr(static_cast<const ast::CallExpr&>(expr), context, env);
        case ast::ExprKind::Construct:
            return type_construct_expr(static_cast<const ast::ConstructExpr&>(expr), context, env);
        case ast::ExprKind::Try:
            return type_try_expr(static_cast<const ast::TryExpr&>(expr), context, env);
        case ast::ExprKind::Match:
            return type_match_expr(static_cast<const ast::MatchExpr&>(expr), context, env);
        case ast::ExprKind::Block:
            return type_block(static_cast<const ast::BlockExpr&>(expr), context, env);
        case ast::ExprKind::Fail:
            return type_fail_expr(static_cast<const ast::FailExpr&>(expr), context, env);
        }
        return ExprType{error_type(), nullptr};
    }

    void analyze_function_body(const ast::FunctionDecl& function_decl, const Scope& scope) {
        FunctionContext context{scope, function_decl, {}, resolve_type(scope, {}, function_decl.signature.return_type), nullptr};
        for (const ast::GenericParam& generic : function_decl.signature.generic_params) {
            context.generics.push_back(generic.name);
        }
        if (function_decl.signature.yields_type.has_value()) {
            const Type yields_type = resolve_type(scope, context.generics, *function_decl.signature.yields_type);
            context.yields_reason = yields_type.decl != nullptr ? static_cast<const ast::ReasonDecl*>(yields_type.decl) : nullptr;
        }

        ValueEnv env{nullptr, {}};
        for (const ast::Parameter& param : function_decl.signature.params) {
            bind_value(env, param.name, resolve_type(scope, context.generics, param.type), param.span);
        }

        ExprType body_type = type_block(*function_decl.body, context, env);
        if (!assignable_to(context.return_type, body_type.value)) {
            diagnostics_.error(function_decl.body->span,
                              "function '" + function_decl.name + "' returns '" + type_name(body_type.value)
                                  + "' but signature requires '" + type_name(context.return_type) + "'");
        }
    }
};

} // namespace

SemanticAnalyzer::SemanticAnalyzer(DiagnosticSink& diagnostics)
    : diagnostics_(diagnostics) {}

void SemanticAnalyzer::analyze(const ast::TranslationUnit& unit) {
    Analyzer analyzer(diagnostics_);
    analyzer.analyze(unit);
}

} // namespace evident
