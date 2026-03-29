#pragma once

#include "evident/Hir.hpp"

#include <cstddef>
#include <optional>
#include <string>
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

enum class OperandKind {
    Local,
    IntLiteral,
    StringLiteral,
    Unit,
};

enum class RvalueKind {
    Use,
    Call,
    Construct,
    ProjectField,
};

enum class StatementKind {
    Assign,
};

enum class TerminatorKind {
    Return,
    Fail,
    Goto,
    SwitchVariant,
    Invoke,
    Unreachable,
};

struct Operand {
    OperandKind kind = OperandKind::Unit;
    LocalId local_id = 0;
    std::string text;
};

struct FieldValue {
    std::string name;
    Operand value;
};

struct Rvalue {
    RvalueKind kind = RvalueKind::Use;
    Operand operand;
    hir::FunctionId function_id = 0;
    std::string callee_name;
    std::vector<Operand> args;
    hir::TypeId owner_type_id = 0;
    std::optional<hir::VariantId> variant_id;
    std::string qualified_name;
    std::vector<FieldValue> fields;
    LocalId base_local = 0;
    hir::TypeId projection_owner_type_id = 0;
    std::optional<hir::VariantId> projection_variant_id;
    std::string field_name;
};

struct Statement {
    StatementKind kind = StatementKind::Assign;
    LocalId dest_local = 0;
    Rvalue value;
};

struct SwitchEdge {
    hir::VariantId variant_id = 0;
    std::string variant_name;
    BlockId target_block = 0;
};

struct Terminator {
    TerminatorKind kind = TerminatorKind::Unreachable;
    std::optional<Operand> value;
    BlockId target_block = 0;
    LocalId scrutinee_local = 0;
    std::vector<SwitchEdge> edges;
    hir::FunctionId function_id = 0;
    std::string callee_name;
    std::vector<Operand> args;
    LocalId success_local = 0;
    LocalId failure_local = 0;
    BlockId success_block = 0;
    BlockId failure_block = 0;
};

struct Local {
    LocalId id = 0;
    LocalKind kind = LocalKind::Temporary;
    std::string name;
    std::string type;
    bool is_compile_time_only = false;
    bool is_affine = false;
};

struct BasicBlock {
    BlockId id = 0;
    std::vector<Statement> statements;
    Terminator terminator;
    bool has_terminator = false;
};

struct Function {
    hir::FunctionId function_id = 0;
    ast::Visibility visibility = ast::Visibility::Private;
    std::string qualified_name;
    std::string return_type;
    std::optional<std::string> yields_type;
    std::optional<std::string> grants_type;
    std::optional<std::string> proves_type;
    bool is_foreign = false;
    std::vector<Local> locals;
    std::vector<BasicBlock> blocks;
};

struct Package {
    std::vector<Function> functions;
};

[[nodiscard]] Package lower(const hir::Package& package);
[[nodiscard]] std::string dump(const Package& package);

} // namespace evident::mir
