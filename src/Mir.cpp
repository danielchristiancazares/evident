#include "evident/Mir.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace evident::mir {

Operand Operand::local(LocalId local_id) {
    return Operand(Kind::Local, local_id, "local operand");
}

Operand Operand::int_literal(std::string text) {
    return Operand(Kind::IntLiteral, LocalId{}, std::move(text));
}

Operand Operand::string_literal(std::string text) {
    return Operand(Kind::StringLiteral, LocalId{}, std::move(text));
}

Operand Operand::unit() {
    return Operand(Kind::Unit, LocalId{}, "unit operand");
}

Operand::Operand(Kind kind, LocalId local_id, std::string text)
    : kind_(kind), local_id_(local_id), text_(std::move(text)) {}

FieldValue FieldValue::named(std::string field_name, Operand operand) {
    return FieldValue(std::move(field_name), std::move(operand));
}

FieldValue::FieldValue(std::string field_name, Operand operand)
    : field_name_(std::move(field_name)), operand_(std::move(operand)) {}

Statement Statement::assign(LocalId dest_local, Rvalue value) {
    return Statement(dest_local, std::move(value));
}

Statement::Statement(LocalId dest_local, Rvalue value)
    : dest_local_(dest_local), value_(std::move(value)) {}

FunctionFailureContract FunctionFailureContract::returns_declared_value() {
    return FunctionFailureContract(FunctionFailureBehavior::ReturnsDeclaredValue, "returns declared value");
}

FunctionFailureContract FunctionFailureContract::yields_reason(std::string reason_type) {
    return FunctionFailureContract(FunctionFailureBehavior::YieldsReason, std::move(reason_type));
}

FunctionFailureContract::FunctionFailureContract(FunctionFailureBehavior behavior, std::string reason_type)
    : behavior_(behavior), reason_type_(std::move(reason_type)) {}

FunctionAuthorityContract FunctionAuthorityContract::ordinary_call() {
    return FunctionAuthorityContract(FunctionAuthorityEffect::OrdinaryCall, "ordinary call");
}

FunctionAuthorityContract FunctionAuthorityContract::grants_scoped_permit(std::string permit_type) {
    return FunctionAuthorityContract(FunctionAuthorityEffect::GrantsScopedPermit, std::move(permit_type));
}

FunctionAuthorityContract::FunctionAuthorityContract(FunctionAuthorityEffect effect, std::string permit_type)
    : effect_(effect), permit_type_(std::move(permit_type)) {}

Rvalue Rvalue::uses(Operand operand) {
    return Rvalue(Kind::Use,
                  std::move(operand),
                  hir::FunctionId{},
                  "use rvalue",
                  {},
                  hir::TypeId{},
                  hir::VariantId{},
                  "use rvalue",
                  {},
                  LocalId{},
                  hir::TypeId{},
                  "use rvalue");
}

Rvalue Rvalue::calls(hir::FunctionId function_id,
                     std::string callee_name,
                     std::vector<Operand> args) {
    return Rvalue(Kind::Call,
                  Operand::unit(),
                  function_id,
                  std::move(callee_name),
                  std::move(args),
                  hir::TypeId{},
                  hir::VariantId{},
                  "call rvalue",
                  {},
                  LocalId{},
                  hir::TypeId{},
                  "call rvalue");
}

Rvalue Rvalue::constructs_named_type(hir::TypeId owner_type_id,
                                     std::string qualified_name,
                                     std::vector<FieldValue> fields) {
    return Rvalue(Kind::ConstructNamedType,
                  Operand::unit(),
                  hir::FunctionId{},
                  "construct named type rvalue",
                  {},
                  owner_type_id,
                  hir::VariantId{},
                  std::move(qualified_name),
                  std::move(fields),
                  LocalId{},
                  hir::TypeId{},
                  "construct named type rvalue");
}

Rvalue Rvalue::constructs_named_variant(hir::TypeId owner_type_id,
                                        hir::VariantId variant_id,
                                        std::string qualified_name,
                                        std::vector<FieldValue> fields) {
    return Rvalue(Kind::ConstructNamedVariant,
                  Operand::unit(),
                  hir::FunctionId{},
                  "construct named variant rvalue",
                  {},
                  owner_type_id,
                  variant_id,
                  std::move(qualified_name),
                  std::move(fields),
                  LocalId{},
                  hir::TypeId{},
                  "construct named variant rvalue");
}

Rvalue Rvalue::projects_named_type_field(LocalId base_local, std::string field_name) {
    return Rvalue(Kind::ProjectNamedTypeField,
                  Operand::unit(),
                  hir::FunctionId{},
                  "project named type field rvalue",
                  {},
                  hir::TypeId{},
                  hir::VariantId{},
                  "project named type field rvalue",
                  {},
                  base_local,
                  hir::TypeId{},
                  std::move(field_name));
}

Rvalue Rvalue::projects_named_variant_payload_field(LocalId base_local,
                                                    hir::TypeId projection_owner_type_id,
                                                    hir::VariantId variant_id,
                                                    std::string field_name) {
    return Rvalue(Kind::ProjectNamedVariantPayloadField,
                  Operand::unit(),
                  hir::FunctionId{},
                  "project named variant payload field rvalue",
                  {},
                  hir::TypeId{},
                  variant_id,
                  "project named variant payload field rvalue",
                  {},
                  base_local,
                  projection_owner_type_id,
                  std::move(field_name));
}

Rvalue::Rvalue(Kind kind,
               Operand operand,
               hir::FunctionId function_id,
               std::string callee_name,
               std::vector<Operand> args,
               hir::TypeId owner_type_id,
               hir::VariantId variant_id,
               std::string qualified_name,
               std::vector<FieldValue> fields,
               LocalId base_local,
               hir::TypeId projection_owner_type_id,
               std::string field_name)
    : kind_(kind),
      operand_(std::move(operand)),
      function_id_(function_id),
      callee_name_(std::move(callee_name)),
      args_(std::move(args)),
      owner_type_id_(owner_type_id),
      variant_id_(variant_id),
      qualified_name_(std::move(qualified_name)),
      fields_(std::move(fields)),
      base_local_(base_local),
      projection_owner_type_id_(projection_owner_type_id),
      field_name_(std::move(field_name)) {}

SwitchEdge SwitchEdge::to_variant(hir::VariantId variant_id,
                                  std::string variant_name,
                                  BlockId target_block) {
    return SwitchEdge(variant_id, std::move(variant_name), target_block);
}

SwitchEdge::SwitchEdge(hir::VariantId variant_id, std::string variant_name, BlockId target_block)
    : variant_id_(variant_id), variant_name_(std::move(variant_name)), target_block_(target_block) {}

Terminator Terminator::returns(Operand value) {
    return Terminator(Kind::Return,
                      std::move(value),
                      BlockId{},
                      LocalId{},
                      {},
                      hir::FunctionId{},
                      "return terminator",
                      {},
                      LocalId{},
                      LocalId{},
                      BlockId{},
                      BlockId{});
}

Terminator Terminator::fails(Operand reason) {
    return Terminator(Kind::Fail,
                      std::move(reason),
                      BlockId{},
                      LocalId{},
                      {},
                      hir::FunctionId{},
                      "fail terminator",
                      {},
                      LocalId{},
                      LocalId{},
                      BlockId{},
                      BlockId{});
}

Terminator Terminator::jumps_to(BlockId target_block) {
    return Terminator(Kind::Goto,
                      Operand::unit(),
                      target_block,
                      LocalId{},
                      {},
                      hir::FunctionId{},
                      "goto terminator",
                      {},
                      LocalId{},
                      LocalId{},
                      BlockId{},
                      BlockId{});
}

Terminator Terminator::switches_on_variant(LocalId scrutinee_local,
                                           std::vector<SwitchEdge> edges) {
    return Terminator(Kind::SwitchVariant,
                      Operand::unit(),
                      BlockId{},
                      scrutinee_local,
                      std::move(edges),
                      hir::FunctionId{},
                      "switch variant terminator",
                      {},
                      LocalId{},
                      LocalId{},
                      BlockId{},
                      BlockId{});
}

Terminator Terminator::invokes(hir::FunctionId function_id,
                               std::string callee_name,
                               std::vector<Operand> args,
                               LocalId success_local,
                               LocalId failure_local,
                               BlockId success_block,
                               BlockId failure_block) {
    return Terminator(Kind::Invoke,
                      Operand::unit(),
                      BlockId{},
                      LocalId{},
                      {},
                      function_id,
                      std::move(callee_name),
                      std::move(args),
                      success_local,
                      failure_local,
                      success_block,
                      failure_block);
}

Terminator Terminator::unreachable() {
    return Terminator(Kind::Unreachable,
                      Operand::unit(),
                      BlockId{},
                      LocalId{},
                      {},
                      hir::FunctionId{},
                      "unreachable terminator",
                      {},
                      LocalId{},
                      LocalId{},
                      BlockId{},
                      BlockId{});
}

Terminator::Terminator(Kind kind,
                       Operand operand,
                       BlockId target_block,
                       LocalId scrutinee_local,
                       std::vector<SwitchEdge> edges,
                       hir::FunctionId function_id,
                       std::string callee_name,
                       std::vector<Operand> args,
                       LocalId success_local,
                       LocalId failure_local,
                       BlockId success_block,
                       BlockId failure_block)
    : kind_(kind),
      operand_(std::move(operand)),
      target_block_(target_block),
      scrutinee_local_(scrutinee_local),
      edges_(std::move(edges)),
      function_id_(function_id),
      callee_name_(std::move(callee_name)),
      args_(std::move(args)),
      success_local_(success_local),
      failure_local_(failure_local),
      success_block_(success_block),
      failure_block_(failure_block) {}

Local Local::parameter(LocalId id,
                       std::string name,
                       std::string type_name,
                       typesys::UseDiscipline discipline) {
    return Local(id, LocalKind::Parameter, std::move(name), std::move(type_name), discipline);
}

Local Local::let_binding(LocalId id,
                         std::string name,
                         std::string type_name,
                         typesys::UseDiscipline discipline) {
    return Local(id, LocalKind::Let, std::move(name), std::move(type_name), discipline);
}

Local Local::temporary(LocalId id,
                       std::string name,
                       std::string type_name,
                       typesys::UseDiscipline discipline) {
    return Local(id, LocalKind::Temporary, std::move(name), std::move(type_name), discipline);
}

Local Local::return_slot(LocalId id,
                         std::string name,
                         std::string type_name,
                         typesys::UseDiscipline discipline) {
    return Local(id, LocalKind::ReturnSlot, std::move(name), std::move(type_name), discipline);
}

Local::Local(LocalId id,
             LocalKind kind,
             std::string name,
             std::string type_name,
             typesys::UseDiscipline discipline)
    : id_(id),
      kind_(kind),
      name_(std::move(name)),
      type_name_(std::move(type_name)),
      discipline_(discipline) {}

namespace {

enum class BlockCursorState {
    ContinuesAtBlock,
    TerminatesControlFlow,
};

class BlockCursor final {
public:
    [[nodiscard]] static BlockCursor continues_at(BlockId block_id) {
        return BlockCursor(BlockCursorState::ContinuesAtBlock, block_id);
    }

    [[nodiscard]] static BlockCursor terminates_control_flow() {
        return BlockCursor(BlockCursorState::TerminatesControlFlow, BlockId{});
    }

    [[nodiscard]] BlockCursorState state() const noexcept { return state_; }
    [[nodiscard]] BlockId block_id() const noexcept { return block_id_; }

private:
    BlockCursorState state_;
    BlockId block_id_;

    BlockCursor(BlockCursorState state, BlockId block_id)
        : state_(state), block_id_(block_id) {}
};

std::string temp_type_name_for(const hir::TypeRef& type) {
    if (type.flavor == typesys::TypeFlavor::Error) {
        return "Unit";
    }
    return type.text;
}

enum class JoinBlockState {
    AwaitingContinuingArm,
    JoinBlockAllocated,
};

class JoinBlockTarget final {
public:
    [[nodiscard]] static JoinBlockTarget awaiting_continuing_arm() {
        return JoinBlockTarget(JoinBlockState::AwaitingContinuingArm, BlockId{});
    }

    [[nodiscard]] static JoinBlockTarget allocated(BlockId block_id) {
        return JoinBlockTarget(JoinBlockState::JoinBlockAllocated, block_id);
    }

    [[nodiscard]] JoinBlockState state() const noexcept { return state_; }
    [[nodiscard]] BlockId block_id() const noexcept { return block_id_; }

    [[nodiscard]] BlockCursor cursor() const {
        if (state_ == JoinBlockState::JoinBlockAllocated) {
            return BlockCursor::continues_at(block_id_);
        }
        return BlockCursor::terminates_control_flow();
    }

private:
    JoinBlockState state_;
    BlockId block_id_;

    JoinBlockTarget(JoinBlockState state, BlockId block_id)
        : state_(state), block_id_(block_id) {}
};

struct Env {
    Env* parent = nullptr;
    std::unordered_map<std::string, LocalId> locals;
};

struct Builder {
    const hir::Package& hir_package;
    Function function;
    LocalId return_local = 0;

    explicit Builder(const hir::Package& package, const hir::FunctionDecl& source)
        : hir_package(package) {
        function.function_id = source.id;
        function.visibility = source.visibility;
        function.qualified_name = source.qualified_name;
        function.return_type = source.return_type.text;
        if (source.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
            function.failure = FunctionFailureContract::yields_reason(
                hir::lookup_type(package, source.failure.reason_type_id()).qualified_name);
        }
        if (source.authority.effect() == hir::FunctionAuthorityEffect::GrantsScopedPermit) {
            function.authority = FunctionAuthorityContract::grants_scoped_permit(
                hir::lookup_type(package, source.authority.permit_type_id()).qualified_name);
        }
        for (hir::TypeId proof_id : source.proves_proof_type_ids) {
            function.proves_types.push_back(hir::lookup_type(package, proof_id).qualified_name);
        }
        function.implementation = source.implementation;
    }

    [[nodiscard]] Function lower(const hir::FunctionDecl& source);

private:
    [[nodiscard]] LocalId add_local(std::string name,
                                    std::string type,
                                    LocalKind kind,
                                    typesys::UseDiscipline discipline = typesys::UseDiscipline::Copyable);
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
    [[nodiscard]] BlockId ensure_join_block(JoinBlockTarget& join);
    [[nodiscard]] BlockCursor lower_block_expr(const hir::BlockExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_expr_to(const hir::Expr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_value(const hir::Expr& expr, BlockId current, Env& env, Operand& out);
    [[nodiscard]] BlockCursor lower_construct_expr(const hir::ConstructExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_call_rvalue(const hir::CallExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_direct_call(hir::FunctionId function_id,
                                                const std::string& callee_name,
                                                const std::vector<std::unique_ptr<hir::Expr>>& args,
                                                LocalId dest,
                                                BlockId current,
                                                Env& env);
    [[nodiscard]] BlockCursor lower_call_to_success_or_failure(hir::FunctionId function_id,
                                                               const std::string& callee_name,
                                                               const std::vector<std::unique_ptr<hir::Expr>>& args,
                                                               LocalId success_dest,
                                                               LocalId failure_dest,
                                                               BlockId success_block,
                                                               BlockId failure_block,
                                                               BlockId current,
                                                               Env& env);
    [[nodiscard]] BlockCursor lower_expr_to_success_or_failure(const hir::Expr& expr,
                                                               LocalId success_dest,
                                                               LocalId failure_dest,
                                                               BlockId success_block,
                                                               BlockId failure_block,
                                                               BlockId current,
                                                               Env& env);
    [[nodiscard]] BlockCursor lower_block_to_success_or_failure(const hir::BlockExpr& expr,
                                                                LocalId success_dest,
                                                                LocalId failure_dest,
                                                                BlockId success_block,
                                                                BlockId failure_block,
                                                                BlockId current,
                                                                Env& env);
    [[nodiscard]] BlockCursor lower_grant_to_success_or_failure(const hir::GrantExpr& expr,
                                                                LocalId success_dest,
                                                                LocalId failure_dest,
                                                                BlockId success_block,
                                                                BlockId failure_block,
                                                                BlockId current,
                                                                Env& env);
    [[nodiscard]] BlockCursor lower_failing_expr_to_propagation(const hir::Expr& expr,
                                                                LocalId dest,
                                                                BlockId current,
                                                                Env& env);
    [[nodiscard]] BlockCursor lower_try_expr(const hir::TryExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_match_expr(const hir::MatchExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_state_match(const hir::MatchExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_yield_match(const hir::MatchExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_grant_expr(const hir::GrantExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_field_access_expr(const hir::FieldAccessExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor lower_prove_expr(const hir::ProveExpr& expr, LocalId dest, BlockId current, Env& env);
    [[nodiscard]] BlockCursor ensure_local_operand(Operand operand,
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
    out << '%' << local.id() << '[' << local.name() << ']';
    return out.str();
}

std::string format_operand(const Operand& operand, const Function& function) {
    return operand.match(
        [&](Operand::LocalValue local) -> std::string {
            return format_local_name(function.locals.at(local.local_id));
        },
        [](Operand::IntLiteralValue literal) -> std::string {
            return literal.text;
        },
        [](Operand::StringLiteralValue literal) -> std::string {
            return literal.text;
        },
        [](Operand::UnitValue) -> std::string {
            return "unit";
        });
}

Rvalue use_rvalue(Operand operand) {
    return Rvalue::uses(std::move(operand));
}

std::string format_rvalue(const Rvalue& value, const Function& function) {
    std::ostringstream out;
    return value.match(
        [&](Rvalue::UseValue value) {
            return format_operand(value.operand, function);
        },
        [&](Rvalue::CallValue value) {
            out << "call " << value.callee_name << '(';
            for (std::size_t index = 0; index < value.args.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << format_operand(value.args[index], function);
            }
            out << ')';
            return out.str();
        },
        [&](Rvalue::ConstructNamedTypeValue value) {
            out << "construct " << value.qualified_name << " {";
            for (std::size_t index = 0; index < value.fields.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << value.fields[index].field_name() << ": "
                    << format_operand(value.fields[index].operand(), function);
            }
            out << '}';
            return out.str();
        },
        [&](Rvalue::ConstructNamedVariantValue value) {
            out << "construct " << value.qualified_name << " {";
            for (std::size_t index = 0; index < value.fields.size(); ++index) {
                if (index > 0) {
                    out << ", ";
                }
                out << value.fields[index].field_name() << ": "
                    << format_operand(value.fields[index].operand(), function);
            }
            out << '}';
            return out.str();
        },
        [&](Rvalue::ProjectNamedTypeFieldValue value) {
            out << format_local_name(function.locals.at(value.base_local)) << '.' << value.field_name;
            return out.str();
        },
        [&](Rvalue::ProjectNamedVariantPayloadFieldValue value) {
            out << format_local_name(function.locals.at(value.base_local)) << '.' << value.field_name;
            return out.str();
        });
}

Function Builder::lower(const hir::FunctionDecl& source) {
    Env env{nullptr, {}};
    for (const hir::Parameter& param : source.params) {
        LocalId local = add_local(param.name,
                                  param.type.text,
                                  LocalKind::Parameter,
                                  param.type.discipline);
        env.locals.emplace(param.name, local);
    }
    if (source.implementation == ast::FunctionImplementation::ForeignImport) {
        return std::move(function);
    }

    return_local = add_local("$return",
                             source.return_type.text,
                             LocalKind::ReturnSlot,
                             source.return_type.discipline);
    BlockId entry = add_block();
    BlockCursor tail = lower_block_expr(*source.body, return_local, entry, env);
    if (tail.state() == BlockCursorState::ContinuesAtBlock) {
        set_terminator(tail.block_id(), Terminator::returns(local_operand(return_local)));
    }
    return std::move(function);
}

LocalId Builder::add_local(std::string name,
                           std::string type,
                           LocalKind kind,
                           typesys::UseDiscipline discipline) {
    const LocalId id = function.locals.size();
    switch (kind) {
    case LocalKind::Parameter:
        function.locals.push_back(Local::parameter(id, std::move(name), std::move(type), discipline));
        break;
    case LocalKind::Let:
        function.locals.push_back(Local::let_binding(id, std::move(name), std::move(type), discipline));
        break;
    case LocalKind::Temporary:
        function.locals.push_back(Local::temporary(id, std::move(name), std::move(type), discipline));
        break;
    case LocalKind::ReturnSlot:
        function.locals.push_back(Local::return_slot(id, std::move(name), std::move(type), discipline));
        break;
    }
    return id;
}

BlockId Builder::add_block() {
    function.blocks.push_back(BasicBlock{function.blocks.size(), {}, Terminator::unreachable()});
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
}

Operand Builder::local_operand(LocalId local_id) const {
    return Operand::local(local_id);
}

Operand Builder::unit_operand() const {
    return Operand::unit();
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
                     discipline);
}

BlockId Builder::ensure_join_block(JoinBlockTarget& join) {
    if (join.state() == JoinBlockState::AwaitingContinuingArm) {
        join = JoinBlockTarget::allocated(add_block());
    }
    return join.block_id();
}

BlockCursor Builder::lower_block_expr(const hir::BlockExpr& expr,
                                      LocalId dest,
                                      BlockId current,
                                      Env& env) {
    Env local{&env, {}};
    BlockCursor cursor = BlockCursor::continues_at(current);

    for (const auto& stmt_ptr : expr.statements) {
        if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
            return BlockCursor::terminates_control_flow();
        }
        const hir::Stmt& stmt = *stmt_ptr;
        switch (stmt.kind) {
        case hir::StatementKind::Let: {
            const auto& let_stmt = static_cast<const hir::LetStmt&>(stmt);
            LocalId local_id = add_local(let_stmt.name,
                                         let_stmt.type.text,
                                         LocalKind::Let,
                                         let_stmt.type.discipline);
            cursor = lower_expr_to(*let_stmt.initializer, local_id, cursor.block_id(), local);
            if (cursor.state() == BlockCursorState::ContinuesAtBlock) {
                local.locals[let_stmt.name] = local_id;
            }
            break;
        }
        case hir::StatementKind::Expr: {
            const auto& expr_stmt = static_cast<const hir::ExprStmt&>(stmt);
            LocalId temp = make_temp(temp_type_name_for(expr_stmt.expr->result_type),
                                     expr_stmt.expr->result_type.discipline,
                                     "discard");
            cursor = lower_expr_to(*expr_stmt.expr, temp, cursor.block_id(), local);
            break;
        }
        }
    }

    if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }
    return lower_expr_to(*expr.result, dest, cursor.block_id(), local);
}

BlockCursor Builder::lower_value(const hir::Expr& expr,
                                 BlockId current,
                                 Env& env,
                                 Operand& out) {
    switch (expr.kind) {
    case hir::ExprKind::NumberLiteral:
        out = Operand::int_literal(static_cast<const hir::NumberLiteralExpr&>(expr).lexeme);
        return BlockCursor::continues_at(current);
    case hir::ExprKind::StringLiteral:
        out = Operand::string_literal(static_cast<const hir::StringLiteralExpr&>(expr).lexeme);
        return BlockCursor::continues_at(current);
    case hir::ExprKind::Unit:
        out = unit_operand();
        return BlockCursor::continues_at(current);
    case hir::ExprKind::LocalRef: {
        const auto& local_ref = static_cast<const hir::LocalRefExpr&>(expr);
        out = local_operand(lookup_local(env, local_ref.name));
        return BlockCursor::continues_at(current);
    }
    default:
        break;
    }

    LocalId temp = make_temp(temp_type_name_for(expr.result_type), expr.result_type.discipline);
    BlockCursor next = lower_expr_to(expr, temp, current, env);
    if (next.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }
    out = local_operand(temp);
    return next;
}

BlockCursor Builder::ensure_local_operand(Operand operand,
                                          std::string type,
                                          typesys::UseDiscipline discipline,
                                          BlockId current,
                                          LocalId& out_local) {
    return std::move(operand).match_local_or_materialized(
        [&](Operand::LocalValue local) -> BlockCursor {
            out_local = local.local_id;
            return BlockCursor::continues_at(current);
        },
        [&](Operand materialized) -> BlockCursor {
            out_local = make_temp(std::move(type), discipline, "materialize");
            append_statement(current, Statement::assign(out_local, use_rvalue(std::move(materialized))));
            return BlockCursor::continues_at(current);
        });
}

BlockCursor Builder::lower_construct_expr(const hir::ConstructExpr& expr,
                                          LocalId dest,
                                          BlockId current,
                                          Env& env) {
    std::vector<FieldValue> fields;
    fields.reserve(expr.fields.size());
    BlockCursor cursor = BlockCursor::continues_at(current);
    for (const hir::FieldInit& field : expr.fields) {
        Operand operand = unit_operand();
        cursor = lower_value(*field.value, cursor.block_id(), env, operand);
        if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
            return BlockCursor::terminates_control_flow();
        }
        fields.push_back(FieldValue::named(field.name, std::move(operand)));
    }

    Rvalue value = expr.variant_target.state() == hir::ConstructVariantTargetState::ConstructsNamedVariant
        ? Rvalue::constructs_named_variant(expr.owner_type_id,
                                           expr.variant_target.variant_id(),
                                           expr.qualified_name,
                                           std::move(fields))
        : Rvalue::constructs_named_type(expr.owner_type_id,
                                        expr.qualified_name,
                                        std::move(fields));
    append_statement(cursor.block_id(), Statement::assign(dest, std::move(value)));
    return cursor;
}

BlockCursor Builder::lower_call_rvalue(const hir::CallExpr& expr,
                                       LocalId dest,
                                       BlockId current,
                                       Env& env) {
    return lower_direct_call(expr.function_id, expr.callee_name, expr.args, dest, current, env);
}

BlockCursor Builder::lower_direct_call(hir::FunctionId function_id,
                                       const std::string& callee_name,
                                       const std::vector<std::unique_ptr<hir::Expr>>& args,
                                       LocalId dest,
                                       BlockId current,
                                       Env& env) {
    const hir::FunctionDecl& callee = hir::lookup_function(hir_package, function_id);

    std::vector<Operand> call_args;
    call_args.reserve(args.size());
    BlockCursor cursor = BlockCursor::continues_at(current);
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index < callee.params.size()
            && typesys::discipline_materialization(callee.params[index].type.discipline)
                == typesys::DisciplineMaterialization::CompileTimeOnly) {
            call_args.push_back(unit_operand());
            continue;
        }
        Operand operand = unit_operand();
        cursor = lower_value(*args[index], cursor.block_id(), env, operand);
        if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
            return BlockCursor::terminates_control_flow();
        }
        call_args.push_back(std::move(operand));
    }

    append_statement(cursor.block_id(),
                     Statement::assign(dest,
                                       Rvalue::calls(function_id, callee_name, std::move(call_args))));
    return cursor;
}

BlockCursor Builder::lower_call_to_success_or_failure(hir::FunctionId function_id,
                                                      const std::string& callee_name,
                                                      const std::vector<std::unique_ptr<hir::Expr>>& args,
                                                      LocalId success_dest,
                                                      LocalId failure_dest,
                                                      BlockId success_block,
                                                      BlockId failure_block,
                                                      BlockId current,
                                                      Env& env) {
    const hir::FunctionDecl& callee = hir::lookup_function(hir_package, function_id);

    std::vector<Operand> invoke_args;
    invoke_args.reserve(args.size());
    BlockCursor cursor = BlockCursor::continues_at(current);
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index < callee.params.size()
            && typesys::discipline_materialization(callee.params[index].type.discipline)
                == typesys::DisciplineMaterialization::CompileTimeOnly) {
            invoke_args.push_back(unit_operand());
            continue;
        }
        Operand operand = unit_operand();
        cursor = lower_value(*args[index], cursor.block_id(), env, operand);
        if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
            return BlockCursor::terminates_control_flow();
        }
        invoke_args.push_back(std::move(operand));
    }

    set_terminator(cursor.block_id(),
                   Terminator::invokes(function_id,
                                       callee_name,
                                       std::move(invoke_args),
                                       success_dest,
                                       failure_dest,
                                       success_block,
                                       failure_block));
    return BlockCursor::terminates_control_flow();
}

BlockCursor Builder::lower_block_to_success_or_failure(const hir::BlockExpr& expr,
                                                       LocalId success_dest,
                                                       LocalId failure_dest,
                                                       BlockId success_block,
                                                       BlockId failure_block,
                                                       BlockId current,
                                                       Env& env) {
    Env local{&env, {}};
    BlockCursor cursor = BlockCursor::continues_at(current);

    for (const auto& stmt_ptr : expr.statements) {
        if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
            return BlockCursor::terminates_control_flow();
        }
        const hir::Stmt& stmt = *stmt_ptr;
        switch (stmt.kind) {
        case hir::StatementKind::Let: {
            const auto& let_stmt = static_cast<const hir::LetStmt&>(stmt);
            LocalId local_id = add_local(let_stmt.name,
                                         let_stmt.type.text,
                                         LocalKind::Let,
                                         let_stmt.type.discipline);
            cursor = lower_expr_to(*let_stmt.initializer, local_id, cursor.block_id(), local);
            if (cursor.state() == BlockCursorState::ContinuesAtBlock) {
                local.locals[let_stmt.name] = local_id;
            }
            break;
        }
        case hir::StatementKind::Expr: {
            const auto& expr_stmt = static_cast<const hir::ExprStmt&>(stmt);
            LocalId temp = make_temp(temp_type_name_for(expr_stmt.expr->result_type),
                                     expr_stmt.expr->result_type.discipline,
                                     "discard");
            cursor = lower_expr_to(*expr_stmt.expr, temp, cursor.block_id(), local);
            break;
        }
        }
    }

    if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }
    return lower_expr_to_success_or_failure(*expr.result,
                                            success_dest,
                                            failure_dest,
                                            success_block,
                                            failure_block,
                                            cursor.block_id(),
                                            local);
}

BlockCursor Builder::lower_grant_to_success_or_failure(const hir::GrantExpr& expr,
                                                       LocalId success_dest,
                                                       LocalId failure_dest,
                                                       BlockId success_block,
                                                       BlockId failure_block,
                                                       BlockId current,
                                                       Env& env) {
    LocalId grant_dest = make_temp("Unit", typesys::UseDiscipline::Copyable, "grant");
    BlockCursor cursor = BlockCursor::continues_at(current);
    const hir::FunctionDecl& grantor = hir::lookup_function(hir_package, expr.grant_function_id);
    if (grantor.failure.behavior() == hir::FunctionFailureBehavior::YieldsReason) {
        const BlockId grant_success_block = add_block();
        const BlockId grant_call_block = cursor.block_id();
        const BlockCursor grant_tail = lower_call_to_success_or_failure(expr.grant_function_id,
                                                                        expr.grant_name,
                                                                        expr.args,
                                                                        grant_dest,
                                                                        failure_dest,
                                                                        grant_success_block,
                                                                        failure_block,
                                                                        grant_call_block,
                                                                        env);
        if (grant_tail.state() == BlockCursorState::ContinuesAtBlock) {
            return grant_tail;
        }
        cursor = block(grant_call_block).terminator.match(
            [&](Terminator::ReturnValue) -> BlockCursor {
                return grant_tail;
            },
            [&](Terminator::FailValue) -> BlockCursor {
                return grant_tail;
            },
            [&](Terminator::GotoValue) -> BlockCursor {
                return grant_tail;
            },
            [&](Terminator::SwitchVariantValue) -> BlockCursor {
                return grant_tail;
            },
            [&](Terminator::InvokeValue) -> BlockCursor {
                return BlockCursor::continues_at(grant_success_block);
            },
            [&](Terminator::UnreachableValue) -> BlockCursor {
                return grant_tail;
            });
    } else {
        cursor = lower_direct_call(expr.grant_function_id, expr.grant_name, expr.args, grant_dest, cursor.block_id(), env);
    }
    if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }

    Env scoped{&env, {}};
    const LocalId binder = add_local(expr.binder_name,
                                     expr.permit_name,
                                     LocalKind::Let,
                                     typesys::UseDiscipline::ScopedAuthority);
    scoped.locals.emplace(expr.binder_name, binder);
    return lower_block_to_success_or_failure(*expr.body,
                                             success_dest,
                                             failure_dest,
                                             success_block,
                                             failure_block,
                                             cursor.block_id(),
                                             scoped);
}

BlockCursor Builder::lower_expr_to_success_or_failure(const hir::Expr& expr,
                                                      LocalId success_dest,
                                                      LocalId failure_dest,
                                                      BlockId success_block,
                                                      BlockId failure_block,
                                                      BlockId current,
                                                      Env& env) {
    if (expr.failure.effect() == hir::ExprFailureEffect::YieldsReason) {
        switch (expr.kind) {
        case hir::ExprKind::Call: {
            const auto& call = static_cast<const hir::CallExpr&>(expr);
            return lower_call_to_success_or_failure(call.function_id,
                                                    call.callee_name,
                                                    call.args,
                                                    success_dest,
                                                    failure_dest,
                                                    success_block,
                                                    failure_block,
                                                    current,
                                                    env);
        }
        case hir::ExprKind::Grant:
            return lower_grant_to_success_or_failure(static_cast<const hir::GrantExpr&>(expr),
                                                     success_dest,
                                                     failure_dest,
                                                     success_block,
                                                     failure_block,
                                                     current,
                                                     env);
        case hir::ExprKind::Block:
            return lower_block_to_success_or_failure(static_cast<const hir::BlockExpr&>(expr),
                                                     success_dest,
                                                     failure_dest,
                                                     success_block,
                                                     failure_block,
                                                     current,
                                                     env);
        default:
            break;
        }
    }

    BlockCursor tail = lower_expr_to(expr, success_dest, current, env);
    if (tail.state() == BlockCursorState::ContinuesAtBlock) {
        set_terminator(tail.block_id(), Terminator::jumps_to(success_block));
    }
    return BlockCursor::terminates_control_flow();
}

BlockCursor Builder::lower_failing_expr_to_propagation(const hir::Expr& expr,
                                                       LocalId dest,
                                                       BlockId current,
                                                       Env& env) {
    if (expr.failure.effect() != hir::ExprFailureEffect::YieldsReason) {
        return lower_expr_to(expr, dest, current, env);
    }

    const hir::TypeDecl& failure_type = hir::lookup_type(hir_package, expr.failure.reason_type_id());
    const typesys::UseDiscipline failure_discipline =
        failure_type.discipline.state() == hir::TypeDisciplineResolutionState::ConcreteUseDisciplineKnown
        ? failure_type.discipline.discipline()
        : typesys::UseDiscipline::Copyable;
    LocalId failure_local = make_temp(failure_type.qualified_name,
                                      failure_discipline,
                                      "failure");
    BlockId success_block = add_block();
    BlockId failure_block = add_block();
    const BlockCursor lowered = lower_expr_to_success_or_failure(expr,
                                                                 dest,
                                                                 failure_local,
                                                                 success_block,
                                                                 failure_block,
                                                                 current,
                                                                 env);
    if (lowered.state() == BlockCursorState::ContinuesAtBlock) {
        return lowered;
    }

    set_terminator(failure_block, Terminator::fails(local_operand(failure_local)));
    return BlockCursor::continues_at(success_block);
}

BlockCursor Builder::lower_try_expr(const hir::TryExpr& expr,
                                    LocalId dest,
                                    BlockId current,
                                    Env& env) {
    return lower_failing_expr_to_propagation(*expr.operand, dest, current, env);
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
                                      field.type.discipline);
            env.locals[binding.binding_name] = local;
            append_statement(block_id,
                             Statement::assign(local,
                                               Rvalue::projects_named_variant_payload_field(source_local,
                                                                                           variant.owner_type,
                                                                                           variant.id,
                                                                                           field.name)));
            break;
        }
    }
}

BlockCursor Builder::lower_state_match(const hir::MatchExpr& expr,
                                       LocalId dest,
                                       BlockId current,
                                       Env& env) {
    Operand operand = unit_operand();
    BlockCursor cursor = lower_value(*expr.scrutinee, current, env, operand);
    if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }

    LocalId scrutinee_local = 0;
    cursor = ensure_local_operand(std::move(operand),
                                  expr.scrutinee->result_type.text,
                                  expr.scrutinee->result_type.discipline,
                                  cursor.block_id(),
                                  scrutinee_local);
    if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }

    std::vector<SwitchEdge> switch_edges;
    switch_edges.reserve(expr.arms.size());

    JoinBlockTarget join = JoinBlockTarget::awaiting_continuing_arm();
    for (const hir::MatchArm& arm : expr.arms) {
        const auto& pattern = static_cast<const hir::VariantPattern&>(*arm.pattern);
        const hir::VariantDecl& variant = hir::lookup_variant(hir_package, pattern.variant_id);
        BlockId arm_block = add_block();
        switch_edges.push_back(SwitchEdge::to_variant(variant.id, variant.name, arm_block));

        Env arm_env{&env, {}};
        bind_variant_pattern(pattern, variant, scrutinee_local, arm_block, arm_env);

        BlockCursor arm_tail = lower_expr_to(*arm.body, dest, arm_block, arm_env);
        if (arm_tail.state() == BlockCursorState::ContinuesAtBlock) {
            const BlockId join_block = ensure_join_block(join);
            set_terminator(arm_tail.block_id(), Terminator::jumps_to(join_block));
        }
    }

    set_terminator(cursor.block_id(), Terminator::switches_on_variant(scrutinee_local, std::move(switch_edges)));
    return join.cursor();
}

BlockCursor Builder::lower_yield_match(const hir::MatchExpr& expr,
                                       LocalId dest,
                                       BlockId current,
                                       Env& env) {
    if (expr.scrutinee->failure.effect() != hir::ExprFailureEffect::YieldsReason) {
        return lower_state_match(expr, dest, current, env);
    }

    LocalId success_local = make_temp(expr.scrutinee->result_type.text,
                                      expr.scrutinee->result_type.discipline,
                                      "success");
    const hir::TypeDecl& yielded_reason = hir::lookup_type(hir_package, expr.scrutinee->failure.reason_type_id());
    const typesys::UseDiscipline yielded_reason_discipline =
        yielded_reason.discipline.state() == hir::TypeDisciplineResolutionState::ConcreteUseDisciplineKnown
        ? yielded_reason.discipline.discipline()
        : typesys::UseDiscipline::Copyable;
    LocalId failure_local = make_temp(yielded_reason.qualified_name,
                                      yielded_reason_discipline,
                                      "failure");

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
    const BlockCursor lowered_scrutinee = lower_expr_to_success_or_failure(*expr.scrutinee,
                                                                           success_local,
                                                                           failure_local,
                                                                           success_block,
                                                                           failure_dispatch,
                                                                           current,
                                                                           env);
    if (lowered_scrutinee.state() == BlockCursorState::ContinuesAtBlock) {
        return lowered_scrutinee;
    }

    JoinBlockTarget join = JoinBlockTarget::awaiting_continuing_arm();
    if (success_arm != nullptr) {
        Env success_env{&env, {}};
        const auto& pattern = static_cast<const hir::SucceededPattern&>(*success_arm->pattern);
        if (pattern.binding == ast::SuccessPatternBinding::NamedBinding) {
            LocalId binding = add_local(pattern.binding_name,
                                        expr.scrutinee->result_type.text,
                                        LocalKind::Let,
                                        expr.scrutinee->result_type.discipline);
            success_env.locals[pattern.binding_name] = binding;
            append_statement(success_block,
                             Statement::assign(binding, use_rvalue(local_operand(success_local))));
        }

        BlockCursor success_tail = lower_expr_to(*success_arm->body, dest, success_block, success_env);
        if (success_tail.state() == BlockCursorState::ContinuesAtBlock) {
            const BlockId join_block = ensure_join_block(join);
            set_terminator(success_tail.block_id(), Terminator::jumps_to(join_block));
        }
    }

    std::vector<SwitchEdge> failure_edges;
    failure_edges.reserve(failure_arms.size());
    for (const hir::MatchArm* arm : failure_arms) {
        const auto& pattern = static_cast<const hir::FailedPattern&>(*arm->pattern);
        const hir::VariantDecl& variant = hir::lookup_variant(hir_package, pattern.variant->variant_id);
        BlockId arm_block = add_block();
        failure_edges.push_back(SwitchEdge::to_variant(variant.id, variant.name, arm_block));

        Env failure_env{&env, {}};
        bind_variant_pattern(*pattern.variant, variant, failure_local, arm_block, failure_env);

        BlockCursor arm_tail = lower_expr_to(*arm->body, dest, arm_block, failure_env);
        if (arm_tail.state() == BlockCursorState::ContinuesAtBlock) {
            const BlockId join_block = ensure_join_block(join);
            set_terminator(arm_tail.block_id(), Terminator::jumps_to(join_block));
        }
    }
    set_terminator(failure_dispatch, Terminator::switches_on_variant(failure_local, std::move(failure_edges)));
    return join.cursor();
}

BlockCursor Builder::lower_match_expr(const hir::MatchExpr& expr,
                                      LocalId dest,
                                      BlockId current,
                                      Env& env) {
    if (expr.scrutinee->failure.effect() == hir::ExprFailureEffect::YieldsReason) {
        return lower_yield_match(expr, dest, current, env);
    }
    return lower_state_match(expr, dest, current, env);
}

BlockCursor Builder::lower_grant_expr(const hir::GrantExpr& expr,
                                      LocalId dest,
                                      BlockId current,
                                      Env& env) {
    if (expr.failure.effect() == hir::ExprFailureEffect::YieldsReason) {
        return lower_failing_expr_to_propagation(expr, dest, current, env);
    }

    LocalId grant_dest = make_temp("Unit", typesys::UseDiscipline::Copyable, "grant");
    BlockCursor cursor = lower_direct_call(expr.grant_function_id, expr.grant_name, expr.args, grant_dest, current, env);
    if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }

    Env scoped{&env, {}};
    const LocalId binder = add_local(expr.binder_name,
                                     expr.permit_name,
                                     LocalKind::Let,
                                     typesys::UseDiscipline::ScopedAuthority);
    scoped.locals.emplace(expr.binder_name, binder);
    return lower_block_expr(*expr.body, dest, cursor.block_id(), scoped);
}

BlockCursor Builder::lower_field_access_expr(const hir::FieldAccessExpr& expr,
                                             LocalId dest,
                                             BlockId current,
                                             Env& env) {
    Operand base = unit_operand();
    BlockCursor cursor = lower_value(*expr.object, current, env, base);
    if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }
    LocalId base_local = 0;
    cursor = ensure_local_operand(std::move(base),
                                  expr.object->result_type.text,
                                  expr.object->result_type.discipline,
                                  cursor.block_id(),
                                  base_local);
    if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
        return BlockCursor::terminates_control_flow();
    }
    append_statement(cursor.block_id(),
                     Statement::assign(dest,
                                       Rvalue::projects_named_type_field(base_local, expr.field_name)));
    return cursor;
}

BlockCursor Builder::lower_prove_expr(const hir::ProveExpr& expr,
                                      LocalId dest,
                                      BlockId current,
                                      Env& env) {
    std::vector<FieldValue> fields;
    fields.reserve(expr.fields.size());
    BlockCursor cursor = BlockCursor::continues_at(current);
    for (const hir::FieldInit& field : expr.fields) {
        Operand operand = unit_operand();
        cursor = lower_value(*field.value, cursor.block_id(), env, operand);
        if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
            return BlockCursor::terminates_control_flow();
        }
        fields.push_back(FieldValue::named(field.name, std::move(operand)));
    }

    append_statement(cursor.block_id(),
                     Statement::assign(dest,
                                       Rvalue::constructs_named_type(expr.proof_type_id,
                                                                     expr.qualified_name,
                                                                     std::move(fields))));
    return cursor;
}

BlockCursor Builder::lower_expr_to(const hir::Expr& expr,
                                   LocalId dest,
                                   BlockId current,
                                   Env& env) {
    if (expr.failure.effect() == hir::ExprFailureEffect::YieldsReason
        && (expr.kind == hir::ExprKind::Call || expr.kind == hir::ExprKind::Grant
            || expr.kind == hir::ExprKind::Block)) {
        return lower_failing_expr_to_propagation(expr, dest, current, env);
    }

    switch (expr.kind) {
    case hir::ExprKind::NumberLiteral: {
        const auto& literal = static_cast<const hir::NumberLiteralExpr&>(expr);
        append_statement(current, Statement::assign(dest, use_rvalue(Operand::int_literal(literal.lexeme))));
        return BlockCursor::continues_at(current);
    }
    case hir::ExprKind::StringLiteral: {
        const auto& literal = static_cast<const hir::StringLiteralExpr&>(expr);
        append_statement(current, Statement::assign(dest, use_rvalue(Operand::string_literal(literal.lexeme))));
        return BlockCursor::continues_at(current);
    }
    case hir::ExprKind::Unit: {
        append_statement(current, Statement::assign(dest, use_rvalue(unit_operand())));
        return BlockCursor::continues_at(current);
    }
    case hir::ExprKind::LocalRef: {
        const auto& local_ref = static_cast<const hir::LocalRefExpr&>(expr);
        append_statement(
            current,
            Statement::assign(dest, use_rvalue(local_operand(lookup_local(env, local_ref.name)))));
        return BlockCursor::continues_at(current);
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
        const typesys::UseDiscipline reason_discipline =
            reason_type.discipline.state() == hir::TypeDisciplineResolutionState::ConcreteUseDisciplineKnown
            ? reason_type.discipline.discipline()
            : typesys::UseDiscipline::Copyable;
        LocalId reason_local = make_temp(reason_type.qualified_name,
                                         reason_discipline,
                                         "reason");
        std::vector<FieldValue> reason_fields;
        reason_fields.reserve(fail.fields.size());
        BlockCursor cursor = BlockCursor::continues_at(current);
        for (const hir::FieldInit& field : fail.fields) {
            Operand operand = unit_operand();
            cursor = lower_value(*field.value, cursor.block_id(), env, operand);
            if (cursor.state() == BlockCursorState::TerminatesControlFlow) {
                return BlockCursor::terminates_control_flow();
            }
            reason_fields.push_back(FieldValue::named(field.name, std::move(operand)));
        }

        append_statement(cursor.block_id(),
                         Statement::assign(reason_local,
                                           Rvalue::constructs_named_variant(fail.reason_type_id,
                                                                            fail.variant_id,
                                                                            fail.reason_name + "::" + fail.variant_name,
                                                                            std::move(reason_fields))));
        set_terminator(cursor.block_id(), Terminator::fails(local_operand(reason_local)));
        return BlockCursor::terminates_control_flow();
    }
    case hir::ExprKind::Grant:
        return lower_grant_expr(static_cast<const hir::GrantExpr&>(expr), dest, current, env);
    case hir::ExprKind::FieldAccess:
        return lower_field_access_expr(static_cast<const hir::FieldAccessExpr&>(expr), dest, current, env);
    case hir::ExprKind::Prove:
        return lower_prove_expr(static_cast<const hir::ProveExpr&>(expr), dest, current, env);
    }
    return BlockCursor::continues_at(current);
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
            << (function.implementation == ast::FunctionImplementation::ForeignImport ? "foreign-fn " : "fn ")
            << function.qualified_name
            << " -> " << function.return_type;
        if (function.failure.behavior() == FunctionFailureBehavior::YieldsReason) {
            out << " fails " << function.failure.reason_type();
        }
        if (function.authority.effect() == FunctionAuthorityEffect::GrantsScopedPermit) {
            out << " grants " << function.authority.permit_type();
        }
        for (const std::string& p : function.proves_types) {
            out << " proves " << p;
        }
        out << '\n';
        if (!function.locals.empty()) {
            out << "      locals:\n";
            for (const Local& local : function.locals) {
                out << "        " << format_local_name(local) << " : " << local.type_name();
                if (typesys::discipline_materialization(local.discipline())
                    == typesys::DisciplineMaterialization::CompileTimeOnly) {
                    out << " [compile-time-only]";
                }
                if (typesys::discipline_movement(local.discipline()) == typesys::DisciplineMovement::Affine) {
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
                out << "        " << format_local_name(function.locals.at(statement.dest_local()))
                    << " = " << format_rvalue(statement.rvalue(), function) << '\n';
            }
            block.terminator.match(
                [&](Terminator::ReturnValue terminator) {
                    out << "        return " << format_operand(terminator.value, function) << '\n';
                },
                [&](Terminator::FailValue terminator) {
                    out << "        fail " << format_operand(terminator.reason, function) << '\n';
                },
                [&](Terminator::GotoValue terminator) {
                    out << "        goto %" << terminator.target_block << '\n';
                },
                [&](Terminator::SwitchVariantValue terminator) {
                    out << "        switch " << format_local_name(function.locals.at(terminator.scrutinee_local)) << '\n';
                    for (const SwitchEdge& edge : terminator.edges) {
                        out << "          arm " << edge.variant_name() << " -> %" << edge.target_block() << '\n';
                    }
                },
                [&](Terminator::InvokeValue terminator) {
                    out << "        invoke " << terminator.callee_name << '(';
                    for (std::size_t index = 0; index < terminator.args.size(); ++index) {
                        if (index > 0) {
                            out << ", ";
                        }
                        out << format_operand(terminator.args[index], function);
                    }
                    out << ") ok " << format_local_name(function.locals.at(terminator.success_local))
                        << " -> %" << terminator.success_block
                        << " fail " << format_local_name(function.locals.at(terminator.failure_local))
                        << " -> %" << terminator.failure_block << '\n';
                },
                [&](Terminator::UnreachableValue) {
                    out << "        unreachable\n";
                });
        }
    }
    return out.str();
}

} // namespace evident::mir
