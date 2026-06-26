#include "evident/Semantic.hpp"
#include "evident/ResolvedType.hpp"
#include "evident/Source.hpp"

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
    ast::DeclKind kind = ast::DeclKind::Record;
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
    "Present", "Absent", "Missing", "Some", "None", "Known", "Unknown",
};

const std::unordered_set<std::string_view> kBuiltins = {
    "Int",   "Nat",   "Float",  "Char",  "Text",    "Bytes",
    "Never", "List",  "NonEmptyList",  "Map",   "NonEmptyMap",    "CString",
    "CInt",  "CSize", "Byte",   "Unit",
};

bool is_reserved_public_name(std::string_view name) {
    return name.size() == 1 || kReservedPublicNames.contains(name);
}

bool is_builtin(std::string_view name) {
    return kBuiltins.contains(name);
}

std::optional<std::size_t> expected_builtin_type_arg_count(std::string_view name) {
    if (name == "List" || name == "NonEmptyList") {
        return 1;
    }
    if (name == "Map" || name == "NonEmptyMap") {
        return 2;
    }
    if (is_builtin(name)) {
        return 0;
    }
    return std::nullopt;
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
    explicit Analyzer(DiagnosticSink& diagnostics, const SourceFile& source)
        : diagnostics_(diagnostics),
          source_(source),
          discipline_classifier_([this](const ast::Decl* owner_decl,
                                        const std::vector<typesys::Type>& owner_args,
                                        const ast::TypeRef& type_ref) {
              return resolve_member_type(owner_decl, owner_args, type_ref);
          }) {}

    void analyze(const ast::TranslationUnit& unit) {
        build_scope(unit.decls, root_, "");
        validate_imports(unit.imports);
        analyze_scope(unit.decls, root_, true, std::nullopt);
    }

private:
    using Type = typesys::Type;
    using UseDiscipline = typesys::UseDiscipline;

    struct ExprType {
        Type value;
        const ast::ReasonDecl* yielded_reason = nullptr;
        bool reachable = true;
    };

    using BindingId = std::size_t;

    struct BindingState {
        BindingId id = 0;
        Type type;
        UseDiscipline discipline = UseDiscipline::Copyable;
        bool moved = false;
        std::optional<SourceSpan> moved_at;
    };

    struct ValueEnv {
        std::vector<std::unordered_map<std::string, BindingState>> scopes;
    };

    struct BranchState {
        ValueEnv env;
        bool reachable = true;
    };

    struct FunctionContext {
        const Scope& scope;
        const ast::FunctionDecl& function;
        std::vector<std::string> generics;
        std::vector<std::pair<std::string, Type>> substitutions;
        Type return_type;
        const ast::ReasonDecl* fails_reason = nullptr;
        std::vector<const ast::ProofDecl*> proves_proofs;
    };

    struct VariantResolution {
        const ast::Decl* owner_decl = nullptr;
        const ast::Variant* variant = nullptr;
        bool ambiguous = false;
    };

    struct PhasePositionType {
        const ast::Decl* decl = nullptr;
        ast::Visibility visibility = ast::Visibility::Private;
        std::string qualified_name;
    };

    DiagnosticSink& diagnostics_;
    const SourceFile& source_;
    Scope root_;
    std::unordered_map<const ast::Decl*, std::string> qualified_names_;
    std::unordered_map<const ast::Decl*, const Scope*> decl_scopes_;
    std::unordered_map<std::string, std::vector<std::vector<std::string>>> imported_module_paths_by_file_;
    std::unordered_set<std::string> checked_generic_function_instantiations_;
    std::unordered_set<std::string> active_generic_function_instantiations_;
    bool import_gate_enabled_ = false;
    typesys::DisciplineClassifier discipline_classifier_;
    BindingId next_binding_id_ = 1;

    void build_scope(const std::vector<std::unique_ptr<ast::Decl>>& decls, Scope& scope, const std::string& prefix) {
        for (const auto& decl_ptr : decls) {
            const ast::Decl& decl = *decl_ptr;
            auto [it, inserted] = scope.symbols.try_emplace(decl.name, Symbol{&decl, decl.kind, decl.visibility, nullptr});
            if (!inserted) {
                if (decl.kind == ast::DeclKind::Module && it->second.kind == ast::DeclKind::Module
                    && it->second.child_scope != nullptr) {
                    const auto& existing_module = static_cast<const ast::ModuleDecl&>(*it->second.decl);
                    const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
                    if (existing_module.module_kind != module_decl.module_kind) {
                        diagnostics_.error(decl.span,
                                           "split module '" + decl.name + "' must use module kind '"
                                               + std::string(ast::module_kind_name(existing_module.module_kind)) + "'");
                        continue;
                    }

                    const std::string qualified = prefix.empty() ? decl.name : prefix + "::" + decl.name;
                    qualified_names_[&decl] = qualified;
                    decl_scopes_[&decl] = &scope;
                    build_scope(module_decl.members, *it->second.child_scope, qualified);
                    continue;
                }
                diagnostics_.error(decl.span, "duplicate declaration for '" + decl.name + "'");
                continue;
            }

            const std::string qualified = prefix.empty() ? decl.name : prefix + "::" + decl.name;
            qualified_names_[&decl] = qualified;
            decl_scopes_[&decl] = &scope;

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
        case ast::DeclKind::Record:
        case ast::DeclKind::State:
        case ast::DeclKind::Reason:
        case ast::DeclKind::Proof:
        case ast::DeclKind::Permit:
        case ast::DeclKind::Phase:
            return true;
        default:
            return false;
        }
    }

    Type error_type() const {
        return typesys::error_type();
    }

    Type builtin_type(std::string name, std::vector<Type> args = {}) const {
        return typesys::builtin_type(std::move(name), std::move(args));
    }

    Type generic_type(std::string name) const {
        return typesys::generic_type(std::move(name));
    }

    Type named_type(const ast::Decl* decl, std::vector<Type> args = {}) const {
        auto it = qualified_names_.find(decl);
        const std::string name = it != qualified_names_.end() ? it->second : decl->name;
        return typesys::named_type(name, decl, std::move(args));
    }

    // True if `ancestor` is `descendant` or an ancestor along `parent` links (nested modules).
    static bool scope_encloses(const Scope* ancestor, const Scope* descendant) {
        for (const Scope* p = descendant; p != nullptr; p = p->parent) {
            if (p == ancestor) {
                return true;
            }
        }
        return false;
    }

    const Scope* declaring_scope(const ast::Decl* decl) const {
        const auto it = decl_scopes_.find(decl);
        return it != decl_scopes_.end() ? it->second : nullptr;
    }

    const Symbol* containing_top_level_module(const Scope& scope) const {
        for (const auto& [_, symbol] : root_.symbols) {
            if (symbol.kind != ast::DeclKind::Module || symbol.child_scope == nullptr) {
                continue;
            }
            if (scope_encloses(symbol.child_scope.get(), &scope)) {
                return &symbol;
            }
        }
        return nullptr;
    }

    bool path_is_under_import(const std::vector<std::string>& path, SourceSpan span) const {
        const std::string source_path(source_.path_at(span.begin));
        const auto imports_it = imported_module_paths_by_file_.find(source_path);
        if (imports_it == imported_module_paths_by_file_.end()) {
            return false;
        }

        for (const std::vector<std::string>& imported_path : imports_it->second) {
            if (path.size() < imported_path.size()) {
                continue;
            }
            if (std::equal(imported_path.begin(), imported_path.end(), path.begin())) {
                return true;
            }
        }
        return false;
    }

    bool check_import_access(const Scope& scope, const std::vector<std::string>& path, SourceSpan span) {
        if (!import_gate_enabled_ || path.size() < 2) {
            return true;
        }

        const auto root_it = root_.symbols.find(path.front());
        if (root_it == root_.symbols.end() || root_it->second.kind != ast::DeclKind::Module) {
            return true;
        }

        if (const Symbol* current_top_level = containing_top_level_module(scope);
            current_top_level != nullptr && current_top_level->decl != nullptr
            && current_top_level->decl->name == path.front()) {
            return true;
        }

        if (path_is_under_import(path, span)) {
            return true;
        }

        diagnostics_.error(span,
                           "cross-module reference '" + format_path(path) + "' requires a matching import");
        return false;
    }

    std::optional<PhasePositionType> resolve_phase_position_type(const Scope& scope,
                                                                 const std::vector<std::string>& generics,
                                                                 const std::vector<std::string>& path) const {
        if (path.size() < 2) {
            return std::nullopt;
        }

        std::vector<std::string> family_path(path.begin(), path.end() - 1);
        const std::string& position_name = path.back();
        const Symbol* symbol = resolve_symbol(scope, generics, family_path);
        if (symbol == nullptr || symbol->kind != ast::DeclKind::Phase) {
            return std::nullopt;
        }

        const auto& phase_decl = static_cast<const ast::PhaseDecl&>(*symbol->decl);
        if (std::find(phase_decl.positions.begin(), phase_decl.positions.end(), position_name)
            == phase_decl.positions.end()) {
            return std::nullopt;
        }

        return PhasePositionType{
            symbol->decl,
            symbol->visibility,
            qualified_names_.at(symbol->decl) + "::" + position_name,
        };
    }

    bool is_error(const Type& type) const {
        return typesys::is_error(type);
    }

    bool is_never(const Type& type) const {
        return typesys::is_never(type);
    }

    bool is_permit_type(const Type& type) const {
        return typesys::is_compile_time_only(discipline(type));
    }

    UseDiscipline discipline(const Type& type) const {
        return discipline_classifier_.classify(type);
    }

    std::string type_name(const Type& type) const {
        return typesys::type_name(type);
    }

    bool types_equal(const Type& lhs, const Type& rhs) const {
        return typesys::types_equal(lhs, rhs);
    }

    ExprType make_expr(Type value, const ast::ReasonDecl* yielded_reason = nullptr) const {
        const bool reachable = !is_never(value);
        return ExprType{std::move(value), yielded_reason, reachable};
    }

    bool assignable_to(const Type& target, const Type& actual) const {
        if (is_error(target) || is_error(actual) || is_never(actual)) {
            return true;
        }
        return types_equal(target, actual);
    }

    const std::vector<ast::GenericParam>* generic_params_for(const ast::Decl* decl) const {
        if (decl == nullptr) {
            return nullptr;
        }
        switch (decl->kind) {
        case ast::DeclKind::Record:
            return &static_cast<const ast::RecordDecl*>(decl)->generic_params;
        case ast::DeclKind::Function:
        case ast::DeclKind::ForeignFunction:
            return &static_cast<const ast::FunctionDecl*>(decl)->signature.generic_params;
        case ast::DeclKind::State:
        case ast::DeclKind::Reason:
        case ast::DeclKind::Proof:
        case ast::DeclKind::Permit:
        case ast::DeclKind::Phase:
        case ast::DeclKind::Module:
            return nullptr;
        }
        return nullptr;
    }

    Type resolve_type(const Scope& scope,
                      const std::vector<std::string>& generics,
                      const std::vector<std::pair<std::string, Type>>& substitutions,
                      const ast::TypeRef& type_ref) const {
        if (type_ref.path.empty()) {
            return error_type();
        }

        std::vector<Type> args;
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
                return generic_type(name);
            }
            if (is_builtin(name)) {
                return builtin_type(name, std::move(args));
            }
        }

        if (const auto phase_position = resolve_phase_position_type(scope, generics, type_ref.path);
            phase_position.has_value()) {
            return typesys::named_type(phase_position->qualified_name, phase_position->decl, std::move(args));
        }

        if (const Symbol* symbol = resolve_symbol(scope, generics, type_ref.path); symbol != nullptr) {
            if (symbol->kind == ast::DeclKind::Phase) {
                return error_type();
            }
            if (is_type_decl_kind(symbol->kind)) {
                return named_type(symbol->decl, std::move(args));
            }
        }
        return error_type();
    }

    Type resolve_type(const Scope& scope,
                      const std::vector<std::string>& generics,
                      const ast::TypeRef& type_ref) const {
        return resolve_type(scope, generics, {}, type_ref);
    }

    Type resolve_member_type(const ast::Decl* owner_decl,
                             const std::vector<Type>& owner_args,
                             const ast::TypeRef& type_ref) const {
        std::vector<std::pair<std::string, Type>> substitutions;
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
        return resolve_type(*decl_scopes_.at(owner_decl), generics, substitutions, type_ref);
    }

    std::vector<std::string> generic_names_for(const std::vector<ast::GenericParam>& generic_params) const {
        std::vector<std::string> generics;
        generics.reserve(generic_params.size());
        for (const ast::GenericParam& generic : generic_params) {
            generics.push_back(generic.name);
        }
        return generics;
    }

    std::string generic_function_instantiation_key(
        const ast::FunctionDecl& function,
        const std::vector<std::pair<std::string, Type>>& substitutions) const {
        std::ostringstream out;
        out << static_cast<const void*>(&function) << '<';
        for (std::size_t index = 0; index < substitutions.size(); ++index) {
            if (index > 0) {
                out << ',';
            }
            out << substitutions[index].first << '=' << type_name(substitutions[index].second);
        }
        out << '>';
        return out.str();
    }

    bool prepare_call_type_substitutions(const ast::FunctionDecl& function,
                                         const ast::CallExpr& call,
                                         const FunctionContext& context,
                                         std::vector<std::pair<std::string, Type>>& substitutions) {
        const std::size_t expected = function.signature.generic_params.size();
        const std::size_t actual = call.type_args.size();
        if (expected == 0) {
            if (actual != 0) {
                diagnostics_.error(call.span,
                                   "function '" + function.name + "' is not generic but was called with type arguments");
                return false;
            }
            return true;
        }
        if (actual == 0) {
            diagnostics_.error(call.span,
                               "generic function '" + function.name + "' requires explicit type arguments");
            return false;
        }
        if (expected != actual) {
            diagnostics_.error(call.span,
                               "generic function '" + function.name + "' expects " + std::to_string(expected)
                                   + " type argument(s), got " + std::to_string(actual));
            return false;
        }

        substitutions.reserve(expected);
        for (std::size_t index = 0; index < expected; ++index) {
            const ast::TypeRef& type_arg = call.type_args[index];
            check_type_ref_usage(context.scope,
                                 context.generics,
                                 type_arg,
                                 TypeUseRules{false, false, false, "generic function type argument"});
            substitutions.emplace_back(function.signature.generic_params[index].name,
                                       resolve_type(context.scope, context.generics, context.substitutions, type_arg));
        }
        return true;
    }

    bool prepare_record_constructor_type_args(const ast::RecordDecl& record,
                                              const ast::ConstructExpr& construct,
                                              const FunctionContext& context,
                                              std::vector<Type>& type_args) {
        const std::size_t expected = record.generic_params.size();
        const std::size_t actual = construct.type_args.size();
        if (expected == 0) {
            if (actual != 0) {
                diagnostics_.error(construct.span,
                                   "record '" + record.name
                                       + "' is not generic but was constructed with type arguments");
                return false;
            }
            return true;
        }
        if (actual == 0) {
            diagnostics_.error(construct.span,
                               "generic record '" + record.name + "' requires explicit type arguments");
            return false;
        }
        if (expected != actual) {
            diagnostics_.error(construct.span,
                               "generic record '" + record.name + "' expects " + std::to_string(expected)
                                   + " type argument(s), got " + std::to_string(actual));
            return false;
        }

        type_args.reserve(expected);
        for (const ast::TypeRef& type_arg : construct.type_args) {
            check_type_ref_usage(context.scope,
                                 context.generics,
                                 type_arg,
                                 TypeUseRules{false, false, false, "generic record constructor type argument"});
            type_args.push_back(resolve_type(context.scope, context.generics, context.substitutions, type_arg));
        }
        return true;
    }

    ValueEnv make_root_env() const {
        ValueEnv env;
        env.scopes.emplace_back();
        return env;
    }

    ValueEnv push_scope(const ValueEnv& env) const {
        ValueEnv child = env;
        child.scopes.emplace_back();
        return child;
    }

    const BindingState* lookup_binding(const ValueEnv& env, const std::string& name) const {
        for (auto scope_it = env.scopes.rbegin(); scope_it != env.scopes.rend(); ++scope_it) {
            if (const auto it = scope_it->find(name); it != scope_it->end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    BindingState* lookup_binding_mut(ValueEnv& env, const std::string& name) const {
        for (auto scope_it = env.scopes.rbegin(); scope_it != env.scopes.rend(); ++scope_it) {
            if (const auto it = scope_it->find(name); it != scope_it->end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    const BindingState* lookup_binding_by_id(const ValueEnv& env, BindingId id) const {
        for (auto scope_it = env.scopes.rbegin(); scope_it != env.scopes.rend(); ++scope_it) {
            for (const auto& [_, binding] : *scope_it) {
                if (binding.id == id) {
                    return &binding;
                }
            }
        }
        return nullptr;
    }

    void merge_sequential_env(ValueEnv& target, const ValueEnv& child) const {
        for (auto& scope : target.scopes) {
            for (auto& [_, binding] : scope) {
                if (binding.moved) {
                    continue;
                }
                const BindingState* child_binding = lookup_binding_by_id(child, binding.id);
                if (child_binding != nullptr && child_binding->moved) {
                    binding.moved = true;
                    binding.moved_at = child_binding->moved_at;
                }
            }
        }
    }

    void merge_branch_envs(ValueEnv& target, const std::vector<BranchState>& branches) const {
        for (auto& scope : target.scopes) {
            for (auto& [_, binding] : scope) {
                if (binding.moved) {
                    continue;
                }
                for (const BranchState& branch : branches) {
                    if (!branch.reachable) {
                        continue;
                    }
                    const BindingState* branch_binding = lookup_binding_by_id(branch.env, binding.id);
                    if (branch_binding != nullptr && branch_binding->moved) {
                        binding.moved = true;
                        binding.moved_at = branch_binding->moved_at;
                        break;
                    }
                }
            }
        }
    }

    Type use_binding(ValueEnv& env, const std::string& name, SourceSpan span) {
        BindingState* binding = lookup_binding_mut(env, name);
        if (binding == nullptr) {
            return error_type();
        }
        if (binding->moved) {
            diagnostics_.error(span, "affine value '" + name + "' was already moved");
            if (binding->moved_at.has_value()) {
                diagnostics_.note(*binding->moved_at, "'" + name + "' was previously moved here");
            }
            return error_type();
        }
        if (typesys::is_affine(binding->discipline)) {
            binding->moved = true;
            binding->moved_at = span;
        }
        return binding->type;
    }

    Type read_binding(ValueEnv& env, const std::string& name, SourceSpan span) {
        const BindingState* binding = lookup_binding(env, name);
        if (binding == nullptr) {
            return error_type();
        }
        if (binding->moved) {
            diagnostics_.error(span, "affine value '" + name + "' was already moved");
            if (binding->moved_at.has_value()) {
                diagnostics_.note(*binding->moved_at, "'" + name + "' was previously moved here");
            }
            return error_type();
        }
        return binding->type;
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

    void validate_imports(const std::vector<ast::ImportDecl>& imports) {
        imported_module_paths_by_file_.clear();
        import_gate_enabled_ = false;
        std::unordered_map<std::string, std::unordered_set<std::string>> seen_by_file;
        for (const ast::ImportDecl& import_decl : imports) {
            if (import_decl.path.empty()) {
                continue;
            }

            const std::string import_name = format_path(import_decl.path);
            const std::string source_path(source_.path_at(import_decl.span.begin));
            std::unordered_set<std::string>& seen_in_file = seen_by_file[source_path];
            if (!seen_in_file.insert(import_name).second) {
                diagnostics_.error(import_decl.span, "duplicate import '" + import_name + "'");
                continue;
            }

            const Symbol* symbol = resolve_symbol(root_, {}, import_decl.path);
            if (symbol == nullptr) {
                diagnostics_.error(import_decl.span, "unknown import '" + import_name + "'");
                continue;
            }
            if (symbol->kind != ast::DeclKind::Module) {
                diagnostics_.error(import_decl.span,
                                   "import must name a module, not '" + std::string(ast::decl_kind_name(symbol->kind)) + "'");
                continue;
            }
            imported_module_paths_by_file_[source_path].push_back(import_decl.path);
        }
        import_gate_enabled_ = !imported_module_paths_by_file_.empty();
    }

    void check_type_ref_usage(const Scope& scope,
                              const std::vector<std::string>& generics,
                              const ast::TypeRef& type,
                              const TypeUseRules& rules) {
        const bool is_single_generic = type.path.size() == 1
            && std::find(generics.begin(), generics.end(), type.path.front()) != generics.end();

        if (!type.path.empty() && is_builtin(type.path.front()) && type.path.size() != 1) {
            diagnostics_.error(type.span,
                              "builtin type '" + type.path.front() + "' may not be used as a qualified type");
        }

        if (is_single_generic && !type.args.empty()) {
            diagnostics_.error(type.span,
                              "generic parameter '" + type.path.front() + "' may not have type arguments");
        } else if (type.path.size() == 1) {
            const std::optional<std::size_t> expected_builtin_args =
                expected_builtin_type_arg_count(type.path.front());
            if (expected_builtin_args.has_value()) {
                const std::size_t actual = type.args.size();
                if (*expected_builtin_args == 0 && actual != 0) {
                    diagnostics_.error(type.span,
                                      "builtin type '" + type.path.front() + "' may not have type arguments");
                } else if (*expected_builtin_args != 0 && actual == 0) {
                    diagnostics_.error(type.span,
                                      "builtin type '" + type.path.front() + "' requires "
                                          + std::to_string(*expected_builtin_args) + " type argument(s)");
                } else if (*expected_builtin_args != actual) {
                    diagnostics_.error(type.span,
                                      "builtin type '" + type.path.front() + "' expects "
                                          + std::to_string(*expected_builtin_args) + " type argument(s), got "
                                          + std::to_string(actual));
                }
            }
        }

        if (!type.path.empty() && !is_builtin(type.path.front()) && !is_single_generic) {
            const auto phase_position = resolve_phase_position_type(scope, generics, type.path);
            if (phase_position.has_value()) {
                (void)check_import_access(scope, type.path, type.span);
                if (rules.require_public && phase_position->visibility != ast::Visibility::Public) {
                    diagnostics_.error(type.span,
                                      "public API cannot reference private type '" + ast::format_type(type) + "'");
                }
                if (!type.args.empty()) {
                    diagnostics_.error(type.span,
                                      "phase type '" + ast::format_type(type) + "' may not have generic arguments");
                }
            } else if (const Symbol* symbol = resolve_symbol(scope, generics, type.path); symbol == nullptr) {
                diagnostics_.error(type.span,
                                  "unknown type '" + ast::format_type(type) + "' in " + std::string(rules.context));
            } else {
                (void)check_import_access(scope, type.path, type.span);
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
                    if (const auto* generic_params = generic_params_for(symbol->decl); generic_params != nullptr) {
                        const std::size_t expected = generic_params->size();
                        const std::size_t actual = type.args.size();
                        if (expected == 0 && actual != 0) {
                            diagnostics_.error(type.span,
                                              std::string(ast::decl_kind_name(symbol->kind)) + " type '"
                                                  + format_path(type.path)
                                                  + "' is not generic but was used with type arguments");
                        } else if (expected != 0 && actual == 0) {
                            diagnostics_.error(type.span,
                                              "generic " + std::string(ast::decl_kind_name(symbol->kind)) + " '"
                                                  + format_path(type.path) + "' requires type arguments");
                        } else if (expected != actual) {
                            diagnostics_.error(type.span,
                                              "generic " + std::string(ast::decl_kind_name(symbol->kind)) + " '"
                                                  + format_path(type.path) + "' expects "
                                                  + std::to_string(expected) + " type argument(s), got "
                                                  + std::to_string(actual));
                        }
                    } else if (!type.args.empty()) {
                        diagnostics_.error(type.span,
                                          std::string(ast::decl_kind_name(symbol->kind)) + " type '"
                                              + format_path(type.path) + "' may not have generic arguments");
                    }
                    if (symbol->kind == ast::DeclKind::Phase) {
                        const auto& phase_decl = static_cast<const ast::PhaseDecl&>(*symbol->decl);
                        const std::string family_name = format_path(type.path);
                        std::string example = family_name + "::<Position>";
                        if (!phase_decl.positions.empty()) {
                            example = family_name + "::" + phase_decl.positions.front();
                        }
                        diagnostics_.error(type.span,
                                          "phase family name '" + family_name
                                              + "' is not a concrete type; use a concrete position such as '"
                                              + example + "'");
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
            const Symbol* param_symbol = resolve_symbol(scope, generics, param.type.path);
            const bool top_level_permit = param_symbol != nullptr && param_symbol->kind == ast::DeclKind::Permit;
            check_type_ref_usage(scope,
                                 generics,
                                 param.type,
                                 TypeUseRules{require_public, false, param.is_permit_param || top_level_permit, "function parameter"});
            if (param.is_permit_param) {
                if (param_symbol == nullptr || param_symbol->kind != ast::DeclKind::Permit) {
                    diagnostics_.error(param.type.span,
                                      "permit parameter '" + param.name + "' must reference a permit type");
                }
            } else if (param_symbol != nullptr && param_symbol->kind == ast::DeclKind::Permit) {
                diagnostics_.error(param.span,
                                  "permit parameter '" + param.name + "' must be written as 'as "
                                      + param.name + ": " + ast::format_type(param.type) + "'");
            }
        }

        check_type_ref_usage(scope,
                             generics,
                             signature.return_type,
                             TypeUseRules{require_public, false, false, "function return type"});

        if (signature.fails_type.has_value()) {
            check_type_ref_usage(scope,
                                 generics,
                                 *signature.fails_type,
                                 TypeUseRules{require_public, true, false, "fails clause"});
            if (const Symbol* symbol = resolve_symbol(scope, generics, signature.fails_type->path); symbol != nullptr) {
                if (symbol->kind != ast::DeclKind::Reason) {
                    diagnostics_.error(signature.fails_type->span,
                                      "'fails' must reference a reason type, not '"
                                          + std::string(ast::decl_kind_name(symbol->kind)) + "'");
                }
            }
            if (is_foreign) {
                diagnostics_.error(signature.fails_type->span, "foreign functions may not use 'fails'");
            }
        }

        if (signature.grants_type.has_value()) {
            check_type_ref_usage(scope,
                                 generics,
                                 *signature.grants_type,
                                 TypeUseRules{require_public, false, true, "grant permit"});
            if (const Symbol* symbol = resolve_symbol(scope, generics, signature.grants_type->path); symbol != nullptr) {
                if (symbol->kind != ast::DeclKind::Permit) {
                    diagnostics_.error(signature.grants_type->span,
                                      "'grants' must reference a permit type, not '"
                                          + std::string(ast::decl_kind_name(symbol->kind)) + "'");
                } else if (declaring_scope(symbol->decl) != &scope) {
                    diagnostics_.error(signature.grants_type->span,
                                      "functions may only declare 'grants' for permits from the same module");
                }
            }
            const Type return_type = resolve_type(scope, generics, signature.return_type);
            if (!types_equal(return_type, builtin_type("Unit"))) {
                diagnostics_.error(signature.return_type.span, "granting functions must return 'Unit'");
            }
            if (is_foreign) {
                diagnostics_.error(signature.grants_type->span, "foreign functions may not use 'grants'");
            }
        }

        std::unordered_set<const ast::Decl*> seen_proves;
        for (const ast::TypeRef& proves_type : signature.proves_types) {
            check_type_ref_usage(scope,
                                 generics,
                                 proves_type,
                                 TypeUseRules{require_public, false, false, "proof authorization"});
            if (const Symbol* symbol = resolve_symbol(scope, generics, proves_type.path); symbol != nullptr) {
                if (symbol->kind != ast::DeclKind::Proof) {
                    diagnostics_.error(proves_type.span,
                                      "'proves' must reference a proof type, not '"
                                          + std::string(ast::decl_kind_name(symbol->kind)) + "'");
                } else if (declaring_scope(symbol->decl) != &scope) {
                    diagnostics_.error(proves_type.span,
                                      "functions may only declare 'proves' for proofs from the same module");
                } else if (!seen_proves.insert(symbol->decl).second) {
                    diagnostics_.error(proves_type.span,
                                      "function signature repeats `proves` for proof type '"
                                          + ast::format_type(proves_type) + "'");
                }
            }
            if (is_foreign) {
                diagnostics_.error(proves_type.span, "foreign functions may not use 'proves'");
            }
        }
    }

    void enforce_module_kind(ast::ModuleKind mk, const ast::Decl& decl) {
        switch (mk) {
        case ast::ModuleKind::Foundation: {
            if (decl.kind == ast::DeclKind::Module) {
                diagnostics_.error(decl.span, "foundation modules may not nest additional `module` declarations");
                return;
            }
            if (decl.kind != ast::DeclKind::Record && decl.kind != ast::DeclKind::Function) {
                diagnostics_.error(decl.span, "foundation modules may only declare `record` and `fn`");
            }
            if (decl.visibility == ast::Visibility::Public) {
                if (decl.kind == ast::DeclKind::State || decl.kind == ast::DeclKind::Reason
                    || decl.kind == ast::DeclKind::Proof || decl.kind == ast::DeclKind::Permit
                    || decl.kind == ast::DeclKind::Phase) {
                    diagnostics_.error(decl.span,
                                      "foundation modules must not declare public state, reason, proof, permit, or phase");
                }
            }
            break;
        }
        case ast::ModuleKind::Domain:
            if (decl.kind == ast::DeclKind::ForeignFunction) {
                diagnostics_.error(decl.span, "`domain` modules must not declare `foreign fn`");
            }
            if (decl.kind == ast::DeclKind::Record) {
                const auto& rd = static_cast<const ast::RecordDecl&>(decl);
                if (!rd.generic_params.empty()) {
                    diagnostics_.error(rd.generic_params.front().span,
                                       "`domain` modules must not declare user-defined generics");
                }
            }
            if (decl.kind == ast::DeclKind::Function) {
                const auto& fd = static_cast<const ast::FunctionDecl&>(decl);
                if (!fd.signature.generic_params.empty()) {
                    diagnostics_.error(fd.signature.generic_params.front().span,
                                       "`domain` modules must not declare user-defined generics");
                }
            }
            break;
        case ast::ModuleKind::Boundary:
        case ast::ModuleKind::Hazard:
            if (decl.kind == ast::DeclKind::Record) {
                const auto& rd = static_cast<const ast::RecordDecl&>(decl);
                if (!rd.generic_params.empty()) {
                    diagnostics_.error(rd.generic_params.front().span,
                                       "this module kind must not declare user-defined generics on `record`");
                }
            }
            if (decl.kind == ast::DeclKind::Function) {
                const auto& fd = static_cast<const ast::FunctionDecl&>(decl);
                if (!fd.signature.generic_params.empty()) {
                    diagnostics_.error(fd.signature.generic_params.front().span,
                                       "this module kind must not declare user-defined generics on `fn`");
                }
            }
            break;
        }
    }

    void analyze_scope(const std::vector<std::unique_ptr<ast::Decl>>& decls,
                       const Scope& scope,
                       bool enclosing_public,
                       std::optional<ast::ModuleKind> module_kind) {
        for (const auto& decl_ptr : decls) {
            const ast::Decl& decl = *decl_ptr;
            if (!module_kind.has_value() && decl.kind != ast::DeclKind::Module) {
                diagnostics_.error(decl.span, "declarations must appear inside a module");
                continue;
            }
            if (module_kind.has_value()) {
                enforce_module_kind(*module_kind, decl);
            }
            const bool public_decl = decl.visibility == ast::Visibility::Public;
            const bool effectively_public = enclosing_public && public_decl;

            if (public_decl) {
                check_public_name(decl.name, decl.span);
            }

            switch (decl.kind) {
            case ast::DeclKind::Module: {
                const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
                const auto it = scope.symbols.find(module_decl.name);
                if (it != scope.symbols.end() && it->second.child_scope != nullptr) {
                    analyze_scope(module_decl.members, *it->second.child_scope, effectively_public, module_decl.module_kind);
                }
                break;
            }
            case ast::DeclKind::Record: {
                const auto& record_decl = static_cast<const ast::RecordDecl&>(decl);
                const bool public_type_decl = record_decl.visibility == ast::Visibility::Public;
                check_duplicate_generic_params(record_decl.generic_params, "record");
                check_duplicate_fields(record_decl.fields, "record");
                std::vector<std::string> generics;
                for (const ast::GenericParam& generic : record_decl.generic_params) {
                    generics.push_back(generic.name);
                }
                for (const ast::Field& field : record_decl.fields) {
                    check_type_ref_usage(scope,
                                         generics,
                                         field.type,
                                         TypeUseRules{public_type_decl && field.visibility == ast::Visibility::Public,
                                                      false,
                                                      false,
                                                      "record field"});
                }
                break;
            }
            case ast::DeclKind::State: {
                const auto& state_decl = static_cast<const ast::StateDecl&>(decl);
                const bool public_type_decl = state_decl.visibility == ast::Visibility::Public;
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
                check_duplicate_variants(state_decl.variants, "state");
                for (const ast::Variant& variant : state_decl.variants) {
                    if (public_type_decl) {
                        check_public_name(variant.name, variant.span);
                    }
                    check_duplicate_fields(variant.fields, "state variant");
                    for (const ast::Field& field : variant.fields) {
                        check_type_ref_usage(scope,
                                             {},
                                             field.type,
                                             TypeUseRules{public_type_decl, false, false, "state variant payload"});
                    }
                }
                break;
            }
            case ast::DeclKind::Reason: {
                const auto& reason_decl = static_cast<const ast::ReasonDecl&>(decl);
                const bool public_type_decl = reason_decl.visibility == ast::Visibility::Public;
                if (reason_decl.variants.empty()) {
                    diagnostics_.error(reason_decl.span, "reason declarations must define at least one variant");
                }
                check_duplicate_variants(reason_decl.variants, "reason");
                for (const ast::Variant& variant : reason_decl.variants) {
                    if (public_type_decl) {
                        check_public_name(variant.name, variant.span);
                    }
                    check_duplicate_fields(variant.fields, "reason variant");
                    for (const ast::Field& field : variant.fields) {
                        check_type_ref_usage(scope,
                                             {},
                                             field.type,
                                             TypeUseRules{public_type_decl, false, false, "reason payload"});
                    }
                }
                break;
            }
            case ast::DeclKind::Proof: {
                const auto& proof_decl = static_cast<const ast::ProofDecl&>(decl);
                const bool public_type_decl = proof_decl.visibility == ast::Visibility::Public;
                check_duplicate_fields(proof_decl.fields, "proof");
                for (const ast::Field& field : proof_decl.fields) {
                    check_type_ref_usage(scope,
                                         {},
                                         field.type,
                                         TypeUseRules{public_type_decl && field.visibility == ast::Visibility::Public,
                                                      false,
                                                      false,
                                                      "proof field"});
                }
                break;
            }
            case ast::DeclKind::Permit:
                break;
            case ast::DeclKind::Phase: {
                const auto& phase_decl = static_cast<const ast::PhaseDecl&>(decl);
                const bool public_type_decl = phase_decl.visibility == ast::Visibility::Public;
                if (phase_decl.positions.empty()) {
                    diagnostics_.error(phase_decl.span, "phase declarations must define at least one position");
                }
                check_duplicate_fields(phase_decl.fields, "phase");
                std::unordered_set<std::string> seen_positions;
                for (std::size_t index = 0; index < phase_decl.positions.size(); ++index) {
                    const std::string& pos = phase_decl.positions[index];
                    if (!seen_positions.insert(pos).second) {
                        const SourceSpan span = index < phase_decl.position_spans.size()
                            ? phase_decl.position_spans[index]
                            : phase_decl.span;
                        diagnostics_.error(span, "duplicate phase position '" + pos + "'");
                    }
                }
                for (const ast::Field& field : phase_decl.fields) {
                    check_type_ref_usage(scope,
                                         {},
                                         field.type,
                                         TypeUseRules{public_type_decl && field.visibility == ast::Visibility::Public,
                                                      false,
                                                      false,
                                                      "phase field"});
                }
                break;
            }
            case ast::DeclKind::Function:
            case ast::DeclKind::ForeignFunction: {
                const auto& function_decl = static_cast<const ast::FunctionDecl&>(decl);
                // Public functions must not expose private types in their signature even when the
                // enclosing module is private (visibility is per-declaration).
                const bool signature_is_public_api = function_decl.visibility == ast::Visibility::Public;
                check_function_signature(scope,
                                         function_decl.signature,
                                         signature_is_public_api,
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

    void bind_value(ValueEnv& env, const std::string& name, Type type, SourceSpan span) {
        const UseDiscipline binding_discipline = discipline(type);
        if (!env.scopes.back().emplace(name, BindingState{
                next_binding_id_++,
                std::move(type),
                binding_discipline,
                false,
                std::nullopt,
            }).second) {
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

    void check_call_arguments(const ast::FunctionDecl& function,
                              const std::vector<std::unique_ptr<ast::Expr>>& args,
                              const FunctionContext& context,
                              ValueEnv& env,
                              const Scope& callee_scope,
                              const std::vector<std::string>& callee_generics,
                              const std::vector<std::pair<std::string, Type>>& substitutions,
                              SourceSpan call_span) {
        if (function.signature.params.size() != args.size()) {
            diagnostics_.error(call_span,
                              "function '" + function.name + "' expects "
                                  + std::to_string(function.signature.params.size()) + " argument(s), got "
                                  + std::to_string(args.size()));
        }

        const std::size_t arg_count = std::min(function.signature.params.size(), args.size());
        for (std::size_t index = 0; index < arg_count; ++index) {
            const Type param_type = resolve_type(callee_scope,
                                                 callee_generics,
                                                 substitutions,
                                                 function.signature.params[index].type);
            if (is_permit_type(param_type)) {
                const auto* path_arg = dynamic_cast<const ast::PathExpr*>(args[index].get());
                if (path_arg == nullptr || path_arg->path.size() != 1 || !path_arg->explicit_permit_argument) {
                    diagnostics_.error(args[index]->span,
                                      "permit argument must be written as 'as name'");
                    continue;
                }
                const BindingState* binding = lookup_binding(env, path_arg->path.front());
                if (binding == nullptr || !is_permit_type(binding->type)) {
                    diagnostics_.error(args[index]->span,
                                      "argument " + std::to_string(index + 1) + " to '" + function.name
                                          + "' must be permit '" + type_name(param_type) + "'");
                    continue;
                }
                if (!assignable_to(param_type, binding->type)) {
                    diagnostics_.error(args[index]->span,
                                      "argument " + std::to_string(index + 1) + " to '" + function.name
                                          + "' has type '" + type_name(binding->type) + "', expected '"
                                          + type_name(param_type) + "'");
                }
                continue;
            }

            ExprType arg_type = type_expr(*args[index], context, env);
            if (arg_type.yielded_reason != nullptr) {
                diagnostics_.error(args[index]->span,
                                    "failing expression must be handled with `try` or `match`");
            }
            if (!assignable_to(param_type, arg_type.value)) {
                diagnostics_.error(args[index]->span,
                                  "argument " + std::to_string(index + 1) + " to '" + function.name
                                      + "' has type '" + type_name(arg_type.value) + "', expected '"
                                      + type_name(param_type) + "'");
            }
        }
    }

    bool check_initializer_fields(const ast::Decl* owner_decl,
                                  const std::vector<ast::Field>& expected_fields,
                                  const std::vector<ast::RecordFieldInit>& actual_fields,
                                  const FunctionContext& context,
                                  ValueEnv& env,
                                  SourceSpan construct_span,
                                  const std::vector<Type>& owner_args = {}) {
        if (owner_decl != nullptr) {
            const auto owner_it = decl_scopes_.find(owner_decl);
            if (owner_it != decl_scopes_.end()) {
                const Scope* const owner_scope = owner_it->second;
                for (const ast::Field& field : expected_fields) {
                    if (field.visibility != ast::Visibility::Public
                        && !scope_encloses(owner_scope, &context.scope)) {
                        diagnostics_.error(construct_span,
                                            "cannot construct here: field '" + field.name + "' is private");
                        return false;
                    }
                }
            }
        }

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
                diagnostics_.error(init.span, "failing expression must be handled with `try` or `match`");
                ok = false;
            }
            const Type expected_type = owner_decl != nullptr
                ? resolve_member_type(owner_decl, owner_args, it->second->type)
                : resolve_type(context.scope, context.generics, context.substitutions, it->second->type);
            if (!assignable_to(expected_type, init_type.value)) {
                diagnostics_.error(init.span,
                                  "initializer for field '" + init.name + "' has type '" + type_name(init_type.value)
                                      + "', expected '" + type_name(expected_type) + "'");
                ok = false;
            }
        }

        for (const ast::Field& field : expected_fields) {
            if (!seen.contains(field.name)) {
                diagnostics_.error(construct_span, "missing initializer for field '" + field.name + "'");
                ok = false;
            }
        }
        return ok;
    }

    ExprType type_path_expr(const ast::PathExpr& expr, const FunctionContext& context, ValueEnv& env) {
        if (expr.path.size() == 1) {
            if (const BindingState* binding = lookup_binding(env, expr.path.front()); binding != nullptr) {
                if (is_permit_type(binding->type)) {
                    diagnostics_.error(expr.span, "permit value may only be used as a direct function argument");
                    return make_expr(error_type());
                }
                return make_expr(use_binding(env, expr.path.front(), expr.span));
            }
            if (const Symbol* sym = resolve_symbol(context.scope, context.generics, expr.path);
                sym != nullptr && sym->decl->kind == ast::DeclKind::Phase) {
                diagnostics_.error(expr.span,
                                  "a phase family name is not a value type; use a concrete position such as 'Phase::Position'");
                return make_expr(error_type());
            }
        }

        if (expr.path.size() >= 2) {
            std::vector<std::string> family_path(expr.path.begin(), expr.path.end() - 1);
            const std::string& position_name = expr.path.back();
            if (const Symbol* sym = resolve_symbol(context.scope, context.generics, family_path);
                sym != nullptr && sym->decl->kind == ast::DeclKind::Phase) {
                const auto& pd = static_cast<const ast::PhaseDecl&>(*sym->decl);
                if (std::find(pd.positions.begin(), pd.positions.end(), position_name) != pd.positions.end()) {
                    if (!check_import_access(context.scope, expr.path, expr.span)) {
                        return make_expr(error_type());
                    }
                    const std::string qn = qualified_names_.at(sym->decl) + "::" + position_name;
                    diagnostics_.error(expr.span,
                                      "phase position '" + qn
                                          + "' must be constructed with named fields");
                    return make_expr(error_type());
                }
            }
        }

        VariantResolution state_variant = resolve_variant(context.scope, expr.path, true, false);
        if (state_variant.ambiguous) {
            diagnostics_.error(expr.span, "ambiguous variant reference '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (state_variant.variant != nullptr) {
            if (!check_import_access(context.scope, expr.path, expr.span)) {
                return make_expr(error_type());
            }
            if (!state_variant.variant->fields.empty()) {
                diagnostics_.error(expr.span,
                                  "variant '" + state_variant.variant->name + "' requires payload construction");
                return make_expr(error_type());
            }
            return make_expr(named_type(state_variant.owner_decl));
        }

        VariantResolution reason_variant = resolve_variant(context.scope, expr.path, false, true);
        if (reason_variant.ambiguous) {
            diagnostics_.error(expr.span, "ambiguous reason variant reference '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (reason_variant.variant != nullptr) {
            diagnostics_.error(expr.span, "reason variants may appear only in 'fail' or failed(...) patterns");
            return make_expr(error_type());
        }

        if (const ast::FunctionDecl* function = resolve_function(context.scope, expr.path); function != nullptr) {
            if (!check_import_access(context.scope, expr.path, expr.span)) {
                return make_expr(error_type());
            }
            diagnostics_.error(expr.span, "function values are not first-class; call '" + function->name + "' with '(...)'");
            return make_expr(error_type());
        }

        if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
            if (!check_import_access(context.scope, expr.path, expr.span)) {
                return make_expr(error_type());
            }
            if (symbol->kind == ast::DeclKind::Proof) {
                diagnostics_.error(expr.span, "proof values must be created with 'prove'");
                return make_expr(error_type());
            }
            if (symbol->kind == ast::DeclKind::Record) {
                const auto* record_decl = static_cast<const ast::RecordDecl*>(symbol->decl);
                if (record_decl->fields.empty()) {
                    return make_expr(named_type(symbol->decl));
                }
                diagnostics_.error(expr.span, "record '" + record_decl->name + "' requires field initialization");
                return make_expr(error_type());
            }
            diagnostics_.error(expr.span, "type name '" + expr.path.back() + "' is not a value");
            return make_expr(error_type());
        }

        diagnostics_.error(expr.span, "unknown value '" + expr.path.back() + "'");
        return make_expr(error_type());
    }

    ExprType type_call_expr(const ast::CallExpr& expr, const FunctionContext& context, ValueEnv& env) {
        const auto* callee_path = dynamic_cast<const ast::PathExpr*>(expr.callee.get());
        if (callee_path == nullptr) {
            diagnostics_.error(expr.span, "callee must be a named function");
            return make_expr(error_type());
        }

        const ast::FunctionDecl* function = resolve_function(context.scope, callee_path->path);
        if (function == nullptr) {
            diagnostics_.error(expr.span, "unknown function '" + (callee_path->path.empty() ? std::string{} : callee_path->path.back()) + "'");
            return make_expr(error_type());
        }
        if (!check_import_access(context.scope, callee_path->path, callee_path->span)) {
            return make_expr(error_type());
        }
        if (function->signature.grants_type.has_value()) {
            diagnostics_.error(expr.span, "function '" + function->name + "' grants a permit and must be used with 'grant'");
            return make_expr(error_type());
        }
        std::vector<std::pair<std::string, Type>> substitutions;
        if (!prepare_call_type_substitutions(*function, expr, context, substitutions)) {
            return make_expr(error_type());
        }
        const Scope* callee_scope = declaring_scope(function);
        if (callee_scope == nullptr) {
            return make_expr(error_type());
        }
        const std::vector<std::string> callee_generics = generic_names_for(function->signature.generic_params);
        check_call_arguments(*function, expr.args, context, env, *callee_scope, callee_generics, substitutions, expr.span);
        check_generic_function_instantiation(*function, *callee_scope, substitutions);

        ExprType result = make_expr(resolve_type(*callee_scope,
                                                 callee_generics,
                                                 substitutions,
                                                 function->signature.return_type));
        if (function->signature.fails_type.has_value()) {
            const Type fails_type = resolve_type(*callee_scope,
                                                 callee_generics,
                                                 substitutions,
                                                 *function->signature.fails_type);
            result.yielded_reason = fails_type.decl != nullptr ? static_cast<const ast::ReasonDecl*>(fails_type.decl) : nullptr;
        }
        return result;
    }

    ExprType type_construct_expr(const ast::ConstructExpr& expr, const FunctionContext& context, ValueEnv& env) {
        if (expr.path.size() >= 2) {
            std::vector<std::string> family_path(expr.path.begin(), expr.path.end() - 1);
            const std::string& position_name = expr.path.back();
            if (const Symbol* sym = resolve_symbol(context.scope, context.generics, family_path);
                sym != nullptr && sym->decl->kind == ast::DeclKind::Phase) {
                const auto& pd = static_cast<const ast::PhaseDecl&>(*sym->decl);
                if (!expr.type_args.empty()) {
                    diagnostics_.error(expr.span,
                                      "phase position constructor '" + qualified_names_.at(sym->decl) + "::"
                                          + position_name + "' may not have generic arguments");
                    return make_expr(error_type());
                }
                if (std::find(pd.positions.begin(), pd.positions.end(), position_name) == pd.positions.end()) {
                    diagnostics_.error(expr.span, "unknown phase position '" + position_name + "'");
                    return make_expr(error_type());
                }
                if (!check_import_access(context.scope, expr.path, expr.span)) {
                    return make_expr(error_type());
                }
                const Scope* const phase_scope = declaring_scope(sym->decl);
                if (phase_scope != nullptr && !scope_encloses(phase_scope, &context.scope)) {
                    diagnostics_.error(expr.span,
                                      "cannot construct phase position '" + qualified_names_.at(sym->decl) + "::"
                                          + position_name + "' outside its declaring module");
                    return make_expr(error_type());
                }
                if (!check_initializer_fields(sym->decl, pd.fields, expr.fields, context, env, expr.span)) {
                    return make_expr(error_type());
                }
                const std::string qn = qualified_names_.at(sym->decl) + "::" + position_name;
                return make_expr(typesys::named_type(qn, sym->decl));
            }
        }

        if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
            if (!check_import_access(context.scope, expr.path, expr.span)) {
                return make_expr(error_type());
            }
            switch (symbol->kind) {
            case ast::DeclKind::Record: {
                const auto* record_decl = static_cast<const ast::RecordDecl*>(symbol->decl);
                std::vector<Type> type_args;
                if (!prepare_record_constructor_type_args(*record_decl, expr, context, type_args)) {
                    return make_expr(error_type());
                }
                if (!check_initializer_fields(symbol->decl,
                                              record_decl->fields,
                                              expr.fields,
                                              context,
                                              env,
                                              expr.span,
                                              type_args)) {
                    return make_expr(error_type());
                }
                return make_expr(typesys::named_type(qualified_names_.at(symbol->decl),
                                                     symbol->decl,
                                                     std::move(type_args)));
            }
            case ast::DeclKind::Proof: {
                diagnostics_.error(expr.span, "proof values must be created with 'prove'");
                return make_expr(error_type());
            }
            case ast::DeclKind::Permit:
                diagnostics_.error(expr.span, "permit values may not be constructed directly");
                return make_expr(error_type());
            case ast::DeclKind::Reason:
                diagnostics_.error(expr.span, "reason values may not be constructed directly; use 'fail'");
                return make_expr(error_type());
            default:
                break;
            }
        }

        VariantResolution state_variant = resolve_variant(context.scope, expr.path, true, false);
        if (state_variant.ambiguous) {
            diagnostics_.error(expr.span, "ambiguous variant constructor '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (state_variant.variant != nullptr) {
            if (!expr.type_args.empty()) {
                diagnostics_.error(expr.span,
                                  "variant constructor '" + expr.path.back() + "' may not have generic arguments");
                return make_expr(error_type());
            }
            if (!check_import_access(context.scope, expr.path, expr.span)) {
                return make_expr(error_type());
            }
            if (!check_initializer_fields(state_variant.owner_decl, state_variant.variant->fields, expr.fields, context,
                                           env, expr.span)) {
                return make_expr(error_type());
            }
            return make_expr(named_type(state_variant.owner_decl));
        }

        VariantResolution reason_variant = resolve_variant(context.scope, expr.path, false, true);
        if (reason_variant.variant != nullptr) {
            diagnostics_.error(expr.span, "reason values may not be constructed directly; use 'fail'");
            return make_expr(error_type());
        }

        diagnostics_.error(expr.span, "unknown constructor '" + expr.path.back() + "'");
        return make_expr(error_type());
    }

    ExprType type_grant_expr(const ast::GrantExpr& expr, const FunctionContext& context, ValueEnv& env) {
        const auto* grant_call = dynamic_cast<const ast::CallExpr*>(expr.grant_call.get());
        if (grant_call == nullptr) {
            diagnostics_.error(expr.span, "'grant' requires a direct grantor call");
            return make_expr(error_type());
        }
        const auto* callee_path = dynamic_cast<const ast::PathExpr*>(grant_call->callee.get());
        if (callee_path == nullptr) {
            diagnostics_.error(expr.span, "'grant' requires a named grantor function");
            return make_expr(error_type());
        }

        const ast::FunctionDecl* function = resolve_function(context.scope, callee_path->path);
        if (function == nullptr) {
            diagnostics_.error(expr.span,
                              "unknown function '" + (callee_path->path.empty() ? std::string{} : callee_path->path.back()) + "'");
            return make_expr(error_type());
        }
        if (!check_import_access(context.scope, callee_path->path, callee_path->span)) {
            return make_expr(error_type());
        }
        if (!function->signature.generic_params.empty()) {
            diagnostics_.error(expr.span, "'grant' may not call a generic grantor function");
            return make_expr(error_type());
        }
        if (!function->signature.grants_type.has_value()) {
            diagnostics_.error(expr.span, "'grant' requires a function annotated with 'grants'");
            return make_expr(error_type());
        }

        const Scope* callee_scope = declaring_scope(function);
        if (callee_scope == nullptr) {
            return make_expr(error_type());
        }

        // Spec allows fails + grants together.
        const Type return_type = resolve_type(*callee_scope, {}, function->signature.return_type);
        if (!types_equal(return_type, builtin_type("Unit"))) {
            diagnostics_.error(expr.span, "'grant' requires a grantor function that returns 'Unit'");
            return make_expr(error_type());
        }

        check_call_arguments(*function,
                             grant_call->args,
                             context,
                             env,
                             *callee_scope,
                             {},
                             {},
                             grant_call->span);

        const ast::ReasonDecl* grantor_reason = nullptr;
        if (function->signature.fails_type.has_value()) {
            const Type fails_type = resolve_type(*callee_scope, {}, *function->signature.fails_type);
            if (fails_type.decl != nullptr && fails_type.decl->kind == ast::DeclKind::Reason) {
                grantor_reason = static_cast<const ast::ReasonDecl*>(fails_type.decl);
            }
        }

        ValueEnv scoped_env = push_scope(env);
        const Type permit_type = resolve_type(*callee_scope, {}, *function->signature.grants_type);
        bind_value(scoped_env, expr.binder_name, permit_type, expr.span);
        ExprType result = type_block(*expr.body, context, scoped_env);
        if (result.reachable) {
            merge_sequential_env(env, scoped_env);
        }

        const ast::ReasonDecl* yielded_reason = grantor_reason;
        if (result.yielded_reason != nullptr) {
            if (yielded_reason != nullptr && yielded_reason != result.yielded_reason) {
                diagnostics_.error(expr.span,
                                  "'grant' body fails with reason '" + result.yielded_reason->name
                                      + "' but grantor fails with reason '" + yielded_reason->name + "'");
                return make_expr(error_type());
            }
            yielded_reason = result.yielded_reason;
        }
        return make_expr(result.value, yielded_reason);
    }

    ExprType type_prove_expr(const ast::ProveExpr& expr, const FunctionContext& context, ValueEnv& env) {
        const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path);
        if (symbol == nullptr) {
            diagnostics_.error(expr.span, "unknown proof type '" + (expr.path.empty() ? std::string{} : expr.path.back()) + "'");
            return make_expr(error_type());
        }
        if (symbol->kind != ast::DeclKind::Proof) {
            diagnostics_.error(expr.span,
                               "'prove' expects a proof type, found '"
                                   + std::string(ast::decl_kind_name(symbol->kind)) + "'");
            return make_expr(error_type());
        }
        if (!check_import_access(context.scope, expr.path, expr.span)) {
            return make_expr(error_type());
        }

        const auto* proof_decl = static_cast<const ast::ProofDecl*>(symbol->decl);
        if (context.proves_proofs.empty()) {
            diagnostics_.error(expr.span, "'prove' is only valid inside a function with 'proves'");
            return make_expr(named_type(symbol->decl));
        }
        bool proves_ok = false;
        for (const ast::ProofDecl* allowed : context.proves_proofs) {
            if (allowed == proof_decl) {
                proves_ok = true;
                break;
            }
        }
        if (!proves_ok) {
            diagnostics_.error(expr.span,
                              "'prove' must name one of the proof types listed in the function's `proves` clauses");
        }
        if (!check_initializer_fields(proof_decl, proof_decl->fields, expr.fields, context, env, expr.span)) {
            return make_expr(error_type());
        }
        return make_expr(named_type(symbol->decl));
    }

    ExprType type_try_expr(const ast::TryExpr& expr, const FunctionContext& context, ValueEnv& env) {
        ExprType operand = type_expr(*expr.operand, context, env);
        if (operand.yielded_reason == nullptr) {
            diagnostics_.error(expr.span, "`try` requires an operand that may fail (a call to a function with `fails`)");
            return make_expr(error_type());
        }
        if (context.fails_reason == nullptr) {
            diagnostics_.error(expr.span, "'try' is only valid inside a function with 'fails'");
            return make_expr(error_type());
        }
        if (operand.yielded_reason != context.fails_reason) {
            diagnostics_.error(expr.span,
                              "'try' expects failure reason '" + context.fails_reason->name + "', got '"
                                  + operand.yielded_reason->name + "'");
            return make_expr(error_type());
        }
        return make_expr(operand.value);
    }

    ExprType type_fail_expr(const ast::FailExpr& expr, const FunctionContext& context, ValueEnv& env) {
        if (context.fails_reason == nullptr) {
            diagnostics_.error(expr.span, "'fail' is only valid inside a function with 'fails'");
            return make_expr(error_type());
        }

        VariantResolution variant = resolve_variant(context.scope, expr.path, false, true);
        if (variant.ambiguous) {
            diagnostics_.error(expr.span, "ambiguous reason variant '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (variant.variant == nullptr || variant.owner_decl == nullptr) {
            diagnostics_.error(expr.span, "unknown reason variant '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (!check_import_access(context.scope, expr.path, expr.span)) {
            return make_expr(error_type());
        }
        if (variant.owner_decl != context.fails_reason) {
            diagnostics_.error(expr.span,
                              "'fail' must use a variant of failure reason '" + context.fails_reason->name + "'");
        }
        if (!check_initializer_fields(variant.owner_decl, variant.variant->fields, expr.fields, context, env, expr.span)) {
            return make_expr(error_type());
        }
        return make_expr(builtin_type("Never"));
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
            const Type field_type = resolve_type(context.scope,
                                                 context.generics,
                                                 context.substitutions,
                                                 it->second->type);
            bind_value(env, binding.binding_name, field_type, binding.span);
        }
    }

    ExprType type_match_expr(const ast::MatchExpr& expr, const FunctionContext& context, ValueEnv& env) {
        ExprType scrutinee = type_expr(*expr.scrutinee, context, env);
        Type result_type = builtin_type("Never");
        bool has_arm = false;
        std::vector<BranchState> arm_states;

        if (scrutinee.yielded_reason != nullptr) {
            bool seen_success = false;
            std::unordered_set<std::string> seen_failed;
            for (const ast::MatchArm& arm : expr.arms) {
                ValueEnv arm_env = push_scope(env);
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
                    } else if (!check_import_access(context.scope, pattern.variant->path, pattern.span)) {
                        continue;
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
                                      "match over a `fails` call requires succeeded(...) and failed(...) patterns");
                    break;
                }

                ExprType arm_type = type_expr(*arm.body, context, arm_env);
                if (arm_type.yielded_reason != nullptr) {
                    diagnostics_.error(arm.body->span,
                                        "failing expression must be handled with `try` or `match`");
                }
                result_type = unify_match_result(result_type, arm_type.value, arm.span);
                has_arm = true;
                arm_states.push_back(BranchState{std::move(arm_env), arm_type.reachable});
            }

            if (!seen_success) {
                diagnostics_.error(expr.span, "match over a `fails` call must include a succeeded(...) arm");
            }
            for (const ast::Variant& variant : scrutinee.yielded_reason->variants) {
                if (!seen_failed.contains(variant.name)) {
                    diagnostics_.error(expr.span,
                                      "non-exhaustive failed(...) coverage; missing '" + variant.name + "'");
                }
            }
            merge_branch_envs(env, arm_states);
            return make_expr(has_arm ? result_type : builtin_type("Unit"));
        }

        if (scrutinee.value.decl != nullptr && scrutinee.value.decl->kind == ast::DeclKind::Phase) {
            diagnostics_.error(expr.scrutinee->span,
                               "concrete phase value '" + type_name(scrutinee.value)
                                   + "' cannot be matched; use a state for runtime alternatives");
            return make_expr(error_type());
        }

        const ast::StateDecl* state_decl = as_state(scrutinee.value.decl);
        if (state_decl == nullptr) {
            diagnostics_.error(expr.scrutinee->span,
                                "match requires a state value or an expression that may fail (`fails`)");
            return make_expr(error_type());
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
            if (!check_import_access(context.scope, pattern.path, pattern.span)) {
                continue;
            }
            if (!seen_variants.insert(resolution.variant->name).second) {
                diagnostics_.error(pattern.span,
                                  "duplicate match arm for variant '" + resolution.variant->name + "'");
            }

            ValueEnv arm_env = push_scope(env);
            bind_variant_pattern(pattern, *resolution.variant, context, arm_env);
            ExprType arm_type = type_expr(*arm.body, context, arm_env);
            if (arm_type.yielded_reason != nullptr) {
                diagnostics_.error(arm.body->span,
                                    "failing expression must be handled with `try` or `match`");
            }
            result_type = unify_match_result(result_type, arm_type.value, arm.span);
            has_arm = true;
            arm_states.push_back(BranchState{std::move(arm_env), arm_type.reachable});
        }

        for (const ast::Variant& variant : state_decl->variants) {
            if (!seen_variants.contains(variant.name)) {
                diagnostics_.error(expr.span,
                                  "non-exhaustive match over state '" + state_decl->name + "'; missing '"
                                      + variant.name + "'");
            }
        }
        merge_branch_envs(env, arm_states);
        return make_expr(has_arm ? result_type : builtin_type("Unit"));
    }

    ExprType type_block(const ast::BlockExpr& block, const FunctionContext& context, ValueEnv& parent_env) {
        ValueEnv local = push_scope(parent_env);
        for (const auto& stmt_ptr : block.statements) {
            const ast::Stmt& stmt = *stmt_ptr;
            switch (stmt.kind) {
            case ast::StmtKind::Let: {
                const auto& let_stmt = static_cast<const ast::LetStmt&>(stmt);
                ExprType init_type = type_expr(*let_stmt.initializer, context, local);
                if (init_type.yielded_reason != nullptr) {
                    diagnostics_.error(let_stmt.initializer->span,
                                        "failing expression must be handled with `try` or `match`");
                }
                if (!init_type.reachable) {
                    return init_type;
                }
                bind_value(local, let_stmt.name, init_type.value, let_stmt.span);
                break;
            }
            case ast::StmtKind::Expr: {
                const auto& expr_stmt = static_cast<const ast::ExprStmt&>(stmt);
                ExprType expr_type = type_expr(*expr_stmt.expr, context, local);
                if (expr_type.yielded_reason != nullptr) {
                    diagnostics_.error(expr_stmt.expr->span,
                                        "failing expression must be handled with `try` or `match`");
                }
                if (!expr_type.reachable) {
                    return expr_type;
                }
                break;
            }
            }
        }

        if (block.result != nullptr) {
            ExprType result = type_expr(*block.result, context, local);
            if (result.reachable) {
                merge_sequential_env(parent_env, local);
            }
            return result;
        }
        merge_sequential_env(parent_env, local);
        return make_expr(builtin_type("Unit"));
    }

    ExprType type_field_receiver_expr(const ast::Expr& expr, const FunctionContext& context, ValueEnv& env) {
        if (expr.kind == ast::ExprKind::Path) {
            const auto& path = static_cast<const ast::PathExpr&>(expr);
            if (path.path.size() == 1) {
                if (const BindingState* binding = lookup_binding(env, path.path.front()); binding != nullptr) {
                    if (is_permit_type(binding->type)) {
                        diagnostics_.error(path.span, "permit value may only be used as a direct function argument");
                        return make_expr(error_type());
                    }
                    return make_expr(read_binding(env, path.path.front(), path.span));
                }
            }
        }
        return type_expr(expr, context, env);
    }

    ExprType type_field_access_expr(const ast::FieldAccessExpr& expr, const FunctionContext& context, ValueEnv& env) {
        ExprType recv = type_field_receiver_expr(*expr.object, context, env);
        if (is_error(recv.value)) {
            return recv;
        }
        const ast::Decl* decl = recv.value.decl;
        if (decl == nullptr) {
            diagnostics_.error(expr.span,
                               "field access requires record, proof, or phase value, got '"
                                   + type_name(recv.value) + "'");
            return make_expr(error_type());
        }
        const auto owner_it = decl_scopes_.find(decl);
        if (owner_it == decl_scopes_.end()) {
            diagnostics_.error(expr.span,
                               "field access requires record, proof, or phase value, got '"
                                   + type_name(recv.value) + "'");
            return make_expr(error_type());
        }
        const Scope* const owner_scope = owner_it->second;

        if (decl->kind == ast::DeclKind::Record) {
            const auto* rd = static_cast<const ast::RecordDecl*>(decl);
            for (const ast::Field& f : rd->fields) {
                if (f.name == expr.field_name) {
                    if (f.visibility != ast::Visibility::Public
                        && !scope_encloses(owner_scope, &context.scope)) {
                        diagnostics_.error(expr.span, "field '" + expr.field_name + "' is private");
                        return make_expr(error_type());
                    }
                    const Type field_ty = resolve_member_type(decl, recv.value.args, f.type);
                    if (typesys::is_affine(discipline(recv.value))
                        && discipline(field_ty) != typesys::UseDiscipline::Copyable) {
                        diagnostics_.error(expr.span,
                                            "field access on affine value requires a copyable field type");
                        return make_expr(error_type());
                    }
                    return make_expr(field_ty);
                }
            }
        }
        if (decl->kind == ast::DeclKind::Proof) {
            const auto* pd = static_cast<const ast::ProofDecl*>(decl);
            for (const ast::Field& f : pd->fields) {
                if (f.name == expr.field_name) {
                    if (f.visibility != ast::Visibility::Public
                        && !scope_encloses(owner_scope, &context.scope)) {
                        diagnostics_.error(expr.span, "field '" + expr.field_name + "' is private");
                        return make_expr(error_type());
                    }
                    const Type field_ty = resolve_member_type(decl, recv.value.args, f.type);
                    if (typesys::is_affine(discipline(recv.value))
                        && discipline(field_ty) != typesys::UseDiscipline::Copyable) {
                        diagnostics_.error(expr.span,
                                            "field access on affine value requires a copyable field type");
                        return make_expr(error_type());
                    }
                    return make_expr(field_ty);
                }
            }
        }
        if (decl->kind == ast::DeclKind::Phase) {
            const auto* pd = static_cast<const ast::PhaseDecl*>(decl);
            for (const ast::Field& f : pd->fields) {
                if (f.name == expr.field_name) {
                    if (f.visibility != ast::Visibility::Public
                        && !scope_encloses(owner_scope, &context.scope)) {
                        diagnostics_.error(expr.span, "field '" + expr.field_name + "' is private");
                        return make_expr(error_type());
                    }
                    const Type field_ty = resolve_member_type(decl, recv.value.args, f.type);
                    if (typesys::is_affine(discipline(recv.value))
                        && discipline(field_ty) != typesys::UseDiscipline::Copyable) {
                        diagnostics_.error(expr.span,
                                            "field access on affine value requires a copyable field type");
                        return make_expr(error_type());
                    }
                    return make_expr(field_ty);
                }
            }
        }
        diagnostics_.error(expr.span,
                           "type '" + type_name(recv.value) + "' has no field '" + expr.field_name + "'");
        return make_expr(error_type());
    }

    ExprType type_expr(const ast::Expr& expr, const FunctionContext& context, ValueEnv& env) {
        switch (expr.kind) {
        case ast::ExprKind::NumberLiteral:
            return make_expr(builtin_type("Int"));
        case ast::ExprKind::StringLiteral:
            return make_expr(builtin_type("Text"));
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
        case ast::ExprKind::Grant:
            return type_grant_expr(static_cast<const ast::GrantExpr&>(expr), context, env);
        case ast::ExprKind::Prove:
            return type_prove_expr(static_cast<const ast::ProveExpr&>(expr), context, env);
        case ast::ExprKind::FieldAccess:
            return type_field_access_expr(static_cast<const ast::FieldAccessExpr&>(expr), context, env);
        }
        return make_expr(error_type());
    }

    void check_generic_function_instantiation(
        const ast::FunctionDecl& function_decl,
        const Scope& scope,
        const std::vector<std::pair<std::string, Type>>& substitutions) {
        if (function_decl.signature.generic_params.empty() || function_decl.body == nullptr || substitutions.empty()) {
            return;
        }

        const std::string key = generic_function_instantiation_key(function_decl, substitutions);
        if (checked_generic_function_instantiations_.contains(key)
            || active_generic_function_instantiations_.contains(key)) {
            return;
        }

        active_generic_function_instantiations_.insert(key);
        analyze_function_body(function_decl, scope, substitutions);
        active_generic_function_instantiations_.erase(key);
        checked_generic_function_instantiations_.insert(key);
    }

    void analyze_function_body(
        const ast::FunctionDecl& function_decl,
        const Scope& scope,
        std::vector<std::pair<std::string, Type>> substitutions = {}) {
        FunctionContext context{scope, function_decl, {}, std::move(substitutions), error_type(), nullptr, {}};
        for (const ast::GenericParam& generic : function_decl.signature.generic_params) {
            context.generics.push_back(generic.name);
        }
        context.return_type = resolve_type(scope,
                                           context.generics,
                                           context.substitutions,
                                           function_decl.signature.return_type);
        if (function_decl.signature.fails_type.has_value()) {
            const Type fails_type = resolve_type(scope,
                                                 context.generics,
                                                 context.substitutions,
                                                 *function_decl.signature.fails_type);
            context.fails_reason = fails_type.decl != nullptr ? static_cast<const ast::ReasonDecl*>(fails_type.decl) : nullptr;
        }
        for (const ast::TypeRef& proves_ast : function_decl.signature.proves_types) {
            const Type proves_type = resolve_type(scope, context.generics, context.substitutions, proves_ast);
            if (proves_type.decl != nullptr && proves_type.decl->kind == ast::DeclKind::Proof) {
                context.proves_proofs.push_back(static_cast<const ast::ProofDecl*>(proves_type.decl));
            }
        }

        ValueEnv env = make_root_env();
        for (const ast::Parameter& param : function_decl.signature.params) {
            bind_value(env,
                       param.name,
                       resolve_type(scope, context.generics, context.substitutions, param.type),
                       param.span);
        }

        ExprType body_type = type_block(*function_decl.body, context, env);
        if (body_type.yielded_reason != nullptr) {
            diagnostics_.error(function_decl.body->span,
                                "failing expression must be handled with `try` or `match`");
        }
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

void SemanticAnalyzer::analyze(const ast::TranslationUnit& unit, const SourceFile& source) {
    Analyzer analyzer(diagnostics_, source);
    analyzer.analyze(unit);
}

} // namespace evident
