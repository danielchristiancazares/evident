#include "evident/Mir.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace evident::mir {

namespace {

struct Env {
    Env* parent = nullptr;
    std::unordered_map<std::string, LocalId> locals;
};

struct Builder {
    using MaybeBlock = std::optional<BlockId>;

    const hir::Package& hir_package;
    Function function;
    LocalId return_local = 0;

    explicit Builder(const hir::Package& package, const hir::FunctionDecl& source)
        : hir_package(package) {
        function.function_id = source.id;
        function.visibility = source.visibility;
        function.qualified_name = source.qualified_name;
        function.return_type = source.return_type.text;
        if (source.fails_reason_type_id.has_value()) {
            function.fails_type = hir::lookup_type(package, *source.fails_reason_type_id).qualified_name;
        }
        if (source.grants_permit_type_id.has_value()) {
            function.grants_type = hir::lookup_type(package, *source.grants_permit_type_id).qualified_name;
        }
        for (hir::TypeId proof_id : source.proves_proof_type_ids) {
            function.proves_types.push_back(hir::lookup_type(package, proof_id).qualified_name);
        }
        function.is_foreign = source.is_foreign;
    }

    [[nodiscard]] Function lower(const hir::FunctionDecl& source);

private:
    [[nodiscard]] LocalId add_local(std::string name,
                                    std::string type,
                                    LocalKind kind,
                                    bool is_compile_time_only = false,
                                    bool is_affine = false);
    [[nodiscard]] BlockId add_block();
    [[nodiscard]] BasicBlock& block(BlockId id);
    void append_statement(BlockId id, Statement statement);
    void set_terminator(BlockId id, Terminator terminator);
    [[nodiscard]] Operand local_operand(LocalId local_id) const;
    [[nodiscard]] Operand unit_operand() const;
    [[nodiscard]] LocalId lookup_local(const Env& env, const std::string& name) const;
    [[nodiscard]] LocalId make_temp(std::string type,
                                    typesys::UseDiscipline discipline = typesys::UseDiscipline::Copyable,
                                    std::string prefix = "tmp");
    [[nodiscard]] MaybeBlock lower_block_expr(const hir::BlockExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_expr_to(const hir::Expr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_value(const hir::Expr& expr, BlockId current, Env& env, Operand& out);
    [[nodiscard]] MaybeBlock lower_construct_expr(const hir::ConstructExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_call_rvalue(const hir::CallExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_direct_call(hir::FunctionId function_id,
                                               const std::string& callee_name,
                                               const std::vector<std::unique_ptr<hir::Expr>>& args,
                                               LocalId dest,
                                               BlockId current,
                                               Env& env);
    [[nodiscard]] MaybeBlock lower_try_expr(const hir::TryExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_match_expr(const hir::MatchExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_state_match(const hir::MatchExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_yield_match(const hir::MatchExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_grant_expr(const hir::GrantExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_field_access_expr(const hir::FieldAccessExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock lower_prove_expr(const hir::ProveExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] MaybeBlock ensure_local_operand(Operand operand,
                                                  std::string type,
                                                  typesys::UseDiscipline discipline,
                                                  BlockId current,
                                                  LocalId& out_local);
    void bind_variant_pattern(const hir::VariantPattern& pattern,
                              const hir::VariantDecl& variant,
                              LocalId source_local,
                              BlockId block_id,
                              Env& env);
};

std::string format_local_name(const Local& local) {
    std::ostringstream out;
    out << '%' << local.id << '[' << local.name << ']';
    return out.str();
}

std::string format_operand(const Operand& operand, const Function& function) {
    switch (operand.kind) {
    case OperandKind::Local:
        return format_local_name(function.locals.at(operand.local_id));
    case OperandKind::IntLiteral:
    case OperandKind::StringLiteral:
        return operand.text;
    case OperandKind::Unit:
        return "unit";
    }
    return "<operand>";
}

std::string format_rvalue(const Rvalue& value, const Function& function) {
    std::ostringstream out;
    switch (value.kind) {
    case RvalueKind::Use:
        return format_operand(value.operand, function);
    case RvalueKind::Call:
        out << "call " << value.callee_name << '(';
        for (std::size_t index = 0; index < value.args.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            out << format_operand(value.args[index], function);
        }
        out << ')';
        return out.str();
    case RvalueKind::Construct:
        out << "construct " << value.qualified_name << " {";
        for (std::size_t index = 0; index < value.fields.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            out << value.fields[index].name << ": " << format_operand(value.fields[index].value, function);
        }
        out << '}';
        return out.str();
    case RvalueKind::ProjectField:
        out << format_local_name(function.locals.at(value.base_local)) << '.' << value.field_name;
        return out.str();
    }
    return "<rvalue>";
}

Function Builder::lower(const hir::FunctionDecl& source) {
    Env env{nullptr, {}};
    for (const hir::Parameter& param : source.params) {
        LocalId local = add_local(param.name,
                                  param.type.text,
                                  LocalKind::Parameter,
                                  param.is_compile_time_only,
                                  typesys::is_affine(param.type.discipline));
        env.locals.emplace(param.name, local);
    }
    if (source.is_foreign) {
        return std::move(function);
    }

    return_local = add_local("$return",
                             source.return_type.text,
                             LocalKind::ReturnSlot,
                             false,
                             typesys::is_affine(source.return_type.discipline));
    BlockId entry = add_block();
    MaybeBlock tail = lower_block_expr(*source.body, return_local, entry, env);
    if (tail.has_value()) {
        Terminator terminator;
        terminator.kind = TerminatorKind::Return;
        terminator.value = local_operand(return_local);
        set_terminator(*tail, std::move(terminator));
    }
    return std::move(function);
}

LocalId Builder::add_local(std::string name,
                           std::string type,
                           LocalKind kind,
                           bool is_compile_time_only,
                           bool is_affine) {
    function.locals.push_back(Local{
        function.locals.size(),
        kind,
        std::move(name),
        std::move(type),
        is_compile_time_only,
        is_affine,
    });
    return function.locals.back().id;
}

BlockId Builder::add_block() {
    function.blocks.push_back(BasicBlock{function.blocks.size(), {}, {}, false});
    return function.blocks.back().id;
}

BasicBlock& Builder::block(BlockId id) {
    return function.blocks.at(id);
}

void Builder::append_statement(BlockId id, Statement statement) {
    block(id).statements.push_back(std::move(statement));
}

void Builder::set_terminator(BlockId id, Terminator terminator) {
    BasicBlock& target = block(id);
    target.terminator = std::move(terminator);
    target.has_terminator = true;
}

Operand Builder::local_operand(LocalId local_id) const {
    return Operand{OperandKind::Local, local_id, {}};
}

Operand Builder::unit_operand() const {
    return Operand{OperandKind::Unit, 0, {}};
}

LocalId Builder::lookup_local(const Env& env, const std::string& name) const {
    for (const Env* current = &env; current != nullptr; current = current->parent) {
        const auto it = current->locals.find(name);
        if (it != current->locals.end()) {
            return it->second;
        }
    }
    return 0;
}

LocalId Builder::make_temp(std::string type, typesys::UseDiscipline discipline, std::string prefix) {
    return add_local(std::move(prefix),
                     std::move(type),
                     LocalKind::Temporary,
                     typesys::is_compile_time_only(discipline),
                     typesys::is_affine(discipline));
}

Builder::MaybeBlock Builder::lower_block_expr(const hir::BlockExpr& expr,
                                              LocalId dest,
                                              BlockId current,
                                              Env& env) {
    Env local{&env, {}};
    MaybeBlock cursor = current;

    for (const auto& stmt_ptr : expr.statements) {
        if (!cursor.has_value()) {
            return std::nullopt;
        }
        const hir::Stmt& stmt = *stmt_ptr;
        switch (stmt.kind) {
        case hir::StatementKind::Let: {
            const auto& let_stmt = static_cast<const hir::LetStmt&>(stmt);
            LocalId local_id = add_local(let_stmt.name,
                                         let_stmt.type.text,
                                         LocalKind::Let,
                                         false,
                                         typesys::is_affine(let_stmt.type.discipline));
            cursor = lower_expr_to(*let_stmt.initializer, local_id, *cursor, local);
            if (cursor.has_value()) {
                local.locals[let_stmt.name] = local_id;
            }
            break;
        }
        case hir::StatementKind::Expr: {
            const auto& expr_stmt = static_cast<const hir::ExprStmt&>(stmt);
            LocalId temp = make_temp(expr_stmt.expr->result_type.text.empty() ? "Unit" : expr_stmt.expr->result_type.text,
                                     expr_stmt.expr->result_type.discipline,
                                     "discard");
            cursor = lower_expr_to(*expr_stmt.expr, temp, *cursor, local);
            break;
        }
        }
    }

    if (!cursor.has_value()) {
        return std::nullopt;
    }
    return lower_expr_to(*expr.result, dest, *cursor, local);
}

Builder::MaybeBlock Builder::lower_value(const hir::Expr& expr,
                                         BlockId current,
                                         Env& env,
                                         Operand& out) {
    switch (expr.kind) {
    case hir::ExprKind::NumberLiteral:
        out = Operand{OperandKind::IntLiteral, 0, static_cast<const hir::NumberLiteralExpr&>(expr).lexeme};
        return current;
    case hir::ExprKind::StringLiteral:
        out = Operand{OperandKind::StringLiteral, 0, static_cast<const hir::StringLiteralExpr&>(expr).lexeme};
        return current;
    case hir::ExprKind::Unit:
        out = unit_operand();
        return current;
    case hir::ExprKind::LocalRef: {
        const auto& local_ref = static_cast<const hir::LocalRefExpr&>(expr);
        out = local_operand(lookup_local(env, local_ref.name));
        return current;
    }
    default:
        break;
    }

    LocalId temp = make_temp(expr.result_type.text.empty() ? "Unit" : expr.result_type.text, expr.result_type.discipline);
    MaybeBlock next = lower_expr_to(expr, temp, current, env);
    if (!next.has_value()) {
        return std::nullopt;
    }
    out = local_operand(temp);
    return next;
}

Builder::MaybeBlock Builder::ensure_local_operand(Operand operand,
                                                  std::string type,
                                                  typesys::UseDiscipline discipline,
                                                  BlockId current,
                                                  LocalId& out_local) {
    if (operand.kind == OperandKind::Local) {
        out_local = operand.local_id;
        return current;
    }

    out_local = make_temp(std::move(type), discipline, "materialize");
    Statement statement;
    statement.dest_local = out_local;
    statement.value.kind = RvalueKind::Use;
    statement.value.operand = std::move(operand);
    append_statement(current, std::move(statement));
    return current;
}

Builder::MaybeBlock Builder::lower_construct_expr(const hir::ConstructExpr& expr,
                                                  LocalId dest,
                                                  BlockId current,
                                                  Env& env) {
    Rvalue value;
    value.kind = RvalueKind::Construct;
    value.owner_type_id = expr.owner_type_id;
    value.variant_id = expr.variant_id;
    value.qualified_name = expr.qualified_name;

    MaybeBlock cursor = current;
    for (const hir::FieldInit& field : expr.fields) {
        Operand operand;
        cursor = lower_value(*field.value, *cursor, env, operand);
        if (!cursor.has_value()) {
            return std::nullopt;
        }
        value.fields.push_back(FieldValue{field.name, std::move(operand)});
    }

    append_statement(*cursor, Statement{StatementKind::Assign, dest, std::move(value)});
    return cursor;
}

Builder::MaybeBlock Builder::lower_call_rvalue(const hir::CallExpr& expr,
                                               LocalId dest,
                                               BlockId current,
                                               Env& env) {
    return lower_direct_call(expr.function_id, expr.callee_name, expr.args, dest, current, env);
}

Builder::MaybeBlock Builder::lower_direct_call(hir::FunctionId function_id,
                                               const std::string& callee_name,
                                               const std::vector<std::unique_ptr<hir::Expr>>& args,
                                               LocalId dest,
                                               BlockId current,
                                               Env& env) {
    Rvalue value;
    value.kind = RvalueKind::Call;
    value.function_id = function_id;
    value.callee_name = callee_name;

    const hir::FunctionDecl& callee = hir::lookup_function(hir_package, function_id);

    MaybeBlock cursor = current;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index < callee.params.size() && callee.params[index].is_compile_time_only) {
            value.args.push_back(unit_operand());
            continue;
        }
        Operand operand;
        cursor = lower_value(*args[index], *cursor, env, operand);
        if (!cursor.has_value()) {
            return std::nullopt;
        }
        value.args.push_back(std::move(operand));
    }

    append_statement(*cursor, Statement{StatementKind::Assign, dest, std::move(value)});
    return cursor;
}

Builder::MaybeBlock Builder::lower_try_expr(const hir::TryExpr& expr,
                                            LocalId dest,
                                            BlockId current,
                                            Env& env) {
    const auto* call = dynamic_cast<const hir::CallExpr*>(expr.operand.get());
    if (call == nullptr || !call->fails_reason_type_id.has_value()) {
        return lower_expr_to(*expr.operand, dest, current, env);
    }

    Terminator invoke;
    invoke.kind = TerminatorKind::Invoke;
    invoke.function_id = call->function_id;
    invoke.callee_name = call->callee_name;

    const hir::FunctionDecl& callee = hir::lookup_function(hir_package, call->function_id);

    MaybeBlock cursor = current;
    for (std::size_t index = 0; index < call->args.size(); ++index) {
        if (index < callee.params.size() && callee.params[index].is_compile_time_only) {
            invoke.args.push_back(unit_operand());
            continue;
        }
        Operand operand;
        cursor = lower_value(*call->args[index], *cursor, env, operand);
        if (!cursor.has_value()) {
            return std::nullopt;
        }
        invoke.args.push_back(std::move(operand));
    }

    const hir::TypeDecl& failure_type = hir::lookup_type(hir_package, *call->fails_reason_type_id);
    LocalId failure_local = make_temp(failure_type.qualified_name,
                                      failure_type.concrete_discipline.value_or(typesys::UseDiscipline::Copyable),
                                      "failure");
    invoke.success_local = dest;
    invoke.failure_local = failure_local;
    BlockId success_block = add_block();
    BlockId failure_block = add_block();
    invoke.success_block = success_block;
    invoke.failure_block = failure_block;
    set_terminator(*cursor, std::move(invoke));

    Terminator fail_term;
    fail_term.kind = TerminatorKind::Fail;
    fail_term.value = local_operand(failure_local);
    set_terminator(failure_block, std::move(fail_term));

    return success_block;
}

void Builder::bind_variant_pattern(const hir::VariantPattern& pattern,
                                   const hir::VariantDecl& variant,
                                   LocalId source_local,
                                   BlockId block_id,
                                   Env& env) {
    if (pattern.payload_mode != hir::VariantPattern::PayloadMode::Bindings) {
        return;
    }

    for (const hir::Binding& binding : pattern.bindings) {
        for (const hir::FieldDecl& field : variant.fields) {
            if (field.name != binding.field_name) {
                continue;
            }
            LocalId local = add_local(binding.binding_name,
                                      field.type.text,
                                      LocalKind::Let,
                                      false,
                                      typesys::is_affine(field.type.discipline));
            env.locals[binding.binding_name] = local;
            Rvalue value;
            value.kind = RvalueKind::ProjectField;
            value.base_local = source_local;
            value.projection_owner_type_id = variant.owner_type;
            value.projection_variant_id = variant.id;
            value.field_name = field.name;
            append_statement(block_id, Statement{StatementKind::Assign, local, std::move(value)});
            break;
        }
    }
}

Builder::MaybeBlock Builder::lower_state_match(const hir::MatchExpr& expr,
                                               LocalId dest,
                                               BlockId current,
                                               Env& env) {
    Operand operand;
    MaybeBlock cursor = lower_value(*expr.scrutinee, current, env, operand);
    if (!cursor.has_value()) {
        return std::nullopt;
    }

    LocalId scrutinee_local = 0;
    cursor = ensure_local_operand(std::move(operand),
                                  expr.scrutinee->result_type.text,
                                  expr.scrutinee->result_type.discipline,
                                  *cursor,
                                  scrutinee_local);
    if (!cursor.has_value()) {
        return std::nullopt;
    }

    Terminator switch_term;
    switch_term.kind = TerminatorKind::SwitchVariant;
    switch_term.scrutinee_local = scrutinee_local;

    std::optional<BlockId> join;
    for (const hir::MatchArm& arm : expr.arms) {
        const auto& pattern = static_cast<const hir::VariantPattern&>(*arm.pattern);
        const hir::VariantDecl& variant = hir::lookup_variant(hir_package, pattern.variant_id);
        BlockId arm_block = add_block();
        switch_term.edges.push_back(SwitchEdge{variant.id, variant.name, arm_block});

        Env arm_env{&env, {}};
        bind_variant_pattern(pattern, variant, scrutinee_local, arm_block, arm_env);

        MaybeBlock arm_tail = lower_expr_to(*arm.body, dest, arm_block, arm_env);
        if (arm_tail.has_value()) {
            if (!join.has_value()) {
                join = add_block();
            }
            Terminator goto_term;
            goto_term.kind = TerminatorKind::Goto;
            goto_term.target_block = *join;
            set_terminator(*arm_tail, std::move(goto_term));
        }
    }

    set_terminator(*cursor, std::move(switch_term));
    return join;
}

Builder::MaybeBlock Builder::lower_yield_match(const hir::MatchExpr& expr,
                                               LocalId dest,
                                               BlockId current,
                                               Env& env) {
    const auto* call = dynamic_cast<const hir::CallExpr*>(expr.scrutinee.get());
    if (call == nullptr || !call->fails_reason_type_id.has_value()) {
        return lower_state_match(expr, dest, current, env);
    }

    Terminator invoke;
    invoke.kind = TerminatorKind::Invoke;
    invoke.function_id = call->function_id;
    invoke.callee_name = call->callee_name;

    const hir::FunctionDecl& callee = hir::lookup_function(hir_package, call->function_id);

    MaybeBlock cursor = current;
    for (std::size_t index = 0; index < call->args.size(); ++index) {
        if (index < callee.params.size() && callee.params[index].is_compile_time_only) {
            invoke.args.push_back(unit_operand());
            continue;
        }
        Operand operand;
        cursor = lower_value(*call->args[index], *cursor, env, operand);
        if (!cursor.has_value()) {
            return std::nullopt;
        }
        invoke.args.push_back(std::move(operand));
    }

    LocalId success_local = make_temp(call->result_type.text, call->result_type.discipline, "success");
    const hir::TypeDecl& yielded_reason = hir::lookup_type(hir_package, *call->fails_reason_type_id);
    LocalId failure_local = make_temp(yielded_reason.qualified_name,
                                      yielded_reason.concrete_discipline.value_or(typesys::UseDiscipline::Copyable),
                                      "failure");
    invoke.success_local = success_local;
    invoke.failure_local = failure_local;

    const hir::MatchArm* success_arm = nullptr;
    std::vector<const hir::MatchArm*> failure_arms;
    for (const hir::MatchArm& arm : expr.arms) {
        if (!arm.pattern) {
            continue;
        }
        if (arm.pattern->kind == hir::PatternKind::Succeeded) {
            success_arm = &arm;
        } else if (arm.pattern->kind == hir::PatternKind::Failed) {
            failure_arms.push_back(&arm);
        }
    }

    BlockId success_block = add_block();
    BlockId failure_dispatch = add_block();
    invoke.success_block = success_block;
    invoke.failure_block = failure_dispatch;
    set_terminator(*cursor, std::move(invoke));

    std::optional<BlockId> join;
    if (success_arm != nullptr) {
        Env success_env{&env, {}};
        const auto& pattern = static_cast<const hir::SucceededPattern&>(*success_arm->pattern);
        if (pattern.binding_name.has_value()) {
            LocalId binding = add_local(*pattern.binding_name,
                                        call->result_type.text,
                                        LocalKind::Let,
                                        false,
                                        typesys::is_affine(call->result_type.discipline));
            success_env.locals[*pattern.binding_name] = binding;
            Rvalue copy;
            copy.kind = RvalueKind::Use;
            copy.operand = local_operand(success_local);
            append_statement(success_block, Statement{StatementKind::Assign, binding, std::move(copy)});
        }

        MaybeBlock success_tail = lower_expr_to(*success_arm->body, dest, success_block, success_env);
        if (success_tail.has_value()) {
            if (!join.has_value()) {
                join = add_block();
            }
            Terminator goto_term;
            goto_term.kind = TerminatorKind::Goto;
            goto_term.target_block = *join;
            set_terminator(*success_tail, std::move(goto_term));
        }
    }

    Terminator failure_switch;
    failure_switch.kind = TerminatorKind::SwitchVariant;
    failure_switch.scrutinee_local = failure_local;
    for (const hir::MatchArm* arm : failure_arms) {
        const auto& pattern = static_cast<const hir::FailedPattern&>(*arm->pattern);
        const hir::VariantDecl& variant = hir::lookup_variant(hir_package, pattern.variant->variant_id);
        BlockId arm_block = add_block();
        failure_switch.edges.push_back(SwitchEdge{variant.id, variant.name, arm_block});

        Env failure_env{&env, {}};
        bind_variant_pattern(*pattern.variant, variant, failure_local, arm_block, failure_env);

        MaybeBlock arm_tail = lower_expr_to(*arm->body, dest, arm_block, failure_env);
        if (arm_tail.has_value()) {
            if (!join.has_value()) {
                join = add_block();
            }
            Terminator goto_term;
            goto_term.kind = TerminatorKind::Goto;
            goto_term.target_block = *join;
            set_terminator(*arm_tail, std::move(goto_term));
        }
    }
    set_terminator(failure_dispatch, std::move(failure_switch));
    return join;
}

Builder::MaybeBlock Builder::lower_match_expr(const hir::MatchExpr& expr,
                                              LocalId dest,
                                              BlockId current,
                                              Env& env) {
    if (expr.scrutinee->fails_reason_type_id.has_value()) {
        return lower_yield_match(expr, dest, current, env);
    }
    return lower_state_match(expr, dest, current, env);
}

Builder::MaybeBlock Builder::lower_grant_expr(const hir::GrantExpr& expr,
                                                LocalId dest,
                                                BlockId current,
                                                Env& env) {
    LocalId grant_dest = make_temp("Unit", typesys::UseDiscipline::Copyable, "grant");
    MaybeBlock cursor = lower_direct_call(expr.grant_function_id, expr.grant_name, expr.args, grant_dest, current, env);
    if (!cursor.has_value()) {
        return std::nullopt;
    }

    Env scoped{&env, {}};
    const LocalId binder = add_local(expr.binder_name, expr.permit_name, LocalKind::Let, true, false);
    scoped.locals.emplace(expr.binder_name, binder);
    return lower_block_expr(*expr.body, dest, *cursor, scoped);
}

Builder::MaybeBlock Builder::lower_field_access_expr(const hir::FieldAccessExpr& expr,
                                                     LocalId dest,
                                                     BlockId current,
                                                     Env& env) {
    Operand base;
    MaybeBlock cursor = lower_value(*expr.object, current, env, base);
    if (!cursor.has_value()) {
        return std::nullopt;
    }
    LocalId base_local = 0;
    cursor = ensure_local_operand(std::move(base),
                                  expr.object->result_type.text,
                                  expr.object->result_type.discipline,
                                  *cursor,
                                  base_local);
    if (!cursor.has_value()) {
        return std::nullopt;
    }
    Rvalue proj;
    proj.kind = RvalueKind::ProjectField;
    proj.base_local = base_local;
    if (expr.object->result_type.type_id.has_value()) {
        proj.projection_owner_type_id = *expr.object->result_type.type_id;
    }
    proj.field_name = expr.field_name;
    append_statement(*cursor, Statement{StatementKind::Assign, dest, std::move(proj)});
    return cursor;
}

Builder::MaybeBlock Builder::lower_prove_expr(const hir::ProveExpr& expr,
                                              LocalId dest,
                                              BlockId current,
                                              Env& env) {
    Rvalue value;
    value.kind = RvalueKind::Construct;
    value.owner_type_id = expr.proof_type_id;
    value.qualified_name = expr.qualified_name;

    MaybeBlock cursor = current;
    for (const hir::FieldInit& field : expr.fields) {
        Operand operand;
        cursor = lower_value(*field.value, *cursor, env, operand);
        if (!cursor.has_value()) {
            return std::nullopt;
        }
        value.fields.push_back(FieldValue{field.name, std::move(operand)});
    }

    append_statement(*cursor, Statement{StatementKind::Assign, dest, std::move(value)});
    return cursor;
}

Builder::MaybeBlock Builder::lower_expr_to(const hir::Expr& expr,
                                           LocalId dest,
                                           BlockId current,
                                           Env& env) {
    switch (expr.kind) {
    case hir::ExprKind::NumberLiteral: {
        Statement statement;
        statement.dest_local = dest;
        statement.value.kind = RvalueKind::Use;
        statement.value.operand = Operand{
            OperandKind::IntLiteral,
            0,
            static_cast<const hir::NumberLiteralExpr&>(expr).lexeme,
        };
        append_statement(current, std::move(statement));
        return current;
    }
    case hir::ExprKind::StringLiteral: {
        Statement statement;
        statement.dest_local = dest;
        statement.value.kind = RvalueKind::Use;
        statement.value.operand = Operand{
            OperandKind::StringLiteral,
            0,
            static_cast<const hir::StringLiteralExpr&>(expr).lexeme,
        };
        append_statement(current, std::move(statement));
        return current;
    }
    case hir::ExprKind::Unit: {
        Statement statement;
        statement.dest_local = dest;
        statement.value.kind = RvalueKind::Use;
        statement.value.operand = unit_operand();
        append_statement(current, std::move(statement));
        return current;
    }
    case hir::ExprKind::LocalRef: {
        const auto& local_ref = static_cast<const hir::LocalRefExpr&>(expr);
        Statement statement;
        statement.dest_local = dest;
        statement.value.kind = RvalueKind::Use;
        statement.value.operand = local_operand(lookup_local(env, local_ref.name));
        append_statement(current, std::move(statement));
        return current;
    }
    case hir::ExprKind::Call:
        return lower_call_rvalue(static_cast<const hir::CallExpr&>(expr), dest, current, env);
    case hir::ExprKind::Construct:
        return lower_construct_expr(static_cast<const hir::ConstructExpr&>(expr), dest, current, env);
    case hir::ExprKind::Try:
        return lower_try_expr(static_cast<const hir::TryExpr&>(expr), dest, current, env);
    case hir::ExprKind::Match:
        return lower_match_expr(static_cast<const hir::MatchExpr&>(expr), dest, current, env);
    case hir::ExprKind::Block:
        return lower_block_expr(static_cast<const hir::BlockExpr&>(expr), dest, current, env);
    case hir::ExprKind::Fail: {
        const auto& fail = static_cast<const hir::FailExpr&>(expr);
        const hir::TypeDecl& reason_type = hir::lookup_type(hir_package, fail.reason_type_id);
        LocalId reason_local = make_temp(reason_type.qualified_name,
                                         reason_type.concrete_discipline.value_or(typesys::UseDiscipline::Copyable),
                                         "reason");
        Rvalue reason_value;
        reason_value.kind = RvalueKind::Construct;
        reason_value.owner_type_id = fail.reason_type_id;
        reason_value.variant_id = fail.variant_id;
        reason_value.qualified_name = fail.reason_name + "::" + fail.variant_name;

        MaybeBlock cursor = current;
        for (const hir::FieldInit& field : fail.fields) {
            Operand operand;
            cursor = lower_value(*field.value, *cursor, env, operand);
            if (!cursor.has_value()) {
                return std::nullopt;
            }
            reason_value.fields.push_back(FieldValue{field.name, std::move(operand)});
        }

        append_statement(*cursor, Statement{StatementKind::Assign, reason_local, std::move(reason_value)});
        Terminator terminator;
        terminator.kind = TerminatorKind::Fail;
        terminator.value = local_operand(reason_local);
        set_terminator(*cursor, std::move(terminator));
        return std::nullopt;
    }
    case hir::ExprKind::Grant:
        return lower_grant_expr(static_cast<const hir::GrantExpr&>(expr), dest, current, env);
    case hir::ExprKind::FieldAccess:
        return lower_field_access_expr(static_cast<const hir::FieldAccessExpr&>(expr), dest, current, env);
    case hir::ExprKind::Prove:
        return lower_prove_expr(static_cast<const hir::ProveExpr&>(expr), dest, current, env);
    }
    return current;
}

} // namespace

Package lower(const hir::Package& package) {
    Package lowered;
    lowered.functions.reserve(package.functions.size());
    for (const hir::FunctionDecl& function : package.functions) {
        Builder builder(package, function);
        lowered.functions.push_back(builder.lower(function));
    }
    return lowered;
}

std::string dump(const Package& package) {
    std::ostringstream out;
    out << "functions:\n";
    for (const Function& function : package.functions) {
        out << "  - " << ast::visibility_name(function.visibility) << ' '
            << (function.is_foreign ? "foreign-fn " : "fn ") << function.qualified_name
            << " -> " << function.return_type;
        if (function.fails_type.has_value()) {
            out << " fails " << *function.fails_type;
        }
        if (function.grants_type.has_value()) {
            out << " grants " << *function.grants_type;
        }
        for (const std::string& p : function.proves_types) {
            out << " proves " << p;
        }
        out << '\n';
        if (!function.locals.empty()) {
            out << "      locals:\n";
            for (const Local& local : function.locals) {
                out << "        " << format_local_name(local) << " : " << local.type;
                if (local.is_compile_time_only) {
                    out << " [compile-time-only]";
                }
                if (local.is_affine) {
                    out << " [affine]";
                }
                out << '\n';
            }
        }
        if (function.blocks.empty()) {
            out << "      blocks: <none>\n";
            continue;
        }
        for (const BasicBlock& block : function.blocks) {
            out << "      block %" << block.id << '\n';
            for (const Statement& statement : block.statements) {
                out << "        " << format_local_name(function.locals.at(statement.dest_local))
                    << " = " << format_rvalue(statement.value, function) << '\n';
            }
            if (!block.has_terminator) {
                out << "        <unterminated>\n";
                continue;
            }
            switch (block.terminator.kind) {
            case TerminatorKind::Return:
                out << "        return " << format_operand(*block.terminator.value, function) << '\n';
                break;
            case TerminatorKind::Fail:
                out << "        fail " << format_operand(*block.terminator.value, function) << '\n';
                break;
            case TerminatorKind::Goto:
                out << "        goto %" << block.terminator.target_block << '\n';
                break;
            case TerminatorKind::SwitchVariant:
                out << "        switch " << format_local_name(function.locals.at(block.terminator.scrutinee_local)) << '\n';
                for (const SwitchEdge& edge : block.terminator.edges) {
                    out << "          arm " << edge.variant_name << " -> %" << edge.target_block << '\n';
                }
                break;
            case TerminatorKind::Invoke:
                out << "        invoke " << block.terminator.callee_name << '(';
                for (std::size_t index = 0; index < block.terminator.args.size(); ++index) {
                    if (index > 0) {
                        out << ", ";
                    }
                    out << format_operand(block.terminator.args[index], function);
                }
                out << ") ok " << format_local_name(function.locals.at(block.terminator.success_local))
                    << " -> %" << block.terminator.success_block
                    << " fail " << format_local_name(function.locals.at(block.terminator.failure_local))
                    << " -> %" << block.terminator.failure_block << '\n';
                break;
            case TerminatorKind::Unreachable:
                out << "        unreachable\n";
                break;
            }
        }
    }
    return out.str();
}

} // namespace evident::mir
