#include "evident/Hir.hpp"

#include <sstream>
#include <utility>

namespace evident::hir {

namespace {

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

void lower_decls(const std::vector<std::unique_ptr<ast::Decl>>& decls,
                 const std::string& prefix,
                 Package& package) {
    for (const auto& decl_ptr : decls) {
        const ast::Decl& decl = *decl_ptr;
        const std::string qualified = prefix.empty() ? decl.name : prefix + "::" + decl.name;

        switch (decl.kind) {
        case ast::DeclKind::Module: {
            const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
            lower_decls(module_decl.members, qualified, package);
            break;
        }
        case ast::DeclKind::Struct: {
            const auto& struct_decl = static_cast<const ast::StructDecl&>(decl);
            TypeDecl type{TypeKind::Struct, decl.visibility, qualified, {}, {}};
            for (const ast::GenericParam& param : struct_decl.generic_params) {
                type.generics.push_back(param.name);
            }
            for (const ast::Field& field : struct_decl.fields) {
                type.members.push_back(field.name + ": " + ast::format_type(field.type));
            }
            package.types.push_back(std::move(type));
            break;
        }
        case ast::DeclKind::State: {
            const auto& state_decl = static_cast<const ast::StateDecl&>(decl);
            TypeDecl type{TypeKind::State, decl.visibility, qualified, {}, {}};
            for (const ast::GenericParam& param : state_decl.generic_params) {
                type.generics.push_back(param.name);
            }
            for (const ast::Variant& variant : state_decl.variants) {
                std::string line = variant.name;
                if (!variant.fields.empty()) {
                    line += " { ";
                    for (std::size_t i = 0; i < variant.fields.size(); ++i) {
                        if (i > 0) {
                            line += ", ";
                        }
                        line += variant.fields[i].name + ": " + ast::format_type(variant.fields[i].type);
                    }
                    line += " }";
                }
                type.members.push_back(std::move(line));
            }
            package.types.push_back(std::move(type));
            break;
        }
        case ast::DeclKind::Reason: {
            const auto& reason_decl = static_cast<const ast::ReasonDecl&>(decl);
            TypeDecl type{TypeKind::Reason, decl.visibility, qualified, {}, {}};
            for (const ast::Variant& variant : reason_decl.variants) {
                std::string line = variant.name;
                if (!variant.fields.empty()) {
                    line += " { ";
                    for (std::size_t i = 0; i < variant.fields.size(); ++i) {
                        if (i > 0) {
                            line += ", ";
                        }
                        line += variant.fields[i].name + ": " + ast::format_type(variant.fields[i].type);
                    }
                    line += " }";
                }
                type.members.push_back(std::move(line));
            }
            package.types.push_back(std::move(type));
            break;
        }
        case ast::DeclKind::Proof: {
            const auto& proof_decl = static_cast<const ast::ProofDecl&>(decl);
            TypeDecl type{TypeKind::Proof, decl.visibility, qualified, {}, {}};
            for (const ast::Field& field : proof_decl.fields) {
                type.members.push_back(field.name + ": " + ast::format_type(field.type));
            }
            package.types.push_back(std::move(type));
            break;
        }
        case ast::DeclKind::Permit: {
            package.types.push_back(TypeDecl{TypeKind::Permit, decl.visibility, qualified, {}, {}});
            break;
        }
        case ast::DeclKind::Trait: {
            const auto& trait_decl = static_cast<const ast::TraitDecl&>(decl);
            TypeDecl type{TypeKind::Trait, decl.visibility, qualified, {}, {}};
            for (const ast::GenericParam& param : trait_decl.generic_params) {
                type.generics.push_back(param.name);
            }
            for (const ast::FunctionSignature& method : trait_decl.methods) {
                std::string line = method.name + "(";
                for (std::size_t i = 0; i < method.params.size(); ++i) {
                    if (i > 0) {
                        line += ", ";
                    }
                    line += method.params[i].name + ": " + ast::format_type(method.params[i].type);
                }
                line += ") -> " + ast::format_type(method.return_type);
                if (method.yields_type.has_value()) {
                    line += " yields " + ast::format_type(*method.yields_type);
                }
                type.members.push_back(std::move(line));
            }
            package.types.push_back(std::move(type));
            break;
        }
        case ast::DeclKind::Function:
        case ast::DeclKind::ForeignFunction: {
            const auto& function_decl = static_cast<const ast::FunctionDecl&>(decl);
            FunctionDecl fn;
            fn.visibility = function_decl.visibility;
            fn.qualified_name = qualified;
            fn.return_type = ast::format_type(function_decl.signature.return_type);
            fn.is_foreign = function_decl.is_foreign();
            fn.has_body = function_decl.body_span.has_value();
            for (const ast::GenericParam& param : function_decl.signature.generic_params) {
                fn.generics.push_back(param.name);
            }
            for (const ast::Parameter& param : function_decl.signature.params) {
                fn.params.push_back(param.name + ": " + ast::format_type(param.type));
            }
            if (function_decl.signature.yields_type.has_value()) {
                fn.yields_type = ast::format_type(*function_decl.signature.yields_type);
            }
            package.functions.push_back(std::move(fn));
            break;
        }
        }
    }
}

} // namespace

Package lower(const ast::TranslationUnit& unit) {
    Package package;
    lower_decls(unit.decls, "", package);
    return package;
}

std::string dump(const Package& package) {
    std::ostringstream out;
    out << "types:\n";
    for (const TypeDecl& type : package.types) {
        out << "  - " << ast::visibility_name(type.visibility) << ' ' << type_kind_name(type.kind) << ' ' << type.qualified_name << '\n';
        for (const std::string& member : type.members) {
            out << "      * " << member << '\n';
        }
    }
    out << "functions:\n";
    for (const FunctionDecl& fn : package.functions) {
        out << "  - " << ast::visibility_name(fn.visibility) << ' ' << (fn.is_foreign ? "foreign-fn " : "fn ") << fn.qualified_name << '(';
        for (std::size_t i = 0; i < fn.params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << fn.params[i];
        }
        out << ") -> " << fn.return_type;
        if (fn.yields_type.has_value()) {
            out << " yields " << *fn.yields_type;
        }
        if (fn.has_body) {
            out << " [body]";
        }
        out << '\n';
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
    for (const FunctionDecl& fn : package.functions) {
        out << "- " << fn.qualified_name << " -> " << fn.return_type;
        if (fn.yields_type.has_value()) {
            out << " yields " << *fn.yields_type;
        }
        if (fn.is_foreign) {
            out << " [foreign]";
        }
        out << '\n';
    }
    out << "\n## Next stages\n";
    out << "- parse function bodies into expressions and patterns\n";
    out << "- introduce symbol-qualified HIR IDs\n";
    out << "- add type checking for match exhaustiveness and typestate transitions\n";
    out << "- choose a backend: LLVM IR, C, or a custom bytecode\n";
    return out.str();
}

} // namespace evident::hir
