#include "evident/Hir.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace evident::hir {

namespace {

const std::unordered_set<std::string_view> kBuiltins = {
    "Int",   "Nat",   "Float",  "Char",  "Text",    "Bytes",
    "Never", "List",  "List1",  "Map",   "Map1",    "CString",
    "CInt",  "CSize", "Byte",   "Unit",
};

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

struct ValueEnv {
    ValueEnv* parent = nullptr;
    std::unordered_map<std::string, TypeRef> values;
};

struct VariantResolution {
    const ast::Decl* owner_decl = nullptr;
    VariantId variant_id = 0;
    bool ambiguous = false;
};

struct FunctionContext {
    const Scope& scope;
    const FunctionDecl& function;
    std::vector<std::string> generics;
};

bool is_builtin(std::string_view name) {
    return kBuiltins.contains(name);
}

TypeKind type_kind_from_decl(ast::DeclKind kind) {
    switch (kind) {
    case ast::DeclKind::Struct:
        return TypeKind::Struct;
    case ast::DeclKind::State:
        return TypeKind::State;
    case ast::DeclKind::Reason:
        return TypeKind::Reason;
    case ast::DeclKind::Proof:
        return TypeKind::Proof;
    case ast::DeclKind::Permit:
        return TypeKind::Permit;
    case ast::DeclKind::Trait:
        return TypeKind::Trait;
    default:
        return TypeKind::Struct;
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

void indent(std::ostream& out, std::size_t depth) {
    for (std::size_t index = 0; index < depth; ++index) {
        out << "  ";
    }
}

struct Lowerer {
    Package package;
    Scope root;
    std::unordered_map<const ast::Decl*, std::string> qualified_names;
    std::unordered_map<const ast::Decl*, TypeId> type_ids;
    std::unordered_map<const ast::Decl*, FunctionId> function_ids;

    [[nodiscard]] Package lower(const ast::TranslationUnit& unit);

private:
    void declare_decls(const std::vector<std::unique_ptr<ast::Decl>>& decls,
                       Scope& scope,
                       const std::string& prefix);
    void populate_types(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope);
    void populate_functions(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope);
    void lower_function_bodies(const std::vector<std::unique_ptr<ast::Decl>>& decls, const Scope& scope);
    [[nodiscard]] VariantId add_variant(TypeId owner_type_id, const ast::Variant& variant, const Scope& scope);
    [[nodiscard]] const Symbol* resolve_symbol(const Scope& scope,
                                               const std::vector<std::string>& generics,
                                               const std::vector<std::string>& path) const;
    [[nodiscard]] const FunctionDecl* resolve_function(const Scope& scope,
                                                       const std::vector<std::string>& path) const;
    [[nodiscard]] VariantResolution resolve_variant(const Scope& scope,
                                                    const std::vector<std::string>& path,
                                                    bool allow_state,
                                                    bool allow_reason) const;
    [[nodiscard]] TypeRef builtin_type(std::string name) const;
    [[nodiscard]] TypeRef named_type(TypeId type_id) const;
    [[nodiscard]] TypeRef generic_type(std::string name) const;
    [[nodiscard]] TypeRef lower_type_ref(const Scope& scope,
                                         const std::vector<std::string>& generics,
                                         const ast::TypeRef& type_ref) const;
    [[nodiscard]] const TypeRef* lookup_binding(const ValueEnv& env, const std::string& name) const;
    void bind_value(ValueEnv& env, const std::string& name, TypeRef type) const;
    [[nodiscard]] bool types_equal(const TypeRef& lhs, const TypeRef& rhs) const;
    [[nodiscard]] TypeRef unify_types(const TypeRef& lhs, const TypeRef& rhs) const;
    [[nodiscard]] std::vector<FieldInit> lower_field_inits(const std::vector<ast::RecordFieldInit>& fields,
                                                           const FunctionContext& context,
                                                           ValueEnv& env);
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
                                                         ValueEnv& env);
    [[nodiscard]] std::unique_ptr<BlockExpr> lower_block_expr(const ast::BlockExpr& expr,
                                                              const FunctionContext& context,
                                                              ValueEnv& env);
    [[nodiscard]] std::unique_ptr<Expr> lower_fail_expr(const ast::FailExpr& expr,
                                                        const FunctionContext& context,
                                                        ValueEnv& env);
    [[nodiscard]] std::unique_ptr<Expr> lower_expr(const ast::Expr& expr,
                                                   const FunctionContext& context,
                                                   ValueEnv& env);
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

        auto [it, inserted] = scope.symbols.try_emplace(
            decl.name, Symbol{&decl, decl.kind, decl.visibility, nullptr});
        if (!inserted) {
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
        case ast::DeclKind::Struct:
        case ast::DeclKind::State:
        case ast::DeclKind::Reason:
        case ast::DeclKind::Proof:
        case ast::DeclKind::Permit:
        case ast::DeclKind::Trait: {
            TypeDecl type;
            type.id = package.types.size();
            type.kind = type_kind_from_decl(decl.kind);
            type.visibility = decl.visibility;
            type.qualified_name = qualified;
            package.types.push_back(std::move(type));
            type_ids[&decl] = package.types.back().id;
            break;
        }
        case ast::DeclKind::Function:
        case ast::DeclKind::ForeignFunction: {
            FunctionDecl function;
            function.id = package.functions.size();
            function.visibility = decl.visibility;
            function.qualified_name = qualified;
            function.is_foreign = decl.kind == ast::DeclKind::ForeignFunction;
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

        const auto type_it = type_ids.find(&decl);
        if (type_it == type_ids.end()) {
            continue;
        }

        TypeDecl& type = package.types[type_it->second];
        switch (decl.kind) {
        case ast::DeclKind::Struct: {
            const auto& struct_decl = static_cast<const ast::StructDecl&>(decl);
            for (const ast::GenericParam& generic : struct_decl.generic_params) {
                type.generics.push_back(generic.name);
            }
            for (const ast::Field& field : struct_decl.fields) {
                type.fields.push_back(FieldDecl{
                    field.name,
                    lower_type_ref(scope, type.generics, field.type),
                });
            }
            break;
        }
        case ast::DeclKind::State: {
            const auto& state_decl = static_cast<const ast::StateDecl&>(decl);
            for (const ast::GenericParam& generic : state_decl.generic_params) {
                type.generics.push_back(generic.name);
            }
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
        case ast::DeclKind::Trait: {
            const auto& trait_decl = static_cast<const ast::TraitDecl&>(decl);
            for (const ast::GenericParam& generic : trait_decl.generic_params) {
                type.generics.push_back(generic.name);
            }
            for (const ast::FunctionSignature& method : trait_decl.methods) {
                std::ostringstream out;
                out << method.name << '(';
                for (std::size_t index = 0; index < method.params.size(); ++index) {
                    if (index > 0) {
                        out << ", ";
                    }
                    out << method.params[index].name << ": " << ast::format_type(method.params[index].type);
                }
                out << ") -> " << ast::format_type(method.return_type);
                if (method.yields_type.has_value()) {
                    out << " yields " << ast::format_type(*method.yields_type);
                }
                type.trait_methods.push_back(out.str());
            }
            break;
        }
        case ast::DeclKind::Permit:
        default:
            break;
        }
    }
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
            function.params.push_back(Parameter{
                param.name,
                lower_type_ref(scope, function.generics, param.type),
            });
        }
        function.return_type = lower_type_ref(scope, function.generics, function_decl.signature.return_type);
        if (function_decl.signature.yields_type.has_value()) {
            TypeRef yields_type = lower_type_ref(scope, function.generics, *function_decl.signature.yields_type);
            function.yields_reason_type_id = yields_type.type_id;
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
        function.body = lower_block_expr(*function_decl.body, context, env);
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
        if (std::find(generics.begin(), generics.end(), name) != generics.end() || is_builtin(name)) {
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
                                           bool allow_state,
                                           bool allow_reason) const {
    VariantResolution result;
    if (path.empty()) {
        return result;
    }

    auto try_owner = [&](const ast::Decl* owner_decl, std::string_view variant_name) {
        if (owner_decl == nullptr) {
            return;
        }
        const auto type_it = type_ids.find(owner_decl);
        if (type_it == type_ids.end()) {
            return;
        }
        const TypeDecl& owner = package.types[type_it->second];
        if ((owner.kind == TypeKind::State && !allow_state)
            || (owner.kind == TypeKind::Reason && !allow_reason)) {
            return;
        }
        if (owner.kind != TypeKind::State && owner.kind != TypeKind::Reason) {
            return;
        }
        for (VariantId variant_id : owner.variants) {
            const VariantDecl& variant = package.variants[variant_id];
            if (variant.name == variant_name) {
                if (result.owner_decl != nullptr && result.owner_decl != owner_decl) {
                    result.ambiguous = true;
                    return;
                }
                result.owner_decl = owner_decl;
                result.variant_id = variant_id;
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
            if (result.ambiguous) {
                return result;
            }
        }
    }
    return result;
}

TypeRef Lowerer::builtin_type(std::string name) const {
    return TypeRef{std::move(name), std::nullopt, true, false};
}

TypeRef Lowerer::named_type(TypeId type_id) const {
    const TypeDecl& type = package.types[type_id];
    return TypeRef{type.qualified_name, type_id, false, false};
}

TypeRef Lowerer::generic_type(std::string name) const {
    return TypeRef{std::move(name), std::nullopt, false, true};
}

TypeRef Lowerer::lower_type_ref(const Scope& scope,
                                const std::vector<std::string>& generics,
                                const ast::TypeRef& type_ref) const {
    if (type_ref.path.empty()) {
        return {};
    }
    const std::string text = ast::format_type(type_ref);
    if (type_ref.path.size() == 1) {
        const std::string& name = type_ref.path.front();
        if (std::find(generics.begin(), generics.end(), name) != generics.end()) {
            TypeRef type = generic_type(name);
            type.text = text;
            return type;
        }
        if (is_builtin(name)) {
            TypeRef type = builtin_type(name);
            type.text = text;
            return type;
        }
    }
    if (const Symbol* symbol = resolve_symbol(scope, generics, type_ref.path); symbol != nullptr) {
        const auto type_it = type_ids.find(symbol->decl);
        if (type_it != type_ids.end()) {
            TypeRef type = named_type(type_it->second);
            type.text = text;
            return type;
        }
    }
    return TypeRef{text, std::nullopt, false, false};
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

bool Lowerer::types_equal(const TypeRef& lhs, const TypeRef& rhs) const {
    return lhs.text == rhs.text && lhs.type_id == rhs.type_id
        && lhs.is_builtin == rhs.is_builtin && lhs.is_generic == rhs.is_generic;
}

TypeRef Lowerer::unify_types(const TypeRef& lhs, const TypeRef& rhs) const {
    if (lhs.text.empty()) {
        return rhs;
    }
    if (rhs.text.empty()) {
        return lhs;
    }
    if (lhs.text == "Never") {
        return rhs;
    }
    if (rhs.text == "Never") {
        return lhs;
    }
    return types_equal(lhs, rhs) ? lhs : lhs;
}

std::vector<FieldInit> Lowerer::lower_field_inits(const std::vector<ast::RecordFieldInit>& fields,
                                                  const FunctionContext& context,
                                                  ValueEnv& env) {
    std::vector<FieldInit> lowered;
    lowered.reserve(fields.size());
    for (const ast::RecordFieldInit& field : fields) {
        FieldInit init;
        init.name = field.name;
        init.shorthand = field.shorthand;
        init.value = lower_expr(*field.value, context, env);
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

    if (VariantResolution state_variant = resolve_variant(context.scope, expr.path, true, false);
        state_variant.owner_decl != nullptr && !state_variant.ambiguous) {
        const VariantDecl& variant = package.variants[state_variant.variant_id];
        if (variant.fields.empty()) {
            auto lowered = std::make_unique<ConstructExpr>(named_type(variant.owner_type));
            lowered->construct_kind = ConstructKind::StateVariant;
            lowered->owner_type_id = variant.owner_type;
            lowered->variant_id = variant.id;
            lowered->qualified_name = variant.qualified_name;
            return lowered;
        }
    }

    if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
        const auto type_it = type_ids.find(symbol->decl);
        if (type_it != type_ids.end()) {
            const TypeDecl& type = package.types[type_it->second];
            if ((type.kind == TypeKind::Struct || type.kind == TypeKind::Proof) && type.fields.empty()) {
                auto lowered = std::make_unique<ConstructExpr>(named_type(type.id));
                lowered->construct_kind = type.kind == TypeKind::Struct ? ConstructKind::Struct : ConstructKind::Proof;
                lowered->owner_type_id = type.id;
                lowered->qualified_name = type.qualified_name;
                return lowered;
            }
        }
    }

    return std::make_unique<LocalRefExpr>(format_path(expr.path), TypeRef{});
}

std::unique_ptr<Expr> Lowerer::lower_call_expr(const ast::CallExpr& expr,
                                               const FunctionContext& context,
                                               ValueEnv& env) {
    auto lowered = std::make_unique<CallExpr>();
    if (const auto* callee_path = dynamic_cast<const ast::PathExpr*>(expr.callee.get())) {
        if (const FunctionDecl* function = resolve_function(context.scope, callee_path->path); function != nullptr) {
            lowered->function_id = function->id;
            lowered->callee_name = function->qualified_name;
            lowered->result_type = function->return_type;
            lowered->yields_reason_type_id = function->yields_reason_type_id;
        } else {
            lowered->callee_name = format_path(callee_path->path);
        }
    }
    for (const auto& arg : expr.args) {
        lowered->args.push_back(lower_expr(*arg, context, env));
    }
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_construct_expr(const ast::ConstructExpr& expr,
                                                    const FunctionContext& context,
                                                    ValueEnv& env) {
    auto lowered = std::make_unique<ConstructExpr>();
    lowered->fields = lower_field_inits(expr.fields, context, env);

    if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
        const auto type_it = type_ids.find(symbol->decl);
        if (type_it != type_ids.end()) {
            const TypeDecl& type = package.types[type_it->second];
            if (type.kind == TypeKind::Struct || type.kind == TypeKind::Proof) {
                lowered->result_type = named_type(type.id);
                lowered->construct_kind = type.kind == TypeKind::Struct ? ConstructKind::Struct : ConstructKind::Proof;
                lowered->owner_type_id = type.id;
                lowered->qualified_name = type.qualified_name;
                return lowered;
            }
        }
    }

    if (VariantResolution state_variant = resolve_variant(context.scope, expr.path, true, false);
        state_variant.owner_decl != nullptr && !state_variant.ambiguous) {
        const VariantDecl& variant = package.variants[state_variant.variant_id];
        lowered->result_type = named_type(variant.owner_type);
        lowered->construct_kind = ConstructKind::StateVariant;
        lowered->owner_type_id = variant.owner_type;
        lowered->variant_id = variant.id;
        lowered->qualified_name = variant.qualified_name;
        return lowered;
    }

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
                                                ValueEnv& env) {
    auto lowered = std::make_unique<MatchExpr>();
    lowered->scrutinee = lower_expr(*expr.scrutinee, context, env);
    TypeRef result_type;

    for (const ast::MatchArm& arm : expr.arms) {
        MatchArm lowered_arm;
        ValueEnv arm_env{&env, {}};

        if (lowered->scrutinee->yields_reason_type_id.has_value()) {
            switch (arm.pattern->kind) {
            case ast::PatternKind::Succeeded: {
                const auto& pattern = static_cast<const ast::SucceededPattern&>(*arm.pattern);
                auto lowered_pattern = std::make_unique<SucceededPattern>();
                lowered_pattern->binding_name = pattern.binding_name;
                lowered_pattern->ignore = pattern.ignore;
                if (pattern.binding_name.has_value()) {
                    bind_value(arm_env, *pattern.binding_name, lowered->scrutinee->result_type);
                }
                lowered_arm.pattern = std::move(lowered_pattern);
                break;
            }
            case ast::PatternKind::Failed: {
                const auto& pattern = static_cast<const ast::FailedPattern&>(*arm.pattern);
                VariantResolution resolution = resolve_variant(context.scope, pattern.variant->path, false, true);
                auto lowered_pattern = std::make_unique<FailedPattern>();
                if (resolution.owner_decl != nullptr && !resolution.ambiguous) {
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
            VariantResolution resolution = resolve_variant(context.scope, pattern.path, true, false);
            if (resolution.owner_decl != nullptr && !resolution.ambiguous) {
                auto lowered_pattern = lower_variant_pattern(pattern, resolution.variant_id);
                bind_variant_fields(arm_env,
                                    static_cast<const VariantPattern&>(*lowered_pattern),
                                    package.variants[resolution.variant_id]);
                lowered_arm.pattern = std::move(lowered_pattern);
            }
        }

        lowered_arm.body = lower_expr(*arm.body, context, arm_env);
        result_type = unify_types(result_type, lowered_arm.body->result_type);
        lowered->arms.push_back(std::move(lowered_arm));
    }

    lowered->result_type = result_type.text.empty() ? builtin_type("Unit") : result_type;
    return lowered;
}

std::unique_ptr<BlockExpr> Lowerer::lower_block_expr(const ast::BlockExpr& expr,
                                                     const FunctionContext& context,
                                                     ValueEnv& env) {
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
        lowered->result = lower_expr(*expr.result, context, local);
        lowered->result_type = lowered->result->result_type;
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
    lowered->fields = lower_field_inits(expr.fields, context, env);
    if (VariantResolution resolution = resolve_variant(context.scope, expr.path, false, true);
        resolution.owner_decl != nullptr && !resolution.ambiguous) {
        lowered->reason_type_id = type_ids.at(resolution.owner_decl);
        lowered->variant_id = resolution.variant_id;
        lowered->reason_name = package.types[lowered->reason_type_id].qualified_name;
        lowered->variant_name = package.variants[resolution.variant_id].name;
    }
    return lowered;
}

std::unique_ptr<Expr> Lowerer::lower_expr(const ast::Expr& expr,
                                          const FunctionContext& context,
                                          ValueEnv& env) {
    switch (expr.kind) {
    case ast::ExprKind::NumberLiteral:
        return std::make_unique<NumberLiteralExpr>(
            static_cast<const ast::NumberLiteralExpr&>(expr).lexeme, builtin_type("Int"));
    case ast::ExprKind::StringLiteral:
        return std::make_unique<StringLiteralExpr>(
            static_cast<const ast::StringLiteralExpr&>(expr).lexeme, builtin_type("Text"));
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
        return lower_match_expr(static_cast<const ast::MatchExpr&>(expr), context, env);
    case ast::ExprKind::Block:
        return lower_block_expr(static_cast<const ast::BlockExpr&>(expr), context, env);
    case ast::ExprKind::Fail:
        return lower_fail_expr(static_cast<const ast::FailExpr&>(expr), context, env);
    }
    return std::make_unique<UnitExpr>(builtin_type("Unit"));
}

void dump_pattern(std::ostream& out, const Pattern& pattern, std::size_t depth);
void dump_expr(std::ostream& out, const Expr& expr, std::size_t depth);

void dump_fields(std::ostream& out, const std::vector<FieldDecl>& fields, std::size_t depth) {
    for (const FieldDecl& field : fields) {
        indent(out, depth);
        out << "field " << field.name << ": " << field.type.text << '\n';
    }
}

void dump_field_inits(std::ostream& out, const std::vector<FieldInit>& fields, std::size_t depth) {
    for (const FieldInit& field : fields) {
        indent(out, depth);
        out << "init " << field.name;
        if (field.shorthand) {
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
        out << "let " << let_stmt.name << ": " << let_stmt.type.text << '\n';
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
        out << "number " << static_cast<const NumberLiteralExpr&>(expr).lexeme << " : " << expr.result_type.text << '\n';
        break;
    case ExprKind::StringLiteral:
        out << "string " << static_cast<const StringLiteralExpr&>(expr).lexeme << " : " << expr.result_type.text << '\n';
        break;
    case ExprKind::Unit:
        out << "unit\n";
        break;
    case ExprKind::LocalRef:
        out << "local " << static_cast<const LocalRefExpr&>(expr).name << " : " << expr.result_type.text << '\n';
        break;
    case ExprKind::Call: {
        const auto& call = static_cast<const CallExpr&>(expr);
        out << "call " << call.callee_name << " -> " << expr.result_type.text;
        if (expr.yields_reason_type_id.has_value()) {
            out << " yields";
        }
        out << '\n';
        for (const auto& arg : call.args) {
            dump_expr(out, *arg, depth + 1);
        }
        break;
    }
    case ExprKind::Construct: {
        const auto& construct = static_cast<const ConstructExpr&>(expr);
        out << "construct " << construct.qualified_name << " : " << expr.result_type.text << '\n';
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
        out << "match -> " << expr.result_type.text << '\n';
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
        out << "block -> " << expr.result_type.text << '\n';
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
        if (succeeded.ignore) {
            out << '_';
        } else if (succeeded.binding_name.has_value()) {
            out << *succeeded.binding_name;
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
    case TypeKind::Struct:
        return "struct";
    case TypeKind::State:
        return "state";
    case TypeKind::Reason:
        return "reason";
    case TypeKind::Proof:
        return "proof";
    case TypeKind::Permit:
        return "permit";
    case TypeKind::Trait:
        return "trait";
    }
    return "type";
}

Package lower(const ast::TranslationUnit& unit) {
    Lowerer lowerer;
    return lowerer.lower(unit);
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
            << ' ' << type.qualified_name << '\n';
        if (!type.fields.empty()) {
            dump_fields(out, type.fields, 3);
        }
        for (VariantId variant_id : type.variants) {
            const VariantDecl& variant = package.variants[variant_id];
            indent(out, 3);
            out << "variant " << variant.name << '\n';
            dump_fields(out, variant.fields, 4);
        }
        for (const std::string& method : type.trait_methods) {
            indent(out, 3);
            out << "method " << method << '\n';
        }
    }

    out << "functions:\n";
    for (const FunctionDecl& function : package.functions) {
        out << "  - " << ast::visibility_name(function.visibility) << ' '
            << (function.is_foreign ? "foreign-fn " : "fn ") << function.qualified_name << '(';
        for (std::size_t index = 0; index < function.params.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            out << function.params[index].name << ": " << function.params[index].type.text;
        }
        out << ") -> " << function.return_type.text;
        if (function.yields_reason_type_id.has_value()) {
            out << " yields " << package.types[*function.yields_reason_type_id].qualified_name;
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
        out << "- " << function.qualified_name << " -> " << function.return_type.text;
        if (function.yields_reason_type_id.has_value()) {
            out << " yields " << package.types[*function.yields_reason_type_id].qualified_name;
        }
        if (function.is_foreign) {
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
