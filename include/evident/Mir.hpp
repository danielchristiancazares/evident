#pragma once

#include "evident/Hir.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace evident::mir {

using LocalId = std::size_t;
using BlockId = std::size_t;

enum class LocalKind {
    Parameter,
    Let,
    Temporary,
    ReturnSlot,
};

class Operand final {
private:
    enum class Kind {
        Local,
        IntLiteral,
        StringLiteral,
        Unit,
    };

public:
    struct LocalValue final {
        LocalId local_id;
    };

    struct IntLiteralValue final {
        const std::string& text;
    };

    struct StringLiteralValue final {
        const std::string& text;
    };

    struct UnitValue final {};

    [[nodiscard]] static Operand local(LocalId local_id);
    [[nodiscard]] static Operand int_literal(std::string text);
    [[nodiscard]] static Operand string_literal(std::string text);
    [[nodiscard]] static Operand unit();

    template <typename LocalFn, typename IntLiteralFn, typename StringLiteralFn, typename UnitFn>
    decltype(auto) match(LocalFn&& on_local,
                         IntLiteralFn&& on_int_literal,
                         StringLiteralFn&& on_string_literal,
                         UnitFn&& on_unit) const {
        switch (kind_) {
        case Kind::Local:
            return std::forward<LocalFn>(on_local)(LocalValue{local_id_});
        case Kind::IntLiteral:
            return std::forward<IntLiteralFn>(on_int_literal)(IntLiteralValue{text_});
        case Kind::StringLiteral:
            return std::forward<StringLiteralFn>(on_string_literal)(StringLiteralValue{text_});
        case Kind::Unit:
            return std::forward<UnitFn>(on_unit)(UnitValue{});
        }
        return std::forward<UnitFn>(on_unit)(UnitValue{});
    }

    template <typename LocalFn, typename MaterializedFn>
    decltype(auto) match_local_or_materialized(LocalFn&& on_local, MaterializedFn&& on_materialized) && {
        if (kind_ == Kind::Local) {
            return std::forward<LocalFn>(on_local)(LocalValue{local_id_});
        }
        return std::forward<MaterializedFn>(on_materialized)(std::move(*this));
    }

private:
    Kind kind_;
    LocalId local_id_;
    std::string text_;

    Operand(Kind kind, LocalId local_id, std::string text);
};

class FieldValue final {
public:
    [[nodiscard]] static FieldValue named(std::string field_name, Operand operand);

    [[nodiscard]] const std::string& field_name() const noexcept { return field_name_; }
    [[nodiscard]] const Operand& operand() const noexcept { return operand_; }

private:
    std::string field_name_;
    Operand operand_;

    FieldValue(std::string field_name, Operand operand);
};

class Rvalue final {
private:
    enum class Kind {
        Use,
        Call,
        ConstructNamedType,
        ConstructNamedVariant,
        ProjectNamedTypeField,
        ProjectNamedVariantPayloadField,
    };

public:
    struct UseValue final {
        const Operand& operand;
    };

    struct CallValue final {
        hir::FunctionId function_id;
        const std::string& callee_name;
        const std::vector<Operand>& args;
    };

    struct ConstructNamedTypeValue final {
        hir::TypeId owner_type_id;
        const std::string& qualified_name;
        const std::vector<FieldValue>& fields;
    };

    struct ConstructNamedVariantValue final {
        hir::TypeId owner_type_id;
        hir::VariantId variant_id;
        const std::string& qualified_name;
        const std::vector<FieldValue>& fields;
    };

    struct ProjectNamedTypeFieldValue final {
        LocalId base_local;
        const std::string& field_name;
    };

    struct ProjectNamedVariantPayloadFieldValue final {
        LocalId base_local;
        hir::TypeId projection_owner_type_id;
        hir::VariantId variant_id;
        const std::string& field_name;
    };

    [[nodiscard]] static Rvalue uses(Operand operand);
    [[nodiscard]] static Rvalue calls(hir::FunctionId function_id,
                                      std::string callee_name,
                                      std::vector<Operand> args);
    [[nodiscard]] static Rvalue constructs_named_type(hir::TypeId owner_type_id,
                                                      std::string qualified_name,
                                                      std::vector<FieldValue> fields);
    [[nodiscard]] static Rvalue constructs_named_variant(hir::TypeId owner_type_id,
                                                         hir::VariantId variant_id,
                                                         std::string qualified_name,
                                                         std::vector<FieldValue> fields);
    [[nodiscard]] static Rvalue projects_named_type_field(LocalId base_local,
                                                          std::string field_name);
    [[nodiscard]] static Rvalue projects_named_variant_payload_field(LocalId base_local,
                                                                     hir::TypeId projection_owner_type_id,
                                                                     hir::VariantId variant_id,
                                                                     std::string field_name);

    template <typename UseFn,
              typename CallFn,
              typename ConstructNamedTypeFn,
              typename ConstructNamedVariantFn,
              typename ProjectNamedTypeFieldFn,
              typename ProjectNamedVariantPayloadFieldFn>
    decltype(auto) match(UseFn&& on_use,
                         CallFn&& on_call,
                         ConstructNamedTypeFn&& on_construct_named_type,
                         ConstructNamedVariantFn&& on_construct_named_variant,
                         ProjectNamedTypeFieldFn&& on_project_named_type_field,
                         ProjectNamedVariantPayloadFieldFn&& on_project_named_variant_payload_field) const {
        switch (kind_) {
        case Kind::Use:
            return std::forward<UseFn>(on_use)(UseValue{operand_});
        case Kind::Call:
            return std::forward<CallFn>(on_call)(CallValue{function_id_, callee_name_, args_});
        case Kind::ConstructNamedType:
            return std::forward<ConstructNamedTypeFn>(on_construct_named_type)(
                ConstructNamedTypeValue{owner_type_id_, qualified_name_, fields_});
        case Kind::ConstructNamedVariant:
            return std::forward<ConstructNamedVariantFn>(on_construct_named_variant)(
                ConstructNamedVariantValue{owner_type_id_, variant_id_, qualified_name_, fields_});
        case Kind::ProjectNamedTypeField:
            return std::forward<ProjectNamedTypeFieldFn>(on_project_named_type_field)(
                ProjectNamedTypeFieldValue{base_local_, field_name_});
        case Kind::ProjectNamedVariantPayloadField:
            return std::forward<ProjectNamedVariantPayloadFieldFn>(on_project_named_variant_payload_field)(
                ProjectNamedVariantPayloadFieldValue{
                    base_local_,
                    projection_owner_type_id_,
                    variant_id_,
                    field_name_,
                });
        }
        return std::forward<UseFn>(on_use)(UseValue{operand_});
    }

private:
    Kind kind_;
    Operand operand_;
    hir::FunctionId function_id_;
    std::string callee_name_;
    std::vector<Operand> args_;
    hir::TypeId owner_type_id_;
    hir::VariantId variant_id_;
    std::string qualified_name_;
    std::vector<FieldValue> fields_;
    LocalId base_local_;
    hir::TypeId projection_owner_type_id_;
    std::string field_name_;

    Rvalue(Kind kind,
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
           std::string field_name);
};

class Statement final {
public:
    [[nodiscard]] static Statement assign(LocalId dest_local, Rvalue value);

    [[nodiscard]] LocalId dest_local() const noexcept { return dest_local_; }
    [[nodiscard]] const Rvalue& rvalue() const noexcept { return value_; }

private:
    LocalId dest_local_;
    Rvalue value_;

    Statement(LocalId dest_local, Rvalue value);
};

class SwitchEdge final {
public:
    [[nodiscard]] static SwitchEdge to_variant(hir::VariantId variant_id,
                                               std::string variant_name,
                                               BlockId target_block);

    [[nodiscard]] hir::VariantId variant_id() const noexcept { return variant_id_; }
    [[nodiscard]] const std::string& variant_name() const noexcept { return variant_name_; }
    [[nodiscard]] BlockId target_block() const noexcept { return target_block_; }

private:
    hir::VariantId variant_id_;
    std::string variant_name_;
    BlockId target_block_;

    SwitchEdge(hir::VariantId variant_id, std::string variant_name, BlockId target_block);
};

class Terminator final {
private:
    enum class Kind {
        Return,
        Fail,
        Goto,
        SwitchVariant,
        Invoke,
        Unreachable,
    };

public:
    struct ReturnValue final {
        const Operand& value;
    };

    struct FailValue final {
        const Operand& reason;
    };

    struct GotoValue final {
        BlockId target_block;
    };

    struct SwitchVariantValue final {
        LocalId scrutinee_local;
        const std::vector<SwitchEdge>& edges;
    };

    struct InvokeValue final {
        hir::FunctionId function_id;
        const std::string& callee_name;
        const std::vector<Operand>& args;
        LocalId success_local;
        LocalId failure_local;
        BlockId success_block;
        BlockId failure_block;
    };

    struct UnreachableValue final {};

    [[nodiscard]] static Terminator returns(Operand value);
    [[nodiscard]] static Terminator fails(Operand reason);
    [[nodiscard]] static Terminator jumps_to(BlockId target_block);
    [[nodiscard]] static Terminator switches_on_variant(LocalId scrutinee_local,
                                                        std::vector<SwitchEdge> edges);
    [[nodiscard]] static Terminator invokes(hir::FunctionId function_id,
                                            std::string callee_name,
                                            std::vector<Operand> args,
                                            LocalId success_local,
                                            LocalId failure_local,
                                            BlockId success_block,
                                            BlockId failure_block);
    [[nodiscard]] static Terminator unreachable();

    template <typename ReturnFn,
              typename FailFn,
              typename GotoFn,
              typename SwitchVariantFn,
              typename InvokeFn,
              typename UnreachableFn>
    decltype(auto) match(ReturnFn&& on_return,
                         FailFn&& on_fail,
                         GotoFn&& on_goto,
                         SwitchVariantFn&& on_switch_variant,
                         InvokeFn&& on_invoke,
                         UnreachableFn&& on_unreachable) const {
        switch (kind_) {
        case Kind::Return:
            return std::forward<ReturnFn>(on_return)(ReturnValue{operand_});
        case Kind::Fail:
            return std::forward<FailFn>(on_fail)(FailValue{operand_});
        case Kind::Goto:
            return std::forward<GotoFn>(on_goto)(GotoValue{target_block_});
        case Kind::SwitchVariant:
            return std::forward<SwitchVariantFn>(on_switch_variant)(
                SwitchVariantValue{scrutinee_local_, edges_});
        case Kind::Invoke:
            return std::forward<InvokeFn>(on_invoke)(InvokeValue{
                function_id_,
                callee_name_,
                args_,
                success_local_,
                failure_local_,
                success_block_,
                failure_block_,
            });
        case Kind::Unreachable:
            return std::forward<UnreachableFn>(on_unreachable)(UnreachableValue{});
        }
        return std::forward<UnreachableFn>(on_unreachable)(UnreachableValue{});
    }

private:
    Kind kind_;
    Operand operand_;
    BlockId target_block_;
    LocalId scrutinee_local_;
    std::vector<SwitchEdge> edges_;
    hir::FunctionId function_id_;
    std::string callee_name_;
    std::vector<Operand> args_;
    LocalId success_local_;
    LocalId failure_local_;
    BlockId success_block_;
    BlockId failure_block_;

    Terminator(Kind kind,
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
               BlockId failure_block);
};

class Local final {
public:
    [[nodiscard]] static Local parameter(LocalId id,
                                         std::string name,
                                         std::string type_name,
                                         typesys::UseDiscipline discipline);
    [[nodiscard]] static Local let_binding(LocalId id,
                                           std::string name,
                                           std::string type_name,
                                           typesys::UseDiscipline discipline);
    [[nodiscard]] static Local temporary(LocalId id,
                                         std::string name,
                                         std::string type_name,
                                         typesys::UseDiscipline discipline);
    [[nodiscard]] static Local return_slot(LocalId id,
                                           std::string name,
                                           std::string type_name,
                                           typesys::UseDiscipline discipline);

    [[nodiscard]] LocalId id() const noexcept { return id_; }
    [[nodiscard]] LocalKind kind() const noexcept { return kind_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& type_name() const noexcept { return type_name_; }
    [[nodiscard]] typesys::UseDiscipline discipline() const noexcept { return discipline_; }

private:
    LocalId id_;
    LocalKind kind_;
    std::string name_;
    std::string type_name_;
    typesys::UseDiscipline discipline_;

    Local(LocalId id,
          LocalKind kind,
          std::string name,
          std::string type_name,
          typesys::UseDiscipline discipline);
};

struct BasicBlock {
    BlockId id = 0;
    std::vector<Statement> statements;
    Terminator terminator;
};

enum class FunctionFailureBehavior {
    ReturnsDeclaredValue,
    YieldsReason,
};

enum class FunctionAuthorityEffect {
    OrdinaryCall,
    GrantsScopedPermit,
};

class FunctionFailureContract final {
public:
    [[nodiscard]] static FunctionFailureContract returns_declared_value();
    [[nodiscard]] static FunctionFailureContract yields_reason(std::string reason_type);

    [[nodiscard]] FunctionFailureBehavior behavior() const noexcept { return behavior_; }
    [[nodiscard]] const std::string& reason_type() const noexcept { return reason_type_; }

private:
    FunctionFailureBehavior behavior_;
    std::string reason_type_;

    FunctionFailureContract(FunctionFailureBehavior behavior, std::string reason_type);
};

class FunctionAuthorityContract final {
public:
    [[nodiscard]] static FunctionAuthorityContract ordinary_call();
    [[nodiscard]] static FunctionAuthorityContract grants_scoped_permit(std::string permit_type);

    [[nodiscard]] FunctionAuthorityEffect effect() const noexcept { return effect_; }
    [[nodiscard]] const std::string& permit_type() const noexcept { return permit_type_; }

private:
    FunctionAuthorityEffect effect_;
    std::string permit_type_;

    FunctionAuthorityContract(FunctionAuthorityEffect effect, std::string permit_type);
};

struct Function {
    hir::FunctionId function_id = 0;
    ast::Visibility visibility = ast::Visibility::Private;
    std::string qualified_name;
    std::string return_type;
    FunctionFailureContract failure = FunctionFailureContract::returns_declared_value();
    FunctionAuthorityContract authority = FunctionAuthorityContract::ordinary_call();
    std::vector<std::string> proves_types;
    ast::FunctionImplementation implementation = ast::FunctionImplementation::EvidentBody;
    std::vector<Local> locals;
    std::vector<BasicBlock> blocks;
};

struct Package {
    std::vector<Function> functions;
};

[[nodiscard]] Package lower(const hir::Package& package);
[[nodiscard]] std::string dump(const Package& package);

} // namespace evident::mir
