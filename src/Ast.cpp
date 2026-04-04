#include "evident/Ast.hpp"

#include <sstream>
#include <utility>

namespace evident::ast {

namespace {

void indent(std::ostream& out, std::size_t depth) {
    for (std::size_t i = 0; i < depth; ++i) {
        out << "  ";
    }
}

void dump_expr(std::ostream& out, const Expr& expr, std::size_t depth);
void dump_pattern(std::ostream& out, const Pattern& pattern, std::size_t depth);

void dump_type(std::ostream& out, const TypeRef& type) {
    out << format_type(type);
}

void dump_fields(std::ostream& out, const std::vector<Field>& fields, std::size_t depth) {
    for (const Field& field : fields) {
        indent(out, depth);
        if (field.visibility == Visibility::Public) {
            out << "public ";
        }
        out << "field " << field.name << ": ";
        dump_type(out, field.type);
        out << '\n';
    }
}

void dump_record_fields(std::ostream& out, const std::vector<RecordFieldInit>& fields, std::size_t depth) {
    for (const RecordFieldInit& field : fields) {
        indent(out, depth);
        out << "init " << field.name;
        if (field.shorthand) {
            out << " [shorthand]";
        }
        out << '\n';
        if (field.value != nullptr) {
            dump_expr(out, *field.value, depth + 1);
        }
    }
}

void dump_stmt(std::ostream& out, const Stmt& stmt, std::size_t depth) {
    switch (stmt.kind) {
    case StmtKind::Let: {
        const auto& let_stmt = static_cast<const LetStmt&>(stmt);
        indent(out, depth);
        out << "let " << let_stmt.name << '\n';
        if (let_stmt.initializer != nullptr) {
            dump_expr(out, *let_stmt.initializer, depth + 1);
        }
        break;
    }
    case StmtKind::Expr: {
        const auto& expr_stmt = static_cast<const ExprStmt&>(stmt);
        indent(out, depth);
        out << "expr-stmt\n";
        if (expr_stmt.expr != nullptr) {
            dump_expr(out, *expr_stmt.expr, depth + 1);
        }
        break;
    }
    }
}

void dump_expr(std::ostream& out, const Expr& expr, std::size_t depth) {
    indent(out, depth);
    switch (expr.kind) {
    case ExprKind::NumberLiteral: {
        const auto& literal = static_cast<const NumberLiteralExpr&>(expr);
        out << "number " << literal.lexeme << '\n';
        break;
    }
    case ExprKind::StringLiteral: {
        const auto& literal = static_cast<const StringLiteralExpr&>(expr);
        out << "string " << literal.lexeme << '\n';
        break;
    }
    case ExprKind::Path: {
        const auto& path_expr = static_cast<const PathExpr&>(expr);
        out << "path ";
        for (std::size_t i = 0; i < path_expr.path.size(); ++i) {
            if (i > 0) {
                out << "::";
            }
            out << path_expr.path[i];
        }
        out << '\n';
        break;
    }
    case ExprKind::Call: {
        const auto& call = static_cast<const CallExpr&>(expr);
        out << "call\n";
        if (call.callee != nullptr) {
            dump_expr(out, *call.callee, depth + 1);
        }
        for (const auto& arg : call.args) {
            dump_expr(out, *arg, depth + 1);
        }
        break;
    }
    case ExprKind::Construct: {
        const auto& construct = static_cast<const ConstructExpr&>(expr);
        out << "construct ";
        for (std::size_t i = 0; i < construct.path.size(); ++i) {
            if (i > 0) {
                out << "::";
            }
            out << construct.path[i];
        }
        out << '\n';
        dump_record_fields(out, construct.fields, depth + 1);
        break;
    }
    case ExprKind::Try: {
        const auto& try_expr = static_cast<const TryExpr&>(expr);
        out << "try\n";
        if (try_expr.operand != nullptr) {
            dump_expr(out, *try_expr.operand, depth + 1);
        }
        break;
    }
    case ExprKind::Match: {
        const auto& match_expr = static_cast<const MatchExpr&>(expr);
        out << "match\n";
        if (match_expr.scrutinee != nullptr) {
            indent(out, depth + 1);
            out << "scrutinee\n";
            dump_expr(out, *match_expr.scrutinee, depth + 2);
        }
        for (const MatchArm& arm : match_expr.arms) {
            indent(out, depth + 1);
            out << "arm\n";
            if (arm.pattern != nullptr) {
                dump_pattern(out, *arm.pattern, depth + 2);
            }
            if (arm.body != nullptr) {
                dump_expr(out, *arm.body, depth + 2);
            }
        }
        break;
    }
    case ExprKind::Block: {
        const auto& block = static_cast<const BlockExpr&>(expr);
        out << "block\n";
        for (const auto& stmt : block.statements) {
            dump_stmt(out, *stmt, depth + 1);
        }
        if (block.result != nullptr) {
            indent(out, depth + 1);
            out << "result\n";
            dump_expr(out, *block.result, depth + 2);
        }
        break;
    }
    case ExprKind::Fail: {
        const auto& fail_expr = static_cast<const FailExpr&>(expr);
        out << "fail ";
        for (std::size_t i = 0; i < fail_expr.path.size(); ++i) {
            if (i > 0) {
                out << "::";
            }
            out << fail_expr.path[i];
        }
        out << '\n';
        dump_record_fields(out, fail_expr.fields, depth + 1);
        break;
    }
    case ExprKind::Grant: {
        const auto& grant_expr = static_cast<const GrantExpr&>(expr);
        out << "grant " << grant_expr.binder_name << '\n';
        if (grant_expr.grant_call != nullptr) {
            indent(out, depth + 1);
            out << "grantor\n";
            dump_expr(out, *grant_expr.grant_call, depth + 2);
        }
        if (grant_expr.body != nullptr) {
            indent(out, depth + 1);
            out << "body\n";
            dump_expr(out, *grant_expr.body, depth + 2);
        }
        break;
    }
    case ExprKind::Prove: {
        const auto& prove_expr = static_cast<const ProveExpr&>(expr);
        out << "prove ";
        for (std::size_t i = 0; i < prove_expr.path.size(); ++i) {
            if (i > 0) {
                out << "::";
            }
            out << prove_expr.path[i];
        }
        out << '\n';
        dump_record_fields(out, prove_expr.fields, depth + 1);
        break;
    }
    case ExprKind::FieldAccess: {
        const auto& fa = static_cast<const FieldAccessExpr&>(expr);
        out << "field-access ." << fa.field_name << '\n';
        if (fa.object != nullptr) {
            dump_expr(out, *fa.object, depth + 1);
        }
        break;
    }
    }
}

void dump_pattern(std::ostream& out, const Pattern& pattern, std::size_t depth) {
    indent(out, depth);
    switch (pattern.kind) {
    case PatternKind::Variant: {
        const auto& variant = static_cast<const VariantPattern&>(pattern);
        out << "pattern ";
        for (std::size_t i = 0; i < variant.path.size(); ++i) {
            if (i > 0) {
                out << "::";
            }
            out << variant.path[i];
        }
        out << '\n';
        if (variant.payload_mode == VariantPattern::PayloadMode::Ignore) {
            indent(out, depth + 1);
            out << "payload { .. }\n";
        } else {
            for (const auto& binding : variant.bindings) {
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
        const auto& failed = static_cast<const FailedPattern&>(pattern);
        out << "pattern failed\n";
        if (failed.variant != nullptr) {
            dump_pattern(out, *failed.variant, depth + 1);
        }
        break;
    }
    }
}

void dump_decl(std::ostream& out, const Decl& decl, std::size_t depth) {
    indent(out, depth);
    out << visibility_name(decl.visibility) << ' ' << decl_kind_name(decl.kind) << ' ' << decl.name << '\n';

    switch (decl.kind) {
    case DeclKind::Module: {
        const auto& module = static_cast<const ModuleDecl&>(decl);
        indent(out, depth + 1);
        out << "kind " << module_kind_name(module.module_kind) << '\n';
        for (const auto& member : module.members) {
            dump_decl(out, *member, depth + 1);
        }
        break;
    }
    case DeclKind::Record: {
        const auto& record_decl = static_cast<const RecordDecl&>(decl);
        dump_fields(out, record_decl.fields, depth + 1);
        break;
    }
    case DeclKind::State: {
        const auto& state_decl = static_cast<const StateDecl&>(decl);
        for (const Variant& variant : state_decl.variants) {
            indent(out, depth + 1);
            out << "variant " << variant.name << '\n';
            dump_fields(out, variant.fields, depth + 2);
        }
        break;
    }
    case DeclKind::Reason: {
        const auto& reason_decl = static_cast<const ReasonDecl&>(decl);
        for (const Variant& variant : reason_decl.variants) {
            indent(out, depth + 1);
            out << "variant " << variant.name << '\n';
            dump_fields(out, variant.fields, depth + 2);
        }
        break;
    }
    case DeclKind::Proof: {
        const auto& proof_decl = static_cast<const ProofDecl&>(decl);
        dump_fields(out, proof_decl.fields, depth + 1);
        break;
    }
    case DeclKind::Permit:
        break;
    case DeclKind::Phase: {
        const auto& phase_decl = static_cast<const PhaseDecl&>(decl);
        indent(out, depth + 1);
        out << "fields\n";
        dump_fields(out, phase_decl.fields, depth + 2);
        indent(out, depth + 1);
        out << "positions";
        for (const std::string& pos : phase_decl.positions) {
            out << ' ' << pos;
        }
        out << '\n';
        break;
    }
    case DeclKind::Function:
    case DeclKind::ForeignFunction: {
        const auto& fn_decl = static_cast<const FunctionDecl&>(decl);
        indent(out, depth + 1);
        out << "signature " << fn_decl.signature.name << '(';
        for (std::size_t i = 0; i < fn_decl.signature.params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            if (fn_decl.signature.params[i].is_permit_param) {
                out << "as ";
            }
            out << fn_decl.signature.params[i].name << ": " << format_type(fn_decl.signature.params[i].type);
        }
        out << ") -> " << format_type(fn_decl.signature.return_type);
        if (fn_decl.signature.fails_type.has_value()) {
            out << " fails " << format_type(*fn_decl.signature.fails_type);
        }
        if (fn_decl.signature.grants_type.has_value()) {
            out << " grants " << format_type(*fn_decl.signature.grants_type);
        }
        for (const auto& proves : fn_decl.signature.proves_types) {
            out << " proves " << format_type(proves);
        }
        out << '\n';
        if (fn_decl.body != nullptr) {
            dump_expr(out, *fn_decl.body, depth + 1);
        }
        break;
    }
    }
}

} // namespace

std::string_view decl_kind_name(DeclKind kind) {
    switch (kind) {
    case DeclKind::Module:
        return "module";
    case DeclKind::Record:
        return "record";
    case DeclKind::State:
        return "state";
    case DeclKind::Reason:
        return "reason";
    case DeclKind::Proof:
        return "proof";
    case DeclKind::Permit:
        return "permit";
    case DeclKind::Phase:
        return "phase";
    case DeclKind::Function:
        return "function";
    case DeclKind::ForeignFunction:
        return "foreign-function";
    }
    return "decl";
}

std::string_view module_kind_name(ModuleKind kind) {
    switch (kind) {
    case ModuleKind::Domain:
        return "domain";
    case ModuleKind::Boundary:
        return "boundary";
    case ModuleKind::Foundation:
        return "foundation";
    case ModuleKind::Hazard:
        return "hazard";
    }
    return "unknown";
}

std::string visibility_name(Visibility visibility) {
    return visibility == Visibility::Public ? "public" : "private";
}

std::string format_type(const TypeRef& type) {
    std::ostringstream out;
    for (std::size_t i = 0; i < type.path.size(); ++i) {
        if (i > 0) {
            out << "::";
        }
        out << type.path[i];
    }
    if (!type.args.empty()) {
        out << '<';
        for (std::size_t i = 0; i < type.args.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << format_type(type.args[i]);
        }
        out << '>';
    }
    return out.str();
}

std::string dump(const TranslationUnit& unit, std::string_view) {
    std::ostringstream out;
    for (const auto& decl : unit.decls) {
        dump_decl(out, *decl, 0);
    }
    return out.str();
}

} // namespace evident::ast
