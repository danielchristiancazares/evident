#include "evident/ResolvedType.hpp"

#include <sstream>
#include <utility>

namespace evident::typesys {

Type error_type() {
    return Type{TypeFlavor::Error, "<error>", nullptr, {}};
}

Type builtin_type(std::string name, std::vector<Type> args) {
    return Type{TypeFlavor::Builtin, std::move(name), nullptr, std::move(args)};
}

Type generic_type(std::string name) {
    return Type{TypeFlavor::Generic, std::move(name), nullptr, {}};
}

Type named_type(std::string name, const ast::Decl* decl, std::vector<Type> args) {
    return Type{TypeFlavor::Named, std::move(name), decl, std::move(args)};
}

TypeErrorState type_error_state(const Type& type) {
    if (type.flavor == TypeFlavor::Error) {
        return TypeErrorState::SuppressesFollowupDiagnostics;
    }
    return TypeErrorState::CarriesTypeFacts;
}

NeverTypeState never_type_state(const Type& type) {
    if (type.flavor == TypeFlavor::Builtin && type.name == "Never" && type.args.empty()) {
        return NeverTypeState::DivergesBeforeFollowingCode;
    }
    return NeverTypeState::ProducesValue;
}

TypeEquivalence type_equivalence(const Type& lhs, const Type& rhs) {
    if (type_error_state(lhs) == TypeErrorState::SuppressesFollowupDiagnostics
        || type_error_state(rhs) == TypeErrorState::SuppressesFollowupDiagnostics) {
        return TypeEquivalence::Equivalent;
    }
    if (lhs.flavor != rhs.flavor) {
        return TypeEquivalence::Different;
    }
    if (lhs.name != rhs.name || lhs.decl != rhs.decl || lhs.args.size() != rhs.args.size()) {
        return TypeEquivalence::Different;
    }
    for (std::size_t index = 0; index < lhs.args.size(); ++index) {
        if (type_equivalence(lhs.args[index], rhs.args[index]) == TypeEquivalence::Different) {
            return TypeEquivalence::Different;
        }
    }
    return TypeEquivalence::Equivalent;
}

std::string type_name(const Type& type) {
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

DisciplineMovement discipline_movement(UseDiscipline discipline) {
    if (discipline == UseDiscipline::Affine) {
        return DisciplineMovement::Affine;
    }
    return DisciplineMovement::Copyable;
}

DisciplineMaterialization discipline_materialization(UseDiscipline discipline) {
    if (discipline == UseDiscipline::ScopedAuthority) {
        return DisciplineMaterialization::CompileTimeOnly;
    }
    return DisciplineMaterialization::RuntimeMaterialized;
}

UseDiscipline merge_discipline(UseDiscipline lhs, UseDiscipline rhs) {
    if (lhs == UseDiscipline::ScopedAuthority || rhs == UseDiscipline::ScopedAuthority) {
        return UseDiscipline::ScopedAuthority;
    }
    if (lhs == UseDiscipline::Affine || rhs == UseDiscipline::Affine) {
        return UseDiscipline::Affine;
    }
    return UseDiscipline::Copyable;
}

DisciplineClassifier::DisciplineClassifier(ResolveTypeRefFn resolve_type_ref)
    : resolve_type_ref_(std::move(resolve_type_ref)) {}

UseDiscipline DisciplineClassifier::classify(const Type& type) const {
    if (type_error_state(type) == TypeErrorState::SuppressesFollowupDiagnostics) {
        return UseDiscipline::Copyable;
    }

    const std::string key = cache_key(type);
    if (const auto cached = cache_.find(key); cached != cache_.end()) {
        return cached->second;
    }
    if (!active_.insert(key).second) {
        return UseDiscipline::Copyable;
    }

    const UseDiscipline discipline = classify_impl(type);
    active_.erase(key);
    cache_[key] = discipline;
    return discipline;
}

UseDiscipline DisciplineClassifier::classify_impl(const Type& type) const {
    if (type_error_state(type) == TypeErrorState::SuppressesFollowupDiagnostics) {
        return UseDiscipline::Copyable;
    }

    UseDiscipline discipline = UseDiscipline::Copyable;
    if (type.flavor == TypeFlavor::Builtin) {
        for (const Type& arg : type.args) {
            discipline = merge_discipline(discipline, classify(arg));
        }
        return discipline;
    }
    if (type.flavor == TypeFlavor::Generic || type.decl == nullptr) {
        return UseDiscipline::Copyable;
    }

    switch (type.decl->kind) {
    case ast::DeclKind::Proof:
        return UseDiscipline::Affine;
    case ast::DeclKind::Permit:
        return UseDiscipline::ScopedAuthority;
    case ast::DeclKind::Phase:
        return UseDiscipline::Affine;
    case ast::DeclKind::Record: {
        const auto* record_decl = static_cast<const ast::RecordDecl*>(type.decl);
        for (const ast::Field& field : record_decl->fields) {
            discipline = merge_discipline(discipline, classify(resolve_type_ref_(type.decl, type.args, field.type)));
        }
        return discipline;
    }
    case ast::DeclKind::State: {
        const auto* state_decl = static_cast<const ast::StateDecl*>(type.decl);
        for (const ast::Variant& variant : state_decl->variants) {
            for (const ast::Field& field : variant.fields) {
                discipline = merge_discipline(discipline, classify(resolve_type_ref_(type.decl, type.args, field.type)));
            }
        }
        return discipline;
    }
    case ast::DeclKind::Reason: {
        const auto* reason_decl = static_cast<const ast::ReasonDecl*>(type.decl);
        for (const ast::Variant& variant : reason_decl->variants) {
            for (const ast::Field& field : variant.fields) {
                discipline = merge_discipline(discipline, classify(resolve_type_ref_(type.decl, type.args, field.type)));
            }
        }
        return discipline;
    }
    case ast::DeclKind::Module:
    case ast::DeclKind::Function:
    case ast::DeclKind::ForeignFunction:
        return UseDiscipline::Copyable;
    }

    return UseDiscipline::Copyable;
}

std::string DisciplineClassifier::cache_key(const Type& type) const {
    std::ostringstream out;
    switch (type.flavor) {
    case TypeFlavor::Error:
        out << "E";
        break;
    case TypeFlavor::Builtin:
        out << "B:" << type.name;
        break;
    case TypeFlavor::Generic:
        out << "G:" << type.name;
        break;
    case TypeFlavor::Named:
        out << "N:" << type.name;
        break;
    }
    out << '[';
    for (std::size_t index = 0; index < type.args.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << cache_key(type.args[index]);
    }
    out << ']';
    return out.str();
}

} // namespace evident::typesys
