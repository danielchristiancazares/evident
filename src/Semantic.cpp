#include "evident/Semantic.hpp"
#include "evident/ResolvedType.hpp"
#include "evident/Source.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
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
    "Unknown",     "Known",        "Invalid",    "Unset",        "Reported",
    "NotReported", "Any",          "All",        "Unrestricted", "AllowAll",
};

const std::unordered_set<std::string_view> kPseudoOptionalNames = {
    "Present", "Absent", "Missing", "Some", "None", "Known", "Unknown",
};

const std::unordered_set<std::string_view> kSemanticGenericNames = {
    "Validated",     "Authorized",     "Settled",  "Draft",
    "Validation",    "Authorization",  "Authority", "Proof",
    "Permit",        "validated",      "authorized", "settled",
    "draft",         "validation",     "authorization", "authority",
    "proof",         "permit",
};

const std::unordered_set<std::string_view> kBuiltins = {
    "Int",   "Nat",   "Float",  "Char",  "Text",    "Bytes",
    "Never", "List",  "NonEmptyList",  "Map",   "NonEmptyMap",    "CString",
    "CInt",  "CSize", "Byte",   "Unit",
};

const std::unordered_set<std::string_view> kCanonicalMapKeyBuiltins = {
    "Int",
    "Nat",
    "Float",
    "Byte",
    "Char",
    "Text",
    "Bytes",
};

const std::unordered_set<std::string_view> kCompilerOwnedCollectionNames = {
    "ListCardinalityFailure",
    "MapCardinalityFailure",
    "MapBindingFailure",
    "MapMergeFailure",
    "ListHadNoElements",
    "MapHadNoEntries",
    "RequestedKeyHadNoBinding",
    "RequestedKeyAlreadyHadBinding",
    "InputsHadSharedKey",
    "ListFirstAndRest",
    "MapEntry",
    "MapFirstEntryAndRest",
    "MapBoundValueAndRest",
    "list_empty",
    "list_single",
    "list_prepend",
    "list_append",
    "list_concat",
    "nonempty_list_concat_left",
    "nonempty_list_concat_right",
    "nonempty_list_concat",
    "list_require_nonempty",
    "nonempty_list_widen",
    "list_count_copy",
    "nonempty_list_count_copy",
    "nonempty_list_first_copy",
    "nonempty_list_consume_first",
    "map_empty",
    "map_single",
    "map_require_nonempty",
    "nonempty_map_widen",
    "map_bind_new",
    "nonempty_map_bind_new",
    "map_replace_bound",
    "nonempty_map_replace_bound",
    "map_bind_or_replace",
    "nonempty_map_bind_or_replace",
    "map_remove_bound",
    "nonempty_map_remove_bound",
    "map_consume_bound_value",
    "nonempty_map_consume_bound_value",
    "map_count_copy",
    "nonempty_map_count_copy",
    "map_lookup_copy",
    "nonempty_map_lookup_copy",
    "nonempty_map_first_entry_copy",
    "nonempty_map_consume_first_entry",
    "map_entries_copy",
    "nonempty_map_entries_copy",
    "map_consume_entries",
    "nonempty_map_consume_entries",
    "map_merge_rejecting_shared_keys",
    "map_merge_left_nonempty_rejecting_shared_keys",
    "map_merge_right_nonempty_rejecting_shared_keys",
    "nonempty_map_merge_rejecting_shared_keys",
    "map_merge_using_left_bindings_for_shared_keys",
    "map_merge_left_nonempty_using_left_bindings_for_shared_keys",
    "map_merge_right_nonempty_using_left_bindings_for_shared_keys",
    "nonempty_map_merge_using_left_bindings_for_shared_keys",
    "map_merge_using_right_bindings_for_shared_keys",
    "map_merge_left_nonempty_using_right_bindings_for_shared_keys",
    "map_merge_right_nonempty_using_right_bindings_for_shared_keys",
    "nonempty_map_merge_using_right_bindings_for_shared_keys",
    "map_from_entries_rejecting_shared_keys",
    "nonempty_map_from_entries_rejecting_shared_keys",
    "map_from_entries_using_first_bindings",
    "nonempty_map_from_entries_using_first_bindings",
    "map_from_entries_using_last_bindings",
    "nonempty_map_from_entries_using_last_bindings",
    "text_length",
    "bytes_length",
};

const std::unordered_set<std::string_view> kCompilerOwnedCollectionCompanionRecordNames = {
    "ListFirstAndRest",
    "MapEntry",
    "MapFirstEntryAndRest",
    "MapBoundValueAndRest",
};

const std::unordered_map<std::string_view, std::vector<std::string_view>> kCompilerOwnedCollectionReasonVariants = {
    {"ListCardinalityFailure", {"ListHadNoElements"}},
    {"MapCardinalityFailure", {"MapHadNoEntries"}},
    {"MapBindingFailure", {"RequestedKeyHadNoBinding", "RequestedKeyAlreadyHadBinding"}},
    {"MapMergeFailure", {"InputsHadSharedKey"}},
};

enum class CollectionTypeTemplate {
    GenericT,
    GenericK,
    GenericV,
    Nat,
    Text,
    Bytes,
    ListT,
    NonEmptyListT,
    ListFirstAndRestT,
    MapKV,
    NonEmptyMapKV,
    MapEntryKV,
    MapFirstEntryAndRestKV,
    MapBoundValueAndRestKV,
    ListMapEntryKV,
    NonEmptyListMapEntryKV,
};

struct CollectionFunctionParamSpec {
    std::string_view name;
    CollectionTypeTemplate type;
};

struct CollectionFunctionSpec {
    std::string_view name;
    std::vector<std::string_view> generic_params;
    std::vector<CollectionFunctionParamSpec> params;
    CollectionTypeTemplate return_type;
    std::string_view failure_reason;
};

struct CollectionCopyabilityRequirement {
    std::string_view generic_param;
    std::string_view payload_description;
};

const std::vector<CollectionFunctionSpec> kCompilerOwnedCollectionFunctionSpecs = {
    {"list_empty", {"T"}, {}, CollectionTypeTemplate::ListT, {}},
    {"list_single", {"T"}, {{"value", CollectionTypeTemplate::GenericT}}, CollectionTypeTemplate::NonEmptyListT, {}},
    {"list_prepend",
     {"T"},
     {{"value", CollectionTypeTemplate::GenericT}, {"values", CollectionTypeTemplate::ListT}},
     CollectionTypeTemplate::NonEmptyListT,
     {}},
    {"list_append",
     {"T"},
     {{"values", CollectionTypeTemplate::ListT}, {"value", CollectionTypeTemplate::GenericT}},
     CollectionTypeTemplate::NonEmptyListT,
     {}},
    {"list_concat",
     {"T"},
     {{"left", CollectionTypeTemplate::ListT}, {"right", CollectionTypeTemplate::ListT}},
     CollectionTypeTemplate::ListT,
     {}},
    {"nonempty_list_concat_left",
     {"T"},
     {{"left", CollectionTypeTemplate::NonEmptyListT}, {"right", CollectionTypeTemplate::ListT}},
     CollectionTypeTemplate::NonEmptyListT,
     {}},
    {"nonempty_list_concat_right",
     {"T"},
     {{"left", CollectionTypeTemplate::ListT}, {"right", CollectionTypeTemplate::NonEmptyListT}},
     CollectionTypeTemplate::NonEmptyListT,
     {}},
    {"nonempty_list_concat",
     {"T"},
     {{"left", CollectionTypeTemplate::NonEmptyListT}, {"right", CollectionTypeTemplate::NonEmptyListT}},
     CollectionTypeTemplate::NonEmptyListT,
     {}},
    {"list_require_nonempty",
     {"T"},
     {{"values", CollectionTypeTemplate::ListT}},
     CollectionTypeTemplate::NonEmptyListT,
     "ListCardinalityFailure"},
    {"nonempty_list_widen",
     {"T"},
     {{"values", CollectionTypeTemplate::NonEmptyListT}},
     CollectionTypeTemplate::ListT,
     {}},
    {"list_count_copy", {"T"}, {{"values", CollectionTypeTemplate::ListT}}, CollectionTypeTemplate::Nat, {}},
    {"nonempty_list_count_copy",
     {"T"},
     {{"values", CollectionTypeTemplate::NonEmptyListT}},
     CollectionTypeTemplate::Nat,
     {}},
    {"text_length", {}, {{"text", CollectionTypeTemplate::Text}}, CollectionTypeTemplate::Nat, {}},
    {"bytes_length", {}, {{"bytes", CollectionTypeTemplate::Bytes}}, CollectionTypeTemplate::Nat, {}},
    {"nonempty_list_first_copy",
     {"T"},
     {{"values", CollectionTypeTemplate::NonEmptyListT}},
     CollectionTypeTemplate::GenericT,
     {}},
    {"nonempty_list_consume_first",
     {"T"},
     {{"values", CollectionTypeTemplate::NonEmptyListT}},
     CollectionTypeTemplate::ListFirstAndRestT,
     {}},
    {"map_empty", {"K", "V"}, {}, CollectionTypeTemplate::MapKV, {}},
    {"map_single",
     {"K", "V"},
     {{"key", CollectionTypeTemplate::GenericK}, {"value", CollectionTypeTemplate::GenericV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"map_require_nonempty",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapCardinalityFailure"},
    {"nonempty_map_widen",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::MapKV,
     {}},
    {"map_bind_new",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV},
      {"key", CollectionTypeTemplate::GenericK},
      {"value", CollectionTypeTemplate::GenericV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapBindingFailure"},
    {"nonempty_map_bind_new",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV},
      {"key", CollectionTypeTemplate::GenericK},
      {"value", CollectionTypeTemplate::GenericV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapBindingFailure"},
    {"map_replace_bound",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV},
      {"key", CollectionTypeTemplate::GenericK},
      {"value", CollectionTypeTemplate::GenericV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapBindingFailure"},
    {"nonempty_map_replace_bound",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV},
      {"key", CollectionTypeTemplate::GenericK},
      {"value", CollectionTypeTemplate::GenericV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapBindingFailure"},
    {"map_bind_or_replace",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV},
      {"key", CollectionTypeTemplate::GenericK},
      {"value", CollectionTypeTemplate::GenericV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"nonempty_map_bind_or_replace",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV},
      {"key", CollectionTypeTemplate::GenericK},
      {"value", CollectionTypeTemplate::GenericV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"map_remove_bound",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV}, {"key", CollectionTypeTemplate::GenericK}},
     CollectionTypeTemplate::MapKV,
     "MapBindingFailure"},
    {"nonempty_map_remove_bound",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}, {"key", CollectionTypeTemplate::GenericK}},
     CollectionTypeTemplate::MapKV,
     "MapBindingFailure"},
    {"map_consume_bound_value",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV}, {"key", CollectionTypeTemplate::GenericK}},
     CollectionTypeTemplate::MapBoundValueAndRestKV,
     "MapBindingFailure"},
    {"nonempty_map_consume_bound_value",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}, {"key", CollectionTypeTemplate::GenericK}},
     CollectionTypeTemplate::MapBoundValueAndRestKV,
     "MapBindingFailure"},
    {"map_count_copy", {"K", "V"}, {{"entries", CollectionTypeTemplate::MapKV}}, CollectionTypeTemplate::Nat, {}},
    {"nonempty_map_count_copy",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::Nat,
     {}},
    {"map_lookup_copy",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV}, {"key", CollectionTypeTemplate::GenericK}},
     CollectionTypeTemplate::GenericV,
     "MapBindingFailure"},
    {"nonempty_map_lookup_copy",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}, {"key", CollectionTypeTemplate::GenericK}},
     CollectionTypeTemplate::GenericV,
     "MapBindingFailure"},
    {"nonempty_map_first_entry_copy",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::MapEntryKV,
     {}},
    {"nonempty_map_consume_first_entry",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::MapFirstEntryAndRestKV,
     {}},
    {"map_entries_copy",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::ListMapEntryKV,
     {}},
    {"nonempty_map_entries_copy",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::NonEmptyListMapEntryKV,
     {}},
    {"map_consume_entries",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::ListMapEntryKV,
     {}},
    {"nonempty_map_consume_entries",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::NonEmptyListMapEntryKV,
     {}},
    {"map_merge_rejecting_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::MapKV}, {"right", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::MapKV,
     "MapMergeFailure"},
    {"map_merge_left_nonempty_rejecting_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::NonEmptyMapKV}, {"right", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapMergeFailure"},
    {"map_merge_right_nonempty_rejecting_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::MapKV}, {"right", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapMergeFailure"},
    {"nonempty_map_merge_rejecting_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::NonEmptyMapKV}, {"right", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapMergeFailure"},
    {"map_merge_using_left_bindings_for_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::MapKV}, {"right", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::MapKV,
     {}},
    {"map_merge_left_nonempty_using_left_bindings_for_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::NonEmptyMapKV}, {"right", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"map_merge_right_nonempty_using_left_bindings_for_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::MapKV}, {"right", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"nonempty_map_merge_using_left_bindings_for_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::NonEmptyMapKV}, {"right", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"map_merge_using_right_bindings_for_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::MapKV}, {"right", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::MapKV,
     {}},
    {"map_merge_left_nonempty_using_right_bindings_for_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::NonEmptyMapKV}, {"right", CollectionTypeTemplate::MapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"map_merge_right_nonempty_using_right_bindings_for_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::MapKV}, {"right", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"nonempty_map_merge_using_right_bindings_for_shared_keys",
     {"K", "V"},
     {{"left", CollectionTypeTemplate::NonEmptyMapKV}, {"right", CollectionTypeTemplate::NonEmptyMapKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"map_from_entries_rejecting_shared_keys",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::ListMapEntryKV}},
     CollectionTypeTemplate::MapKV,
     "MapMergeFailure"},
    {"nonempty_map_from_entries_rejecting_shared_keys",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyListMapEntryKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     "MapMergeFailure"},
    {"map_from_entries_using_first_bindings",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::ListMapEntryKV}},
     CollectionTypeTemplate::MapKV,
     {}},
    {"nonempty_map_from_entries_using_first_bindings",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyListMapEntryKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
    {"map_from_entries_using_last_bindings",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::ListMapEntryKV}},
     CollectionTypeTemplate::MapKV,
     {}},
    {"nonempty_map_from_entries_using_last_bindings",
     {"K", "V"},
     {{"entries", CollectionTypeTemplate::NonEmptyListMapEntryKV}},
     CollectionTypeTemplate::NonEmptyMapKV,
     {}},
};

const std::unordered_map<std::string_view, CollectionCopyabilityRequirement>
    kCompilerOwnedCollectionCopyabilityRequirements = {
        {"list_count_copy", {"T", "payload"}},
        {"nonempty_list_count_copy", {"T", "payload"}},
        {"nonempty_list_first_copy", {"T", "payload"}},
        {"map_replace_bound", {"V", "map value"}},
        {"nonempty_map_replace_bound", {"V", "map value"}},
        {"map_bind_or_replace", {"V", "map value"}},
        {"nonempty_map_bind_or_replace", {"V", "map value"}},
        {"map_remove_bound", {"V", "map value"}},
        {"nonempty_map_remove_bound", {"V", "map value"}},
        {"map_count_copy", {"V", "map value"}},
        {"nonempty_map_count_copy", {"V", "map value"}},
        {"map_lookup_copy", {"V", "map value"}},
        {"nonempty_map_lookup_copy", {"V", "map value"}},
        {"nonempty_map_first_entry_copy", {"V", "map value"}},
        {"map_entries_copy", {"V", "map value"}},
        {"nonempty_map_entries_copy", {"V", "map value"}},
        {"map_merge_using_left_bindings_for_shared_keys", {"V", "map value"}},
        {"map_merge_left_nonempty_using_left_bindings_for_shared_keys", {"V", "map value"}},
        {"map_merge_right_nonempty_using_left_bindings_for_shared_keys", {"V", "map value"}},
        {"nonempty_map_merge_using_left_bindings_for_shared_keys", {"V", "map value"}},
        {"map_merge_using_right_bindings_for_shared_keys", {"V", "map value"}},
        {"map_merge_left_nonempty_using_right_bindings_for_shared_keys", {"V", "map value"}},
        {"map_merge_right_nonempty_using_right_bindings_for_shared_keys", {"V", "map value"}},
        {"nonempty_map_merge_using_right_bindings_for_shared_keys", {"V", "map value"}},
        {"map_from_entries_using_first_bindings", {"V", "map value"}},
        {"nonempty_map_from_entries_using_first_bindings", {"V", "map value"}},
        {"map_from_entries_using_last_bindings", {"V", "map value"}},
        {"nonempty_map_from_entries_using_last_bindings", {"V", "map value"}},
    };

std::string final_path_segment_or_malformed_path(const std::vector<std::string>& path) {
    if (path.empty()) {
        return "<malformed path>";
    }
    return path.back();
}

enum class VariantNamingRole {
    DomainAlternativeName,
    PseudoOptionalCarrierName,
};

enum class ParameterTypeAuthority {
    OrdinaryOrUnresolvedType,
    PermitType,
};

enum class TypeArgumentPreparation {
    ReadyForInstantiation,
    RejectedByDiagnostics,
};

enum class PublicNamePolicy {
    InternalName,
    ExportedName,
};

enum class GenericStructuralRole {
    StructurallyBlind,
    SemanticWrapper,
};

enum class TypeReferencePathRole {
    EmptyPath,
    GenericParameter,
    BuiltinTypeName,
    NamedTypePath,
};

enum class PublicNameReservation {
    AvailableForPublicUse,
    ReservedForPublicSurface,
};

enum class CompilerOwnedCollectionNameReservation {
    UserDeclarationName,
    ReservedCollectionSurfaceName,
};

enum class BuiltinTypeNameReservation {
    UserDeclarationName,
    ReservedBuiltinTypeName,
};

enum class BuiltinNameState {
    UserDeclaredName,
    CompilerBuiltinName,
};

enum class CompilerOwnedTypeNameState {
    UserDeclaredTypeName,
    CompilerOwnedTypeName,
};

enum class TypeDeclarationRole {
    DoesNotIntroduceType,
    IntroducesType,
};

enum class ScopeContainment {
    SeparateScopeBranch,
    AncestorOrSameScope,
};

enum class ImportPathCoverage {
    OutsideFileImports,
    CoveredByFileImport,
};

enum class CrossModuleReferenceAccess {
    ReferenceMayProceed,
    MatchingImportRequired,
};

enum class PermitTypeState {
    RuntimeValueType,
    PermitValueType,
};

enum class AssignmentCompatibility {
    TypeMismatch,
    AssignmentAllowed,
};

enum class StringLiteralTypingState {
    RequiresTextDefault,
    AcceptsStringLiteral,
};

enum class BuiltinTypeArgumentArity {
    UserDeclaredTypeName,
    NoTypeArguments,
    OneTypeArgument,
    TwoTypeArguments,
};

enum class BuiltinMapKeyAdmission {
    CanonicalKeyType,
    UnsupportedKnownType,
    UnresolvedType,
};

enum class BuiltinMapFamily {
    OtherBuiltinType,
    MapFamily,
};

enum class NumberLiteralKind {
    Integer,
    Float,
};

PublicNameReservation public_name_reservation(std::string_view name) {
    return name.size() == 1 || kReservedPublicNames.contains(name)
        ? PublicNameReservation::ReservedForPublicSurface
        : PublicNameReservation::AvailableForPublicUse;
}

CompilerOwnedCollectionNameReservation compiler_owned_collection_name_reservation(std::string_view name) {
    return kCompilerOwnedCollectionNames.contains(name)
        ? CompilerOwnedCollectionNameReservation::ReservedCollectionSurfaceName
        : CompilerOwnedCollectionNameReservation::UserDeclarationName;
}

BuiltinTypeNameReservation builtin_type_name_reservation(std::string_view name) {
    return kBuiltins.contains(name) ? BuiltinTypeNameReservation::ReservedBuiltinTypeName
                                    : BuiltinTypeNameReservation::UserDeclarationName;
}

BuiltinNameState builtin_name_state(std::string_view name) {
    return kBuiltins.contains(name) ? BuiltinNameState::CompilerBuiltinName
                                    : BuiltinNameState::UserDeclaredName;
}

CompilerOwnedTypeNameState compiler_owned_type_name_state(std::string_view name) {
    return kBuiltins.contains(name) || kCompilerOwnedCollectionCompanionRecordNames.contains(name)
        ? CompilerOwnedTypeNameState::CompilerOwnedTypeName
        : CompilerOwnedTypeNameState::UserDeclaredTypeName;
}

std::string_view compiler_owned_type_diagnostic_kind(std::string_view name) {
    return kCompilerOwnedCollectionCompanionRecordNames.contains(name)
        ? "compiler-owned collection companion type"
        : "builtin type";
}

GenericStructuralRole generic_structural_role(std::string_view name) {
    return kSemanticGenericNames.contains(name)
        ? GenericStructuralRole::SemanticWrapper
        : GenericStructuralRole::StructurallyBlind;
}

VariantNamingRole variant_naming_role(std::string_view name) {
    return kPseudoOptionalNames.contains(name)
        ? VariantNamingRole::PseudoOptionalCarrierName
        : VariantNamingRole::DomainAlternativeName;
}

BuiltinTypeArgumentArity builtin_type_argument_arity(std::string_view name) {
    if (name == "List" || name == "NonEmptyList") {
        return BuiltinTypeArgumentArity::OneTypeArgument;
    }
    if (name == "ListFirstAndRest") {
        return BuiltinTypeArgumentArity::OneTypeArgument;
    }
    if (name == "Map" || name == "NonEmptyMap") {
        return BuiltinTypeArgumentArity::TwoTypeArguments;
    }
    if (name == "MapEntry" || name == "MapFirstEntryAndRest" || name == "MapBoundValueAndRest") {
        return BuiltinTypeArgumentArity::TwoTypeArguments;
    }
    if (builtin_name_state(name) == BuiltinNameState::CompilerBuiltinName) {
        return BuiltinTypeArgumentArity::NoTypeArguments;
    }
    return BuiltinTypeArgumentArity::UserDeclaredTypeName;
}

BuiltinMapFamily builtin_map_family(std::string_view name) {
    return name == "Map" || name == "NonEmptyMap" || name == "MapEntry"
            || name == "MapFirstEntryAndRest" || name == "MapBoundValueAndRest"
        ? BuiltinMapFamily::MapFamily
        : BuiltinMapFamily::OtherBuiltinType;
}

std::size_t expected_builtin_type_arg_count(BuiltinTypeArgumentArity arity) {
    switch (arity) {
    case BuiltinTypeArgumentArity::NoTypeArguments:
    case BuiltinTypeArgumentArity::UserDeclaredTypeName:
        return 0;
    case BuiltinTypeArgumentArity::OneTypeArgument:
        return 1;
    case BuiltinTypeArgumentArity::TwoTypeArguments:
        return 2;
    }
    return 0;
}

NumberLiteralKind number_literal_kind(std::string_view lexeme) {
    return lexeme.find_first_of(".eE") == std::string_view::npos ? NumberLiteralKind::Integer
                                                                 : NumberLiteralKind::Float;
}

bool is_admitted_float_literal(std::string_view lexeme) {
    const std::string text(lexeme);
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(value);
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

enum class TypePrivacyPolicy {
    PrivateReferencesAllowed,
    PublicApiRequiresPublicTypes,
};

enum class ReasonTypePolicy {
    RejectReasonTypes,
    AllowReasonTypes,
};

enum class PermitTypePolicy {
    RejectPermitTypes,
    AllowPermitTypes,
};

ParameterTypeAuthority parameter_type_authority(const Symbol* symbol) {
    return symbol != nullptr && symbol->kind == ast::DeclKind::Permit
        ? ParameterTypeAuthority::PermitType
        : ParameterTypeAuthority::OrdinaryOrUnresolvedType;
}

PermitTypePolicy permit_type_policy_for_parameter(ast::ParameterAuthority parameter_authority,
                                                  ParameterTypeAuthority type_authority) {
    return parameter_authority == ast::ParameterAuthority::PermitBinding
            || type_authority == ParameterTypeAuthority::PermitType
        ? PermitTypePolicy::AllowPermitTypes
        : PermitTypePolicy::RejectPermitTypes;
}

PublicNamePolicy public_name_policy(ast::Visibility visibility) {
    return visibility == ast::Visibility::Public
        ? PublicNamePolicy::ExportedName
        : PublicNamePolicy::InternalName;
}

PublicNamePolicy field_name_policy(ast::Visibility owner_visibility, ast::Visibility field_visibility) {
    return owner_visibility == ast::Visibility::Public && field_visibility == ast::Visibility::Public
        ? PublicNamePolicy::ExportedName
        : PublicNamePolicy::InternalName;
}

TypeReferencePathRole type_reference_path_role(const std::vector<std::string>& path,
                                               const std::vector<std::string>& generics) {
    if (path.empty()) {
        return TypeReferencePathRole::EmptyPath;
    }
    if (path.size() == 1
        && std::find(generics.begin(), generics.end(), path.front()) != generics.end()) {
        return TypeReferencePathRole::GenericParameter;
    }
    if (compiler_owned_type_name_state(path.front()) == CompilerOwnedTypeNameState::CompilerOwnedTypeName) {
        return TypeReferencePathRole::BuiltinTypeName;
    }
    return TypeReferencePathRole::NamedTypePath;
}

struct TypeUseRules {
    TypePrivacyPolicy privacy = TypePrivacyPolicy::PrivateReferencesAllowed;
    ReasonTypePolicy reason = ReasonTypePolicy::RejectReasonTypes;
    PermitTypePolicy permit = PermitTypePolicy::RejectPermitTypes;
    std::string_view context;
};

TypePrivacyPolicy type_privacy_policy(ast::Visibility visibility) {
    return visibility == ast::Visibility::Public
        ? TypePrivacyPolicy::PublicApiRequiresPublicTypes
        : TypePrivacyPolicy::PrivateReferencesAllowed;
}

TypePrivacyPolicy field_type_privacy_policy(ast::Visibility owner_visibility, ast::Visibility field_visibility) {
    return owner_visibility == ast::Visibility::Public && field_visibility == ast::Visibility::Public
        ? TypePrivacyPolicy::PublicApiRequiresPublicTypes
        : TypePrivacyPolicy::PrivateReferencesAllowed;
}

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
        inject_collection_reason_decls();
        inject_collection_function_decls();
        validate_imports(unit.imports);
        analyze_scope(unit.decls,
                      root_,
                      PublicReachability::ReachableThroughPublicParents,
                      PackageRootDeclarationContext{});
    }

private:
    using Type = typesys::Type;
    using UseDiscipline = typesys::UseDiscipline;

    enum class ControlFlowReachability {
        ContinuesToFollowingCode,
        DivergesBeforeFollowingCode,
    };

    enum class CrossModuleReferencePolicy {
        PackageWideReferencesAllowed,
        FileLocalImportsRequired,
    };

    enum class PublicReachability {
        ReachableThroughPublicParents,
        HiddenByPrivateParent,
    };

    enum class DeclarationAdmission {
        AnalyzeDeclaration,
        SkipDeclaration,
    };

    struct PackageRootDeclarationContext {};

    struct ModuleBodyDeclarationContext {
        ast::ModuleKind module_kind = ast::ModuleKind::Domain;
    };

    struct ExprType {
        Type value;
        const ast::ReasonDecl* yielded_reason = nullptr;
        ControlFlowReachability reachability = ControlFlowReachability::ContinuesToFollowingCode;
    };

    enum class CollectionCompanionRecordFieldState {
        NotCollectionCompanionRecord,
        MalformedCollectionCompanionRecord,
        FieldFound,
        FieldNotFound,
    };

    struct CollectionCompanionRecordField {
        CollectionCompanionRecordFieldState state =
            CollectionCompanionRecordFieldState::NotCollectionCompanionRecord;
        Type type;
    };

    using BindingId = std::size_t;

    enum class BindingMoveState {
        AvailableForUse,
        MovedFrom,
    };

    enum class BindingMoveOriginEvidence {
        NoMoveRecorded,
        PreviousMoveRecorded,
    };

    struct BindingMoveOrigin {
        BindingMoveOriginEvidence evidence = BindingMoveOriginEvidence::NoMoveRecorded;
        SourceSpan span;
    };

    struct BindingState {
        BindingId id = 0;
        Type type;
        UseDiscipline discipline = UseDiscipline::Copyable;
        BindingMoveState move_state = BindingMoveState::AvailableForUse;
        BindingMoveOrigin move_origin;
    };

    struct ValueEnv {
        std::vector<std::unordered_map<std::string, BindingState>> scopes;
    };

    struct BranchState {
        ValueEnv env;
        ControlFlowReachability reachability = ControlFlowReachability::ContinuesToFollowingCode;
    };

    enum class FieldInitializationCheck {
        Accepted,
        Rejected,
    };

    enum class ProofMintingAuthorization {
        AuthorizedByFunctionContract,
        ProofNotListedInFunctionContract,
    };

    enum class MatchArmResultCoverage {
        NoArmResultAvailable,
        ArmResultAvailable,
    };

    enum class FailingMatchSuccessCoverage {
        SucceededArmMissing,
        SucceededArmObserved,
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

    enum class VariantOwnerKind {
        StateDeclaration,
        ReasonDeclaration,
    };

    enum class VariantResolutionState {
        NoMatchingVariant,
        UniqueVariant,
        AmbiguousVariant,
    };

    struct VariantResolution {
        const ast::Decl* owner_decl = nullptr;
        const ast::Variant* variant = nullptr;
        VariantResolutionState state = VariantResolutionState::NoMatchingVariant;
    };

    struct PhasePositionType {
        const ast::Decl* decl = nullptr;
        ast::Visibility visibility = ast::Visibility::Private;
        std::string qualified_name;
    };

    enum class PhasePositionResolutionState {
        PathNamesOtherType,
        PathNamesPhasePosition,
    };

    struct PhasePositionResolution {
        PhasePositionResolutionState state = PhasePositionResolutionState::PathNamesOtherType;
        PhasePositionType position;
    };

    DiagnosticSink& diagnostics_;
    const SourceFile& source_;
    Scope root_;
    std::vector<std::unique_ptr<ast::ReasonDecl>> collection_reason_decls_;
    std::vector<std::unique_ptr<ast::FunctionDecl>> collection_function_decls_;
    std::unordered_map<const ast::Decl*, std::string> qualified_names_;
    std::unordered_map<const ast::Decl*, const Scope*> decl_scopes_;
    std::unordered_map<std::string, std::vector<std::vector<std::string>>> imported_module_paths_by_file_;
    std::unordered_set<std::string> checked_generic_function_instantiations_;
    std::unordered_set<std::string> active_generic_function_instantiations_;
    CrossModuleReferencePolicy cross_module_reference_policy_ =
        CrossModuleReferencePolicy::PackageWideReferencesAllowed;
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

    void inject_collection_reason_decls() {
        for (const auto& [reason_name, variant_names] : kCompilerOwnedCollectionReasonVariants) {
            const std::string name(reason_name);
            if (root_.symbols.contains(name)) {
                continue;
            }

            auto reason = std::make_unique<ast::ReasonDecl>(ast::Visibility::Public, name);
            for (std::string_view variant_name : variant_names) {
                ast::Variant variant;
                variant.name = std::string(variant_name);
                reason->variants.push_back(std::move(variant));
            }

            const ast::ReasonDecl* reason_ptr = reason.get();
            root_.symbols.emplace(reason_ptr->name,
                                  Symbol{reason_ptr, reason_ptr->kind, reason_ptr->visibility, nullptr});
            qualified_names_[reason_ptr] = reason_ptr->name;
            decl_scopes_[reason_ptr] = &root_;
            collection_reason_decls_.push_back(std::move(reason));
        }
    }

    ast::TypeRef collection_type_ref(CollectionTypeTemplate type_template) const {
        auto named_type = [](std::string_view name) {
            ast::TypeRef type;
            type.path.push_back(std::string(name));
            return type;
        };

        auto list_arg_type = [&](std::string_view name) {
            ast::TypeRef type = named_type(name);
            type.args.push_back(named_type("T"));
            return type;
        };

        auto map_arg_type = [&](std::string_view name) {
            ast::TypeRef type = named_type(name);
            type.args.push_back(named_type("K"));
            type.args.push_back(named_type("V"));
            return type;
        };

        auto map_entry_type = [&] {
            return map_arg_type("MapEntry");
        };

        auto list_of_map_entries_type = [&](std::string_view name) {
            ast::TypeRef type = named_type(name);
            type.args.push_back(map_entry_type());
            return type;
        };

        switch (type_template) {
        case CollectionTypeTemplate::GenericT:
            return named_type("T");
        case CollectionTypeTemplate::GenericK:
            return named_type("K");
        case CollectionTypeTemplate::GenericV:
            return named_type("V");
        case CollectionTypeTemplate::Nat:
            return named_type("Nat");
        case CollectionTypeTemplate::Text:
            return named_type("Text");
        case CollectionTypeTemplate::Bytes:
            return named_type("Bytes");
        case CollectionTypeTemplate::ListT:
            return list_arg_type("List");
        case CollectionTypeTemplate::NonEmptyListT:
            return list_arg_type("NonEmptyList");
        case CollectionTypeTemplate::ListFirstAndRestT:
            return list_arg_type("ListFirstAndRest");
        case CollectionTypeTemplate::MapKV:
            return map_arg_type("Map");
        case CollectionTypeTemplate::NonEmptyMapKV:
            return map_arg_type("NonEmptyMap");
        case CollectionTypeTemplate::MapEntryKV:
            return map_entry_type();
        case CollectionTypeTemplate::MapFirstEntryAndRestKV:
            return map_arg_type("MapFirstEntryAndRest");
        case CollectionTypeTemplate::MapBoundValueAndRestKV:
            return map_arg_type("MapBoundValueAndRest");
        case CollectionTypeTemplate::ListMapEntryKV:
            return list_of_map_entries_type("List");
        case CollectionTypeTemplate::NonEmptyListMapEntryKV:
            return list_of_map_entries_type("NonEmptyList");
        }
        return named_type("T");
    }

    std::unique_ptr<ast::FunctionDecl> make_collection_function_decl(
        const CollectionFunctionSpec& spec) const {
        auto function = std::make_unique<ast::FunctionDecl>(
            ast::Visibility::Public,
            std::string(spec.name),
            ast::FunctionImplementation::EvidentBody);
        function->signature.name = function->name;
        for (std::string_view generic_name : spec.generic_params) {
            function->signature.generic_params.push_back(ast::GenericParam{std::string(generic_name), SourceSpan{}});
        }
        function->signature.return_type = collection_type_ref(spec.return_type);
        if (!spec.failure_reason.empty()) {
            function->signature.failure = ast::FunctionFailureContract::yields_reason(
                collection_type_ref_for_named_reason(spec.failure_reason));
        }
        function->signature.params.reserve(spec.params.size());
        for (const CollectionFunctionParamSpec& param_spec : spec.params) {
            function->signature.params.push_back(ast::Parameter{
                std::string(param_spec.name),
                collection_type_ref(param_spec.type),
                ast::ParameterAuthority::OrdinaryValue,
                SourceSpan{},
            });
        }
        return function;
    }

    ast::TypeRef collection_type_ref_for_named_reason(std::string_view reason_name) const {
        ast::TypeRef type;
        type.path.push_back(std::string(reason_name));
        return type;
    }

    void inject_collection_function_decls() {
        for (const CollectionFunctionSpec& spec : kCompilerOwnedCollectionFunctionSpecs) {
            const std::string name(spec.name);
            if (root_.symbols.contains(name)) {
                continue;
            }

            auto function = make_collection_function_decl(spec);
            const ast::FunctionDecl* function_ptr = function.get();
            root_.symbols.emplace(function_ptr->name,
                                  Symbol{function_ptr, function_ptr->kind, function_ptr->visibility, nullptr});
            qualified_names_[function_ptr] = function_ptr->name;
            decl_scopes_[function_ptr] = &root_;
            collection_function_decls_.push_back(std::move(function));
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
            if (std::find(local_generics.begin(), local_generics.end(), name) != local_generics.end()
                || compiler_owned_type_name_state(name) == CompilerOwnedTypeNameState::CompilerOwnedTypeName) {
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
                                      VariantOwnerKind owner_kind) const {
        VariantResolution result;
        if (path.empty()) {
            return result;
        }

        auto owner_kind_matches = [owner_kind](ast::DeclKind candidate) {
            switch (owner_kind) {
            case VariantOwnerKind::StateDeclaration:
                return candidate == ast::DeclKind::State;
            case VariantOwnerKind::ReasonDeclaration:
                return candidate == ast::DeclKind::Reason;
            }
            return false;
        };

        auto try_owner = [&](const ast::Decl* owner, std::string_view variant_name) {
            if (owner == nullptr) {
                return;
            }
            if (!owner_kind_matches(owner->kind)) {
                return;
            }

            const auto* variants = owner->kind == ast::DeclKind::State
                ? &static_cast<const ast::StateDecl*>(owner)->variants
                : &static_cast<const ast::ReasonDecl*>(owner)->variants;
            for (const ast::Variant& variant : *variants) {
                if (variant.name == variant_name) {
                    if (result.variant != nullptr && result.owner_decl != owner) {
                        result.state = VariantResolutionState::AmbiguousVariant;
                        return;
                    }
                    result.owner_decl = owner;
                    result.variant = &variant;
                    result.state = VariantResolutionState::UniqueVariant;
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
                if (owner_kind_matches(symbol.kind)) {
                    try_owner(symbol.decl, path.front());
                    if (result.state == VariantResolutionState::AmbiguousVariant) {
                        return result;
                    }
                }
            }
        }
        return result;
    }

    static TypeDeclarationRole declaration_type_role(ast::DeclKind kind) {
        switch (kind) {
        case ast::DeclKind::Record:
        case ast::DeclKind::State:
        case ast::DeclKind::Reason:
        case ast::DeclKind::Proof:
        case ast::DeclKind::Permit:
        case ast::DeclKind::Phase:
            return TypeDeclarationRole::IntroducesType;
        default:
            return TypeDeclarationRole::DoesNotIntroduceType;
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

    CollectionCompanionRecordField collection_companion_record_field_type(
        const Type& owner,
        std::string_view field_name) const {
        if (owner.flavor != typesys::TypeFlavor::Builtin
            || !kCompilerOwnedCollectionCompanionRecordNames.contains(owner.name)) {
            return {};
        }

        auto malformed = [] {
            return CollectionCompanionRecordField{
                CollectionCompanionRecordFieldState::MalformedCollectionCompanionRecord,
                typesys::error_type(),
            };
        };
        auto found = [](Type type) {
            return CollectionCompanionRecordField{
                CollectionCompanionRecordFieldState::FieldFound,
                std::move(type),
            };
        };
        auto missing = [] {
            return CollectionCompanionRecordField{
                CollectionCompanionRecordFieldState::FieldNotFound,
                typesys::error_type(),
            };
        };
        auto list_type = [this](Type element) {
            std::vector<Type> args;
            args.push_back(std::move(element));
            return builtin_type("List", std::move(args));
        };
        auto map_type = [this](Type key, Type value) {
            std::vector<Type> args;
            args.push_back(std::move(key));
            args.push_back(std::move(value));
            return builtin_type("Map", std::move(args));
        };
        auto map_entry_type = [this](Type key, Type value) {
            std::vector<Type> args;
            args.push_back(std::move(key));
            args.push_back(std::move(value));
            return builtin_type("MapEntry", std::move(args));
        };

        if (owner.name == "ListFirstAndRest") {
            if (owner.args.size() != 1) {
                return malformed();
            }
            if (field_name == "first") {
                return found(owner.args.front());
            }
            if (field_name == "rest") {
                return found(list_type(owner.args.front()));
            }
            return missing();
        }
        if (owner.name == "MapEntry") {
            if (owner.args.size() != 2) {
                return malformed();
            }
            if (field_name == "key") {
                return found(owner.args[0]);
            }
            if (field_name == "value") {
                return found(owner.args[1]);
            }
            return missing();
        }
        if (owner.name == "MapFirstEntryAndRest") {
            if (owner.args.size() != 2) {
                return malformed();
            }
            if (field_name == "first") {
                return found(map_entry_type(owner.args[0], owner.args[1]));
            }
            if (field_name == "rest") {
                return found(map_type(owner.args[0], owner.args[1]));
            }
            return missing();
        }
        if (owner.name == "MapBoundValueAndRest") {
            if (owner.args.size() != 2) {
                return malformed();
            }
            if (field_name == "value") {
                return found(owner.args[1]);
            }
            if (field_name == "rest") {
                return found(map_type(owner.args[0], owner.args[1]));
            }
            return missing();
        }
        return {};
    }

    static ScopeContainment scope_containment(const Scope* ancestor, const Scope* descendant) {
        for (const Scope* p = descendant; p != nullptr; p = p->parent) {
            if (p == ancestor) {
                return ScopeContainment::AncestorOrSameScope;
            }
        }
        return ScopeContainment::SeparateScopeBranch;
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
            if (scope_containment(symbol.child_scope.get(), &scope)
                == ScopeContainment::AncestorOrSameScope) {
                return &symbol;
            }
        }
        return nullptr;
    }

    ImportPathCoverage path_import_coverage(const std::vector<std::string>& path, SourceSpan span) const {
        const std::string source_path(source_.path_at(span.begin));
        const auto imports_it = imported_module_paths_by_file_.find(source_path);
        if (imports_it == imported_module_paths_by_file_.end()) {
            return ImportPathCoverage::OutsideFileImports;
        }

        for (const std::vector<std::string>& imported_path : imports_it->second) {
            if (path.size() < imported_path.size()) {
                continue;
            }
            if (std::equal(imported_path.begin(), imported_path.end(), path.begin())) {
                return ImportPathCoverage::CoveredByFileImport;
            }
        }
        return ImportPathCoverage::OutsideFileImports;
    }

    CrossModuleReferenceAccess check_import_access(const Scope& scope,
                                                   const std::vector<std::string>& path,
                                                   SourceSpan span) {
        if (cross_module_reference_policy_ == CrossModuleReferencePolicy::PackageWideReferencesAllowed
            || path.size() < 2) {
            return CrossModuleReferenceAccess::ReferenceMayProceed;
        }

        const auto root_it = root_.symbols.find(path.front());
        if (root_it == root_.symbols.end() || root_it->second.kind != ast::DeclKind::Module) {
            return CrossModuleReferenceAccess::ReferenceMayProceed;
        }

        if (const Symbol* current_top_level = containing_top_level_module(scope);
            current_top_level != nullptr && current_top_level->decl != nullptr
            && current_top_level->decl->name == path.front()) {
            return CrossModuleReferenceAccess::ReferenceMayProceed;
        }

        if (path_import_coverage(path, span) == ImportPathCoverage::CoveredByFileImport) {
            return CrossModuleReferenceAccess::ReferenceMayProceed;
        }

        diagnostics_.error(span,
                           "cross-module reference '" + format_path(path) + "' requires a matching import");
        return CrossModuleReferenceAccess::MatchingImportRequired;
    }

    PhasePositionResolution resolve_phase_position_type(const Scope& scope,
                                                        const std::vector<std::string>& generics,
                                                        const std::vector<std::string>& path) const {
        if (path.size() < 2) {
            return {};
        }

        std::vector<std::string> family_path(path.begin(), path.end() - 1);
        const std::string& position_name = path.back();
        const Symbol* symbol = resolve_symbol(scope, generics, family_path);
        if (symbol == nullptr || symbol->kind != ast::DeclKind::Phase) {
            return {};
        }

        const auto& phase_decl = static_cast<const ast::PhaseDecl&>(*symbol->decl);
        if (std::find(phase_decl.positions.begin(), phase_decl.positions.end(), position_name)
            == phase_decl.positions.end()) {
            return {};
        }

        return PhasePositionResolution{
            PhasePositionResolutionState::PathNamesPhasePosition,
            PhasePositionType{
                symbol->decl,
                symbol->visibility,
                qualified_names_.at(symbol->decl) + "::" + position_name,
            },
        };
    }

    typesys::TypeErrorState type_error_state(const Type& type) const {
        return typesys::type_error_state(type);
    }

    typesys::NeverTypeState never_type_state(const Type& type) const {
        return typesys::never_type_state(type);
    }

    PermitTypeState permit_type_state(const Type& type) const {
        return typesys::discipline_materialization(discipline(type))
                == typesys::DisciplineMaterialization::CompileTimeOnly
            ? PermitTypeState::PermitValueType
            : PermitTypeState::RuntimeValueType;
    }

    UseDiscipline discipline(const Type& type) const {
        return discipline_classifier_.classify(type);
    }

    std::string type_name(const Type& type) const {
        return typesys::type_name(type);
    }

    typesys::TypeEquivalence type_equivalence(const Type& lhs, const Type& rhs) const {
        return typesys::type_equivalence(lhs, rhs);
    }

    static PublicReachability declaration_public_reachability(PublicReachability enclosing,
                                                              ast::Visibility declaration_visibility) {
        switch (enclosing) {
        case PublicReachability::ReachableThroughPublicParents:
            return declaration_visibility == ast::Visibility::Public
                ? PublicReachability::ReachableThroughPublicParents
                : PublicReachability::HiddenByPrivateParent;
        case PublicReachability::HiddenByPrivateParent:
            return PublicReachability::HiddenByPrivateParent;
        }
        return PublicReachability::HiddenByPrivateParent;
    }

    static void mark_binding_moved(BindingState& binding, SourceSpan origin_span) {
        binding.move_state = BindingMoveState::MovedFrom;
        binding.move_origin = BindingMoveOrigin{
            BindingMoveOriginEvidence::PreviousMoveRecorded,
            origin_span,
        };
    }

    static void copy_binding_move_state(BindingState& target, const BindingState& source) {
        target.move_state = source.move_state;
        target.move_origin = source.move_origin;
    }

    ExprType make_expr(Type value, const ast::ReasonDecl* yielded_reason = nullptr) const {
        const ControlFlowReachability reachability =
            never_type_state(value) == typesys::NeverTypeState::DivergesBeforeFollowingCode
            ? ControlFlowReachability::DivergesBeforeFollowingCode
            : ControlFlowReachability::ContinuesToFollowingCode;
        return ExprType{std::move(value), yielded_reason, reachability};
    }

    AssignmentCompatibility assignment_compatibility(const Type& target, const Type& actual) const {
        if (type_error_state(target) == typesys::TypeErrorState::SuppressesFollowupDiagnostics
            || type_error_state(actual) == typesys::TypeErrorState::SuppressesFollowupDiagnostics
            || never_type_state(actual) == typesys::NeverTypeState::DivergesBeforeFollowingCode) {
            return AssignmentCompatibility::AssignmentAllowed;
        }
        return type_equivalence(target, actual) == typesys::TypeEquivalence::Equivalent
            ? AssignmentCompatibility::AssignmentAllowed
            : AssignmentCompatibility::TypeMismatch;
    }

    StringLiteralTypingState string_literal_typing_state(const Type& type) const {
        if (type.flavor == typesys::TypeFlavor::Builtin
            && type.args.empty()
            && (type.name == "Text" || type.name == "Bytes" || type.name == "CString")) {
            return StringLiteralTypingState::AcceptsStringLiteral;
        }
        return StringLiteralTypingState::RequiresTextDefault;
    }

    const ast::PathExpr* path_expr(const ast::Expr& expr) const {
        return expr.kind == ast::ExprKind::Path ? &static_cast<const ast::PathExpr&>(expr) : nullptr;
    }

    const ast::CallExpr* call_expr(const ast::Expr& expr) const {
        return expr.kind == ast::ExprKind::Call ? &static_cast<const ast::CallExpr&>(expr) : nullptr;
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
            if (compiler_owned_type_name_state(name) == CompilerOwnedTypeNameState::CompilerOwnedTypeName) {
                return builtin_type(name, std::move(args));
            }
        }

        if (const PhasePositionResolution phase_position = resolve_phase_position_type(scope, generics, type_ref.path);
            phase_position.state == PhasePositionResolutionState::PathNamesPhasePosition) {
            return typesys::named_type(phase_position.position.qualified_name,
                                       phase_position.position.decl,
                                       std::move(args));
        }

        if (const Symbol* symbol = resolve_symbol(scope, generics, type_ref.path); symbol != nullptr) {
            if (symbol->kind == ast::DeclKind::Phase) {
                return error_type();
            }
            if (declaration_type_role(symbol->kind) == TypeDeclarationRole::IntroducesType) {
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
        const auto qualified_it = qualified_names_.find(&function);
        out << "F:" << (qualified_it != qualified_names_.end() ? qualified_it->second : function.name) << '<';
        for (std::size_t index = 0; index < substitutions.size(); ++index) {
            if (index > 0) {
                out << ',';
            }
            out << substitutions[index].first << '=' << type_name(substitutions[index].second);
        }
        out << '>';
        return out.str();
    }

    TypeArgumentPreparation prepare_call_type_substitutions(
        const ast::FunctionDecl& function,
        const ast::CallExpr& call,
        const FunctionContext& context,
        std::vector<std::pair<std::string, Type>>& substitutions) {
        const std::size_t expected = function.signature.generic_params.size();
        const std::size_t actual = call.type_args.size();
        if (expected == 0) {
            if (actual != 0) {
                diagnostics_.error(call.span,
                                   "function '" + function.name + "' is not generic but was called with type arguments");
                return TypeArgumentPreparation::RejectedByDiagnostics;
            }
            return TypeArgumentPreparation::ReadyForInstantiation;
        }
        if (actual == 0) {
            diagnostics_.error(call.span,
                               "generic function '" + function.name + "' requires explicit type arguments");
            return TypeArgumentPreparation::RejectedByDiagnostics;
        }
        if (expected != actual) {
            diagnostics_.error(call.span,
                               "generic function '" + function.name + "' expects " + std::to_string(expected)
                                   + " type argument(s), got " + std::to_string(actual));
            return TypeArgumentPreparation::RejectedByDiagnostics;
        }

        substitutions.reserve(expected);
        for (std::size_t index = 0; index < expected; ++index) {
            const ast::TypeRef& type_arg = call.type_args[index];
            check_type_ref_usage(context.scope,
                                 context.generics,
                                 type_arg,
                                 TypeUseRules{
                                     TypePrivacyPolicy::PrivateReferencesAllowed,
                                     ReasonTypePolicy::RejectReasonTypes,
                                     PermitTypePolicy::RejectPermitTypes,
                                     "generic function type argument"});
            substitutions.emplace_back(function.signature.generic_params[index].name,
                                       resolve_type(context.scope, context.generics, context.substitutions, type_arg));
        }
        return TypeArgumentPreparation::ReadyForInstantiation;
    }

    const Type* substituted_type_argument(const std::vector<std::pair<std::string, Type>>& substitutions,
                                          std::string_view generic_name) const {
        for (const auto& [name, type] : substitutions) {
            if (name == generic_name) {
                return &type;
            }
        }
        return nullptr;
    }

    SourceSpan call_type_argument_span(const ast::FunctionDecl& function,
                                       const ast::CallExpr& call,
                                       std::string_view generic_name) const {
        for (std::size_t index = 0; index < function.signature.generic_params.size(); ++index) {
            if (function.signature.generic_params[index].name == generic_name
                && index < call.type_args.size()) {
                return call.type_args[index].span;
            }
        }
        return call.span;
    }

    void check_collection_operation_availability(
        const ast::FunctionDecl& function,
        const ast::CallExpr& call,
        const std::vector<std::pair<std::string, Type>>& substitutions) {
        const auto requirement_it =
            kCompilerOwnedCollectionCopyabilityRequirements.find(std::string_view(function.name));
        if (requirement_it == kCompilerOwnedCollectionCopyabilityRequirements.end()) {
            return;
        }

        const CollectionCopyabilityRequirement& requirement = requirement_it->second;
        const Type* const payload_type = substituted_type_argument(substitutions, requirement.generic_param);
        if (payload_type == nullptr
            || type_error_state(*payload_type) == typesys::TypeErrorState::SuppressesFollowupDiagnostics) {
            return;
        }
        if (typesys::discipline_movement(discipline(*payload_type)) == typesys::DisciplineMovement::Copyable) {
            return;
        }

        diagnostics_.error(
            call_type_argument_span(function, call, requirement.generic_param),
            "collection operation '" + function.name + "' requires copyable "
                + std::string(requirement.payload_description) + " type '"
                + std::string(requirement.generic_param) + "', got affine-bearing '"
                + type_name(*payload_type) + "'");
    }

    TypeArgumentPreparation prepare_record_constructor_type_args(const ast::RecordDecl& record,
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
                return TypeArgumentPreparation::RejectedByDiagnostics;
            }
            return TypeArgumentPreparation::ReadyForInstantiation;
        }
        if (actual == 0) {
            diagnostics_.error(construct.span,
                               "generic record '" + record.name + "' requires explicit type arguments");
            return TypeArgumentPreparation::RejectedByDiagnostics;
        }
        if (expected != actual) {
            diagnostics_.error(construct.span,
                               "generic record '" + record.name + "' expects " + std::to_string(expected)
                                   + " type argument(s), got " + std::to_string(actual));
            return TypeArgumentPreparation::RejectedByDiagnostics;
        }

        type_args.reserve(expected);
        for (const ast::TypeRef& type_arg : construct.type_args) {
            check_type_ref_usage(context.scope,
                                 context.generics,
                                 type_arg,
                                 TypeUseRules{
                                     TypePrivacyPolicy::PrivateReferencesAllowed,
                                     ReasonTypePolicy::RejectReasonTypes,
                                     PermitTypePolicy::RejectPermitTypes,
                                     "generic record constructor type argument"});
            type_args.push_back(resolve_type(context.scope, context.generics, context.substitutions, type_arg));
        }
        return TypeArgumentPreparation::ReadyForInstantiation;
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
                if (binding.move_state == BindingMoveState::MovedFrom) {
                    continue;
                }
                const BindingState* child_binding = lookup_binding_by_id(child, binding.id);
                if (child_binding != nullptr && child_binding->move_state == BindingMoveState::MovedFrom) {
                    copy_binding_move_state(binding, *child_binding);
                }
            }
        }
    }

    void merge_branch_envs(ValueEnv& target, const std::vector<BranchState>& branches) const {
        for (auto& scope : target.scopes) {
            for (auto& [_, binding] : scope) {
                if (binding.move_state == BindingMoveState::MovedFrom) {
                    continue;
                }
                for (const BranchState& branch : branches) {
                    if (branch.reachability != ControlFlowReachability::ContinuesToFollowingCode) {
                        continue;
                    }
                    const BindingState* branch_binding = lookup_binding_by_id(branch.env, binding.id);
                    if (branch_binding != nullptr && branch_binding->move_state == BindingMoveState::MovedFrom) {
                        copy_binding_move_state(binding, *branch_binding);
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
        if (binding->move_state == BindingMoveState::MovedFrom) {
            diagnostics_.error(span, "affine value '" + name + "' was already moved");
            if (binding->move_origin.evidence == BindingMoveOriginEvidence::PreviousMoveRecorded) {
                diagnostics_.note(binding->move_origin.span, "'" + name + "' was previously moved here");
            }
            return error_type();
        }
        if (typesys::discipline_movement(binding->discipline) == typesys::DisciplineMovement::Affine) {
            mark_binding_moved(*binding, span);
        }
        return binding->type;
    }

    Type read_binding(ValueEnv& env, const std::string& name, SourceSpan span) {
        const BindingState* binding = lookup_binding(env, name);
        if (binding == nullptr) {
            return error_type();
        }
        if (binding->move_state == BindingMoveState::MovedFrom) {
            diagnostics_.error(span, "affine value '" + name + "' was already moved");
            if (binding->move_origin.evidence == BindingMoveOriginEvidence::PreviousMoveRecorded) {
                diagnostics_.note(binding->move_origin.span, "'" + name + "' was previously moved here");
            }
            return error_type();
        }
        return binding->type;
    }

    void check_public_name(const std::string& name, SourceSpan span) {
        if (public_name_reservation(name) == PublicNameReservation::ReservedForPublicSurface) {
            diagnostics_.error(span, "public name '" + name + "' is reserved or too generic");
        }
    }

    void check_compiler_owned_collection_name(const std::string& name, SourceSpan span) {
        if (compiler_owned_collection_name_reservation(name)
            == CompilerOwnedCollectionNameReservation::ReservedCollectionSurfaceName) {
            diagnostics_.error(span, "compiler-owned collection name '" + name + "' is reserved");
        }
    }

    void check_builtin_type_declaration_name(const std::string& name, SourceSpan span) {
        if (builtin_type_name_reservation(name) == BuiltinTypeNameReservation::ReservedBuiltinTypeName) {
            diagnostics_.error(span, "builtin type name '" + name + "' is reserved");
        }
    }

    void check_name_under_public_policy(const std::string& name, SourceSpan span, PublicNamePolicy policy) {
        switch (policy) {
        case PublicNamePolicy::ExportedName:
            check_public_name(name, span);
            break;
        case PublicNamePolicy::InternalName:
            break;
        }
    }

    void check_generic_structural_name(std::string_view context, const std::string& name, SourceSpan span) {
        if (generic_structural_role(name) == GenericStructuralRole::SemanticWrapper) {
            diagnostics_.error(span,
                               std::string(context) + " '" + name
                                   + "' is not structurally blind; lifecycle, authority, proof, or domain facts "
                                     "must use first-class language constructs");
        }
    }

    void check_generic_parameters(const std::vector<ast::GenericParam>& generics, std::string_view context) {
        std::unordered_set<std::string> seen;
        for (const ast::GenericParam& generic : generics) {
            check_generic_structural_name("generic parameter", generic.name, generic.span);
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
        cross_module_reference_policy_ = CrossModuleReferencePolicy::PackageWideReferencesAllowed;
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
        cross_module_reference_policy_ = imported_module_paths_by_file_.empty()
            ? CrossModuleReferencePolicy::PackageWideReferencesAllowed
            : CrossModuleReferencePolicy::FileLocalImportsRequired;
    }

    BuiltinMapKeyAdmission map_key_admission(const Scope& scope,
                                             const std::vector<std::string>& generics,
                                             const ast::TypeRef& key_type) const {
        if (key_type.path.size() == 1 && key_type.args.empty()
            && kCanonicalMapKeyBuiltins.contains(key_type.path.front())) {
            return BuiltinMapKeyAdmission::CanonicalKeyType;
        }

        const TypeReferencePathRole path_role = type_reference_path_role(key_type.path, generics);
        switch (path_role) {
        case TypeReferencePathRole::EmptyPath:
            return BuiltinMapKeyAdmission::UnresolvedType;
        case TypeReferencePathRole::GenericParameter:
        case TypeReferencePathRole::BuiltinTypeName:
            return BuiltinMapKeyAdmission::UnsupportedKnownType;
        case TypeReferencePathRole::NamedTypePath:
            break;
        }

        if (resolve_phase_position_type(scope, generics, key_type.path).state
            == PhasePositionResolutionState::PathNamesPhasePosition) {
            return BuiltinMapKeyAdmission::UnsupportedKnownType;
        }

        if (resolve_symbol(scope, generics, key_type.path) != nullptr) {
            return BuiltinMapKeyAdmission::UnsupportedKnownType;
        }

        return BuiltinMapKeyAdmission::UnresolvedType;
    }

    void check_builtin_map_key_type(const Scope& scope,
                                    const std::vector<std::string>& generics,
                                    const ast::TypeRef& map_type) {
        if (map_type.path.size() != 1
            || builtin_map_family(map_type.path.front()) != BuiltinMapFamily::MapFamily
            || map_type.args.size() != 2) {
            return;
        }

        const ast::TypeRef& key_type = map_type.args.front();
        if (map_key_admission(scope, generics, key_type)
            == BuiltinMapKeyAdmission::UnsupportedKnownType) {
            diagnostics_.error(key_type.span,
                               "map key type '" + ast::format_type(key_type)
                                   + "' must be one of Int, Nat, Float, Byte, Char, Text, or Bytes");
        }
    }

    void check_fails_clause_reason_type(const Scope& scope,
                                        const std::vector<std::string>& generics,
                                        const ast::TypeRef& reason_type) {
        const TypeReferencePathRole path_role = type_reference_path_role(reason_type.path, generics);
        if (path_role == TypeReferencePathRole::GenericParameter) {
            diagnostics_.error(reason_type.span,
                               "'fails' must reference a reason type, not generic parameter '"
                                   + ast::format_type(reason_type) + "'");
            return;
        }

        if (path_role == TypeReferencePathRole::BuiltinTypeName && reason_type.path.size() == 1) {
            diagnostics_.error(reason_type.span,
                               "'fails' must reference a reason type, not "
                                   + std::string(compiler_owned_type_diagnostic_kind(reason_type.path.front())) + " '"
                                   + ast::format_type(reason_type) + "'");
            return;
        }

        if (resolve_phase_position_type(scope, generics, reason_type.path).state
            == PhasePositionResolutionState::PathNamesPhasePosition) {
            diagnostics_.error(reason_type.span,
                               "'fails' must reference a reason type, not concrete phase type '"
                                   + ast::format_type(reason_type) + "'");
            return;
        }

        if (const Symbol* symbol = resolve_symbol(scope, generics, reason_type.path);
            symbol != nullptr && symbol->kind != ast::DeclKind::Reason) {
            diagnostics_.error(reason_type.span,
                               "'fails' must reference a reason type, not '"
                                   + std::string(ast::decl_kind_name(symbol->kind)) + "'");
        }
    }

    void check_grants_clause_permit_type(const Scope& scope,
                                         const std::vector<std::string>& generics,
                                         const ast::TypeRef& permit_type) {
        const TypeReferencePathRole path_role = type_reference_path_role(permit_type.path, generics);
        if (path_role == TypeReferencePathRole::GenericParameter) {
            diagnostics_.error(permit_type.span,
                               "'grants' must reference a permit type, not generic parameter '"
                                   + ast::format_type(permit_type) + "'");
            return;
        }

        if (path_role == TypeReferencePathRole::BuiltinTypeName && permit_type.path.size() == 1) {
            diagnostics_.error(permit_type.span,
                               "'grants' must reference a permit type, not "
                                   + std::string(compiler_owned_type_diagnostic_kind(permit_type.path.front())) + " '"
                                   + ast::format_type(permit_type) + "'");
            return;
        }

        if (resolve_phase_position_type(scope, generics, permit_type.path).state
            == PhasePositionResolutionState::PathNamesPhasePosition) {
            diagnostics_.error(permit_type.span,
                               "'grants' must reference a permit type, not concrete phase type '"
                                   + ast::format_type(permit_type) + "'");
            return;
        }

        if (const Symbol* symbol = resolve_symbol(scope, generics, permit_type.path); symbol != nullptr) {
            if (symbol->kind != ast::DeclKind::Permit) {
                diagnostics_.error(permit_type.span,
                                   "'grants' must reference a permit type, not '"
                                       + std::string(ast::decl_kind_name(symbol->kind)) + "'");
            } else if (declaring_scope(symbol->decl) != &scope) {
                diagnostics_.error(permit_type.span,
                                   "functions may only declare 'grants' for permits from the same module");
            }
        }
    }

    void check_proves_clause_proof_type(const Scope& scope,
                                        const std::vector<std::string>& generics,
                                        const ast::TypeRef& proof_type,
                                        std::unordered_set<const ast::Decl*>& seen_proves) {
        const TypeReferencePathRole path_role = type_reference_path_role(proof_type.path, generics);
        if (path_role == TypeReferencePathRole::GenericParameter) {
            diagnostics_.error(proof_type.span,
                               "'proves' must reference a proof type, not generic parameter '"
                                   + ast::format_type(proof_type) + "'");
            return;
        }

        if (path_role == TypeReferencePathRole::BuiltinTypeName && proof_type.path.size() == 1) {
            diagnostics_.error(proof_type.span,
                               "'proves' must reference a proof type, not "
                                   + std::string(compiler_owned_type_diagnostic_kind(proof_type.path.front())) + " '"
                                   + ast::format_type(proof_type) + "'");
            return;
        }

        if (resolve_phase_position_type(scope, generics, proof_type.path).state
            == PhasePositionResolutionState::PathNamesPhasePosition) {
            diagnostics_.error(proof_type.span,
                               "'proves' must reference a proof type, not concrete phase type '"
                                   + ast::format_type(proof_type) + "'");
            return;
        }

        if (const Symbol* symbol = resolve_symbol(scope, generics, proof_type.path); symbol != nullptr) {
            if (symbol->kind != ast::DeclKind::Proof) {
                diagnostics_.error(proof_type.span,
                                   "'proves' must reference a proof type, not '"
                                       + std::string(ast::decl_kind_name(symbol->kind)) + "'");
            } else if (declaring_scope(symbol->decl) != &scope) {
                diagnostics_.error(proof_type.span,
                                   "functions may only declare 'proves' for proofs from the same module");
            } else if (!seen_proves.insert(symbol->decl).second) {
                diagnostics_.error(proof_type.span,
                                   "function signature repeats `proves` for proof type '"
                                       + ast::format_type(proof_type) + "'");
            }
        }
    }

    void check_type_ref_usage(const Scope& scope,
                              const std::vector<std::string>& generics,
                              const ast::TypeRef& type,
                              const TypeUseRules& rules) {
        const TypeReferencePathRole path_role = type_reference_path_role(type.path, generics);

        if (path_role == TypeReferencePathRole::BuiltinTypeName && type.path.size() != 1) {
            diagnostics_.error(type.span,
                              std::string(compiler_owned_type_diagnostic_kind(type.path.front())) + " '"
                                  + type.path.front() + "' may not be used as a qualified type");
        }

        if (path_role == TypeReferencePathRole::GenericParameter && !type.args.empty()) {
            diagnostics_.error(type.span,
                              "generic parameter '" + type.path.front() + "' may not have type arguments");
        } else if (type.path.size() == 1) {
            const BuiltinTypeArgumentArity builtin_arity = builtin_type_argument_arity(type.path.front());
            if (builtin_arity != BuiltinTypeArgumentArity::UserDeclaredTypeName) {
                const std::size_t expected_builtin_args = expected_builtin_type_arg_count(builtin_arity);
                const std::size_t actual = type.args.size();
                const std::string type_kind(compiler_owned_type_diagnostic_kind(type.path.front()));
                if (expected_builtin_args == 0 && actual != 0) {
                    diagnostics_.error(type.span,
                                      type_kind + " '" + type.path.front() + "' may not have type arguments");
                } else if (expected_builtin_args != 0 && actual == 0) {
                    diagnostics_.error(type.span,
                                      type_kind + " '" + type.path.front() + "' requires "
                                          + std::to_string(expected_builtin_args) + " type argument(s)");
                } else if (expected_builtin_args != actual) {
                    diagnostics_.error(type.span,
                                      type_kind + " '" + type.path.front() + "' expects "
                                          + std::to_string(expected_builtin_args) + " type argument(s), got "
                                          + std::to_string(actual));
                }
                check_builtin_map_key_type(scope, generics, type);
            }
        }

        if (path_role == TypeReferencePathRole::NamedTypePath) {
            const PhasePositionResolution phase_position = resolve_phase_position_type(scope, generics, type.path);
            if (phase_position.state == PhasePositionResolutionState::PathNamesPhasePosition) {
                check_import_access(scope, type.path, type.span);
                if (rules.privacy == TypePrivacyPolicy::PublicApiRequiresPublicTypes
                    && phase_position.position.visibility != ast::Visibility::Public) {
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
                check_import_access(scope, type.path, type.span);
                if (declaration_type_role(symbol->kind) == TypeDeclarationRole::DoesNotIntroduceType) {
                    diagnostics_.error(type.span,
                                      "expected a type in " + std::string(rules.context) + ", found '"
                                          + std::string(ast::decl_kind_name(symbol->kind)) + "'");
                } else {
                    if (rules.privacy == TypePrivacyPolicy::PublicApiRequiresPublicTypes
                        && symbol->visibility != ast::Visibility::Public) {
                        diagnostics_.error(type.span,
                                          "public API cannot reference private type '" + ast::format_type(type) + "'");
                    }
                    if (rules.reason == ReasonTypePolicy::RejectReasonTypes
                        && symbol->kind == ast::DeclKind::Reason) {
                        diagnostics_.error(type.span,
                                          "reason type '" + ast::format_type(type) + "' may not appear in "
                                              + std::string(rules.context));
                    }
                    if (rules.permit == PermitTypePolicy::RejectPermitTypes
                        && symbol->kind == ast::DeclKind::Permit) {
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
                                  TypePrivacyPolicy type_privacy,
                                  ast::FunctionImplementation implementation,
                                  const std::vector<std::string>& outer_generics) {
        std::vector<std::string> generics = outer_generics;
        check_generic_parameters(signature.generic_params, "function signature");
        for (const ast::GenericParam& generic : signature.generic_params) {
            generics.push_back(generic.name);
        }

        std::unordered_set<std::string> param_names;
        for (const ast::Parameter& param : signature.params) {
            if (!param_names.insert(param.name).second) {
                diagnostics_.error(param.span, "duplicate parameter '" + param.name + "'");
            }
            const Symbol* param_symbol = resolve_symbol(scope, generics, param.type.path);
            const ParameterTypeAuthority parameter_type = parameter_type_authority(param_symbol);
            check_type_ref_usage(scope,
                                 generics,
                                 param.type,
                                 TypeUseRules{
                                     type_privacy,
                                     ReasonTypePolicy::RejectReasonTypes,
                                     permit_type_policy_for_parameter(param.authority, parameter_type),
                                     "function parameter"});
            if (param.authority == ast::ParameterAuthority::PermitBinding) {
                if (parameter_type != ParameterTypeAuthority::PermitType) {
                    diagnostics_.error(param.type.span,
                                      "permit parameter '" + param.name + "' must reference a permit type");
                }
            } else if (parameter_type == ParameterTypeAuthority::PermitType) {
                diagnostics_.error(param.span,
                                  "permit parameter '" + param.name + "' must be written as 'as "
                                      + param.name + ": " + ast::format_type(param.type) + "'");
            }
        }

        check_type_ref_usage(scope,
                             generics,
                             signature.return_type,
                             TypeUseRules{
                                 type_privacy,
                                 ReasonTypePolicy::RejectReasonTypes,
                                 PermitTypePolicy::RejectPermitTypes,
                                 "function return type"});

        if (signature.failure.behavior() == ast::FunctionFailureBehavior::YieldsReason) {
            check_type_ref_usage(scope,
                                 generics,
                                 signature.failure.reason_type(),
                                 TypeUseRules{
                                     type_privacy,
                                     ReasonTypePolicy::AllowReasonTypes,
                                     PermitTypePolicy::RejectPermitTypes,
                                     "fails clause"});
            check_fails_clause_reason_type(scope, generics, signature.failure.reason_type());
            if (implementation == ast::FunctionImplementation::ForeignImport) {
                diagnostics_.error(signature.failure.reason_type().span, "foreign functions may not use 'fails'");
            }
        }

        if (signature.authority.effect() == ast::FunctionAuthorityEffect::GrantsScopedPermit) {
            check_type_ref_usage(scope,
                                 generics,
                                 signature.authority.permit_type(),
                                 TypeUseRules{
                                     type_privacy,
                                     ReasonTypePolicy::RejectReasonTypes,
                                     PermitTypePolicy::AllowPermitTypes,
                                     "grant permit"});
            check_grants_clause_permit_type(scope, generics, signature.authority.permit_type());
            const Type return_type = resolve_type(scope, generics, signature.return_type);
            if (type_equivalence(return_type, builtin_type("Unit")) == typesys::TypeEquivalence::Different) {
                diagnostics_.error(signature.return_type.span, "granting functions must return 'Unit'");
            }
            if (implementation == ast::FunctionImplementation::ForeignImport) {
                diagnostics_.error(signature.authority.permit_type().span, "foreign functions may not use 'grants'");
            }
        }

        std::unordered_set<const ast::Decl*> seen_proves;
        for (const ast::TypeRef& proves_type : signature.proves_types) {
            check_type_ref_usage(scope,
                                 generics,
                                 proves_type,
                                 TypeUseRules{
                                     type_privacy,
                                     ReasonTypePolicy::RejectReasonTypes,
                                     PermitTypePolicy::RejectPermitTypes,
                                     "proof authorization"});
            check_proves_clause_proof_type(scope, generics, proves_type, seen_proves);
            if (implementation == ast::FunctionImplementation::ForeignImport) {
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
            if (decl.kind == ast::DeclKind::ForeignFunction) {
                diagnostics_.error(decl.span, "foreign functions may only appear in `boundary` or `hazard` modules");
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
                diagnostics_.error(decl.span, "foreign functions may only appear in `boundary` or `hazard` modules");
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

    DeclarationAdmission declaration_admission(const PackageRootDeclarationContext&, const ast::Decl& decl) {
        if (decl.kind != ast::DeclKind::Module) {
            diagnostics_.error(decl.span, "declarations must appear inside a module");
            return DeclarationAdmission::SkipDeclaration;
        }
        return DeclarationAdmission::AnalyzeDeclaration;
    }

    DeclarationAdmission declaration_admission(const ModuleBodyDeclarationContext& context, const ast::Decl& decl) {
        enforce_module_kind(context.module_kind, decl);
        return DeclarationAdmission::AnalyzeDeclaration;
    }

    template <typename DeclarationContext>
    void analyze_scope(const std::vector<std::unique_ptr<ast::Decl>>& decls,
                       const Scope& scope,
                       PublicReachability parent_reachability,
                       const DeclarationContext& context) {
        for (const auto& decl_ptr : decls) {
            const ast::Decl& decl = *decl_ptr;
            if (declaration_admission(context, decl) == DeclarationAdmission::SkipDeclaration) {
                continue;
            }
            const PublicNamePolicy declaration_name_policy = public_name_policy(decl.visibility);
            const PublicReachability declaration_reachability =
                declaration_public_reachability(parent_reachability, decl.visibility);

            check_name_under_public_policy(decl.name, decl.span, declaration_name_policy);
            check_builtin_type_declaration_name(decl.name, decl.span);
            if (decl.kind != ast::DeclKind::Module) {
                check_compiler_owned_collection_name(decl.name, decl.span);
            }

            switch (decl.kind) {
            case ast::DeclKind::Module: {
                const auto& module_decl = static_cast<const ast::ModuleDecl&>(decl);
                const auto it = scope.symbols.find(module_decl.name);
                if (it != scope.symbols.end() && it->second.child_scope != nullptr) {
                    analyze_scope(module_decl.members,
                                  *it->second.child_scope,
                                  declaration_reachability,
                                  ModuleBodyDeclarationContext{module_decl.module_kind});
                }
                break;
            }
            case ast::DeclKind::Record: {
                const auto& record_decl = static_cast<const ast::RecordDecl&>(decl);
                if (!record_decl.generic_params.empty()) {
                    check_generic_structural_name("generic record", record_decl.name, record_decl.span);
                }
                check_generic_parameters(record_decl.generic_params, "record");
                check_duplicate_fields(record_decl.fields, "record");
                std::vector<std::string> generics;
                for (const ast::GenericParam& generic : record_decl.generic_params) {
                    generics.push_back(generic.name);
                }
                for (const ast::Field& field : record_decl.fields) {
                    check_name_under_public_policy(field.name,
                                                   field.span,
                                                   field_name_policy(record_decl.visibility, field.visibility));
                    check_type_ref_usage(scope,
                                         generics,
                                         field.type,
                                         TypeUseRules{
                                             field_type_privacy_policy(record_decl.visibility, field.visibility),
                                             ReasonTypePolicy::RejectReasonTypes,
                                             PermitTypePolicy::RejectPermitTypes,
                                             "record field"});
                }
                break;
            }
            case ast::DeclKind::State: {
                const auto& state_decl = static_cast<const ast::StateDecl&>(decl);
                const PublicNamePolicy variant_name_policy = public_name_policy(state_decl.visibility);
                if (state_decl.variants.empty()) {
                    diagnostics_.error(state_decl.span, "state declarations must define at least one variant");
                }
                if (state_decl.variants.size() == 2) {
                    const VariantNamingRole first_variant_role = variant_naming_role(state_decl.variants[0].name);
                    const VariantNamingRole second_variant_role = variant_naming_role(state_decl.variants[1].name);
                    if (first_variant_role == VariantNamingRole::PseudoOptionalCarrierName
                        || second_variant_role == VariantNamingRole::PseudoOptionalCarrierName) {
                        diagnostics_.error(state_decl.span,
                                          "pseudo-optional state detected; model inhabited domain alternatives instead");
                    }
                }
                check_duplicate_variants(state_decl.variants, "state");
                for (const ast::Variant& variant : state_decl.variants) {
                    check_compiler_owned_collection_name(variant.name, variant.span);
                    check_name_under_public_policy(variant.name, variant.span, variant_name_policy);
                    check_duplicate_fields(variant.fields, "state variant");
                    for (const ast::Field& field : variant.fields) {
                        check_name_under_public_policy(field.name,
                                                       field.span,
                                                       public_name_policy(state_decl.visibility));
                        check_type_ref_usage(scope,
                                             {},
                                             field.type,
                                             TypeUseRules{
                                                 type_privacy_policy(state_decl.visibility),
                                                 ReasonTypePolicy::RejectReasonTypes,
                                                 PermitTypePolicy::RejectPermitTypes,
                                                 "state variant payload"});
                    }
                }
                break;
            }
            case ast::DeclKind::Reason: {
                const auto& reason_decl = static_cast<const ast::ReasonDecl&>(decl);
                const PublicNamePolicy variant_name_policy = public_name_policy(reason_decl.visibility);
                if (reason_decl.variants.empty()) {
                    diagnostics_.error(reason_decl.span, "reason declarations must define at least one variant");
                }
                check_duplicate_variants(reason_decl.variants, "reason");
                for (const ast::Variant& variant : reason_decl.variants) {
                    check_compiler_owned_collection_name(variant.name, variant.span);
                    check_name_under_public_policy(variant.name, variant.span, variant_name_policy);
                    check_duplicate_fields(variant.fields, "reason variant");
                    for (const ast::Field& field : variant.fields) {
                        check_name_under_public_policy(field.name,
                                                       field.span,
                                                       public_name_policy(reason_decl.visibility));
                        check_type_ref_usage(scope,
                                             {},
                                             field.type,
                                             TypeUseRules{
                                                 type_privacy_policy(reason_decl.visibility),
                                                 ReasonTypePolicy::RejectReasonTypes,
                                                 PermitTypePolicy::RejectPermitTypes,
                                                 "reason payload"});
                    }
                }
                break;
            }
            case ast::DeclKind::Proof: {
                const auto& proof_decl = static_cast<const ast::ProofDecl&>(decl);
                check_duplicate_fields(proof_decl.fields, "proof");
                for (const ast::Field& field : proof_decl.fields) {
                    check_name_under_public_policy(field.name,
                                                   field.span,
                                                   field_name_policy(proof_decl.visibility, field.visibility));
                    check_type_ref_usage(scope,
                                         {},
                                         field.type,
                                         TypeUseRules{
                                             field_type_privacy_policy(proof_decl.visibility, field.visibility),
                                             ReasonTypePolicy::RejectReasonTypes,
                                             PermitTypePolicy::RejectPermitTypes,
                                             "proof field"});
                }
                break;
            }
            case ast::DeclKind::Permit:
                break;
            case ast::DeclKind::Phase: {
                const auto& phase_decl = static_cast<const ast::PhaseDecl&>(decl);
                const PublicNamePolicy position_name_policy = public_name_policy(phase_decl.visibility);
                if (phase_decl.positions.empty()) {
                    diagnostics_.error(phase_decl.span, "phase declarations must define at least one position");
                }
                check_duplicate_fields(phase_decl.fields, "phase");
                std::unordered_set<std::string> seen_positions;
                for (std::size_t index = 0; index < phase_decl.positions.size(); ++index) {
                    const std::string& pos = phase_decl.positions[index];
                    const SourceSpan position_span = index < phase_decl.position_spans.size()
                        ? phase_decl.position_spans[index]
                        : phase_decl.span;
                    check_name_under_public_policy(pos, position_span, position_name_policy);
                    if (!seen_positions.insert(pos).second) {
                        diagnostics_.error(position_span, "duplicate phase position '" + pos + "'");
                    }
                }
                for (const ast::Field& field : phase_decl.fields) {
                    check_name_under_public_policy(field.name,
                                                   field.span,
                                                   field_name_policy(phase_decl.visibility, field.visibility));
                    check_type_ref_usage(scope,
                                         {},
                                         field.type,
                                         TypeUseRules{
                                             field_type_privacy_policy(phase_decl.visibility, field.visibility),
                                             ReasonTypePolicy::RejectReasonTypes,
                                             PermitTypePolicy::RejectPermitTypes,
                                             "phase field"});
                }
                break;
            }
            case ast::DeclKind::Function:
            case ast::DeclKind::ForeignFunction: {
                const auto& function_decl = static_cast<const ast::FunctionDecl&>(decl);
                // Public functions must not expose private types in their signature even when the
                // enclosing module is private (visibility is per-declaration).
                const ast::FunctionImplementation implementation = function_decl.implementation();
                if (!function_decl.signature.generic_params.empty()) {
                    check_generic_structural_name("generic function", function_decl.name, function_decl.span);
                }
                check_function_signature(scope,
                                         function_decl.signature,
                                         type_privacy_policy(function_decl.visibility),
                                         implementation,
                                         {});
                if (implementation == ast::FunctionImplementation::ForeignImport && function_decl.body != nullptr) {
                    diagnostics_.error(function_decl.body->span, "foreign functions may not define a body");
                }
                if (function_decl.body != nullptr && implementation == ast::FunctionImplementation::EvidentBody) {
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
                BindingMoveState::AvailableForUse,
                BindingMoveOrigin{},
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
            if (permit_type_state(param_type) == PermitTypeState::PermitValueType) {
                const ast::PathExpr* path_arg = path_expr(*args[index]);
                if (path_arg == nullptr
                    || path_arg->path.size() != 1
                    || path_arg->argument_role != ast::PathArgumentRole::PermitArgument) {
                    diagnostics_.error(args[index]->span,
                                      "permit argument must be written as 'as name'");
                    continue;
                }
                const BindingState* binding = lookup_binding(env, path_arg->path.front());
                if (binding == nullptr
                    || permit_type_state(binding->type) != PermitTypeState::PermitValueType) {
                    diagnostics_.error(args[index]->span,
                                      "argument " + std::to_string(index + 1) + " to '" + function.name
                                          + "' must be permit '" + type_name(param_type) + "'");
                    continue;
                }
                if (assignment_compatibility(param_type, binding->type)
                    == AssignmentCompatibility::TypeMismatch) {
                    diagnostics_.error(args[index]->span,
                                      "argument " + std::to_string(index + 1) + " to '" + function.name
                                          + "' has type '" + type_name(binding->type) + "', expected '"
                                          + type_name(param_type) + "'");
                }
                continue;
            }

            ExprType arg_type = type_expr(*args[index], context, env, &param_type);
            if (arg_type.yielded_reason != nullptr) {
                diagnostics_.error(args[index]->span,
                                    "failing expression must be handled with `try` or `match`");
            }
            if (assignment_compatibility(param_type, arg_type.value)
                == AssignmentCompatibility::TypeMismatch) {
                diagnostics_.error(args[index]->span,
                                  "argument " + std::to_string(index + 1) + " to '" + function.name
                                      + "' has type '" + type_name(arg_type.value) + "', expected '"
                                      + type_name(param_type) + "'");
            }
        }
    }

    FieldInitializationCheck check_initializer_fields(const ast::Decl* owner_decl,
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
                        && scope_containment(owner_scope, &context.scope)
                            == ScopeContainment::SeparateScopeBranch) {
                        diagnostics_.error(construct_span,
                                            "cannot construct here: field '" + field.name + "' is private");
                        return FieldInitializationCheck::Rejected;
                    }
                }
            }
        }

        FieldInitializationCheck check = FieldInitializationCheck::Accepted;
        std::unordered_map<std::string, const ast::Field*> expected;
        for (const ast::Field& field : expected_fields) {
            expected.emplace(field.name, &field);
        }

        std::unordered_set<std::string> seen;
        for (const ast::RecordFieldInit& init : actual_fields) {
            if (!seen.insert(init.name).second) {
                diagnostics_.error(init.span, "duplicate initializer for field '" + init.name + "'");
                check = FieldInitializationCheck::Rejected;
                continue;
            }
            const auto it = expected.find(init.name);
            if (it == expected.end()) {
                diagnostics_.error(init.span, "unknown field '" + init.name + "' in initializer");
                check = FieldInitializationCheck::Rejected;
                continue;
            }
            const Type expected_type = owner_decl != nullptr
                ? resolve_member_type(owner_decl, owner_args, it->second->type)
                : resolve_type(context.scope, context.generics, context.substitutions, it->second->type);
            ExprType init_type = type_expr(*init.value, context, env, &expected_type);
            if (init_type.yielded_reason != nullptr) {
                diagnostics_.error(init.span, "failing expression must be handled with `try` or `match`");
                check = FieldInitializationCheck::Rejected;
            }
            if (assignment_compatibility(expected_type, init_type.value)
                == AssignmentCompatibility::TypeMismatch) {
                diagnostics_.error(init.span,
                                  "initializer for field '" + init.name + "' has type '" + type_name(init_type.value)
                                      + "', expected '" + type_name(expected_type) + "'");
                check = FieldInitializationCheck::Rejected;
            }
        }

        for (const ast::Field& field : expected_fields) {
            if (!seen.contains(field.name)) {
                diagnostics_.error(construct_span, "missing initializer for field '" + field.name + "'");
                check = FieldInitializationCheck::Rejected;
            }
        }
        return check;
    }

    ExprType type_path_expr(const ast::PathExpr& expr, const FunctionContext& context, ValueEnv& env) {
        if (expr.path.size() == 1) {
            if (const BindingState* binding = lookup_binding(env, expr.path.front()); binding != nullptr) {
                if (permit_type_state(binding->type) == PermitTypeState::PermitValueType) {
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
                    if (check_import_access(context.scope, expr.path, expr.span)
                        == CrossModuleReferenceAccess::MatchingImportRequired) {
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

        VariantResolution state_variant = resolve_variant(
            context.scope,
            expr.path,
            VariantOwnerKind::StateDeclaration);
        if (state_variant.state == VariantResolutionState::AmbiguousVariant) {
            diagnostics_.error(expr.span, "ambiguous variant reference '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (state_variant.variant != nullptr) {
            if (check_import_access(context.scope, expr.path, expr.span)
                == CrossModuleReferenceAccess::MatchingImportRequired) {
                return make_expr(error_type());
            }
            if (!state_variant.variant->fields.empty()) {
                diagnostics_.error(expr.span,
                                  "variant '" + state_variant.variant->name + "' requires payload construction");
                return make_expr(error_type());
            }
            return make_expr(named_type(state_variant.owner_decl));
        }

        VariantResolution reason_variant = resolve_variant(
            context.scope,
            expr.path,
            VariantOwnerKind::ReasonDeclaration);
        if (reason_variant.state == VariantResolutionState::AmbiguousVariant) {
            diagnostics_.error(expr.span, "ambiguous reason variant reference '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (reason_variant.variant != nullptr) {
            diagnostics_.error(expr.span, "reason variants may appear only in 'fail' or failed(...) patterns");
            return make_expr(error_type());
        }

        if (const ast::FunctionDecl* function = resolve_function(context.scope, expr.path); function != nullptr) {
            if (check_import_access(context.scope, expr.path, expr.span)
                == CrossModuleReferenceAccess::MatchingImportRequired) {
                return make_expr(error_type());
            }
            diagnostics_.error(expr.span, "function values are not first-class; call '" + function->name + "' with '(...)'");
            return make_expr(error_type());
        }

        if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
            if (check_import_access(context.scope, expr.path, expr.span)
                == CrossModuleReferenceAccess::MatchingImportRequired) {
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
        const ast::PathExpr* callee_path = path_expr(*expr.callee);
        if (callee_path == nullptr) {
            diagnostics_.error(expr.span, "callee must be a named function");
            return make_expr(error_type());
        }

        const ast::FunctionDecl* function = resolve_function(context.scope, callee_path->path);
        if (function == nullptr) {
            diagnostics_.error(expr.span, "unknown function '"
                                              + final_path_segment_or_malformed_path(callee_path->path) + "'");
            return make_expr(error_type());
        }
        if (check_import_access(context.scope, callee_path->path, callee_path->span)
            == CrossModuleReferenceAccess::MatchingImportRequired) {
            return make_expr(error_type());
        }
        if (function->signature.authority.effect() == ast::FunctionAuthorityEffect::GrantsScopedPermit) {
            diagnostics_.error(expr.span, "function '" + function->name + "' grants a permit and must be used with 'grant'");
            return make_expr(error_type());
        }
        std::vector<std::pair<std::string, Type>> substitutions;
        if (prepare_call_type_substitutions(*function, expr, context, substitutions)
            == TypeArgumentPreparation::RejectedByDiagnostics) {
            return make_expr(error_type());
        }
        const Scope* callee_scope = declaring_scope(function);
        if (callee_scope == nullptr) {
            return make_expr(error_type());
        }
        const std::vector<std::string> callee_generics = generic_names_for(function->signature.generic_params);
        check_collection_operation_availability(*function, expr, substitutions);
        check_call_arguments(*function, expr.args, context, env, *callee_scope, callee_generics, substitutions, expr.span);
        check_generic_function_instantiation(*function, *callee_scope, substitutions);

        ExprType result = make_expr(resolve_type(*callee_scope,
                                                 callee_generics,
                                                 substitutions,
                                                 function->signature.return_type));
        if (function->signature.failure.behavior() == ast::FunctionFailureBehavior::YieldsReason) {
            const Type fails_type = resolve_type(*callee_scope,
                                                 callee_generics,
                                                 substitutions,
                                                 function->signature.failure.reason_type());
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
                if (check_import_access(context.scope, expr.path, expr.span)
                    == CrossModuleReferenceAccess::MatchingImportRequired) {
                    return make_expr(error_type());
                }
                const Scope* const phase_scope = declaring_scope(sym->decl);
                if (phase_scope != nullptr
                    && scope_containment(phase_scope, &context.scope)
                        == ScopeContainment::SeparateScopeBranch) {
                    diagnostics_.error(expr.span,
                                      "cannot construct phase position '" + qualified_names_.at(sym->decl) + "::"
                                          + position_name + "' outside its declaring module");
                    return make_expr(error_type());
                }
                if (check_initializer_fields(sym->decl, pd.fields, expr.fields, context, env, expr.span)
                    != FieldInitializationCheck::Accepted) {
                    return make_expr(error_type());
                }
                const std::string qn = qualified_names_.at(sym->decl) + "::" + position_name;
                return make_expr(typesys::named_type(qn, sym->decl));
            }
        }

        if (const Symbol* symbol = resolve_symbol(context.scope, context.generics, expr.path); symbol != nullptr) {
            if (check_import_access(context.scope, expr.path, expr.span)
                == CrossModuleReferenceAccess::MatchingImportRequired) {
                return make_expr(error_type());
            }
            switch (symbol->kind) {
            case ast::DeclKind::Record: {
                const auto* record_decl = static_cast<const ast::RecordDecl*>(symbol->decl);
                std::vector<Type> type_args;
                if (prepare_record_constructor_type_args(*record_decl, expr, context, type_args)
                    == TypeArgumentPreparation::RejectedByDiagnostics) {
                    return make_expr(error_type());
                }
                if (check_initializer_fields(symbol->decl,
                                             record_decl->fields,
                                             expr.fields,
                                             context,
                                             env,
                                             expr.span,
                                             type_args)
                    != FieldInitializationCheck::Accepted) {
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

        VariantResolution state_variant = resolve_variant(
            context.scope,
            expr.path,
            VariantOwnerKind::StateDeclaration);
        if (state_variant.state == VariantResolutionState::AmbiguousVariant) {
            diagnostics_.error(expr.span, "ambiguous variant constructor '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (state_variant.variant != nullptr) {
            if (!expr.type_args.empty()) {
                diagnostics_.error(expr.span,
                                  "variant constructor '" + expr.path.back() + "' may not have generic arguments");
                return make_expr(error_type());
            }
            if (check_import_access(context.scope, expr.path, expr.span)
                == CrossModuleReferenceAccess::MatchingImportRequired) {
                return make_expr(error_type());
            }
            if (check_initializer_fields(state_variant.owner_decl,
                                         state_variant.variant->fields,
                                         expr.fields,
                                         context,
                                         env,
                                         expr.span)
                != FieldInitializationCheck::Accepted) {
                return make_expr(error_type());
            }
            return make_expr(named_type(state_variant.owner_decl));
        }

        VariantResolution reason_variant = resolve_variant(
            context.scope,
            expr.path,
            VariantOwnerKind::ReasonDeclaration);
        if (reason_variant.variant != nullptr) {
            diagnostics_.error(expr.span, "reason values may not be constructed directly; use 'fail'");
            return make_expr(error_type());
        }

        diagnostics_.error(expr.span, "unknown constructor '" + expr.path.back() + "'");
        return make_expr(error_type());
    }

    ExprType type_grant_expr(const ast::GrantExpr& expr,
                             const FunctionContext& context,
                             ValueEnv& env,
                             const Type* expected_type = nullptr) {
        const ast::CallExpr* grant_call = call_expr(*expr.grant_call);
        if (grant_call == nullptr) {
            diagnostics_.error(expr.span, "'grant' requires a direct grantor call");
            return make_expr(error_type());
        }
        const ast::PathExpr* callee_path = path_expr(*grant_call->callee);
        if (callee_path == nullptr) {
            diagnostics_.error(expr.span, "'grant' requires a named grantor function");
            return make_expr(error_type());
        }

        const ast::FunctionDecl* function = resolve_function(context.scope, callee_path->path);
        if (function == nullptr) {
            diagnostics_.error(expr.span,
                              "unknown function '"
                                  + final_path_segment_or_malformed_path(callee_path->path) + "'");
            return make_expr(error_type());
        }
        if (check_import_access(context.scope, callee_path->path, callee_path->span)
            == CrossModuleReferenceAccess::MatchingImportRequired) {
            return make_expr(error_type());
        }
        if (!function->signature.generic_params.empty()) {
            diagnostics_.error(expr.span, "'grant' may not call a generic grantor function");
            return make_expr(error_type());
        }
        if (function->signature.authority.effect() != ast::FunctionAuthorityEffect::GrantsScopedPermit) {
            diagnostics_.error(expr.span, "'grant' requires a function annotated with 'grants'");
            return make_expr(error_type());
        }

        const Scope* callee_scope = declaring_scope(function);
        if (callee_scope == nullptr) {
            return make_expr(error_type());
        }

        // Spec allows fails + grants together.
        const Type return_type = resolve_type(*callee_scope, {}, function->signature.return_type);
        if (type_equivalence(return_type, builtin_type("Unit")) == typesys::TypeEquivalence::Different) {
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
        if (function->signature.failure.behavior() == ast::FunctionFailureBehavior::YieldsReason) {
            const Type fails_type = resolve_type(*callee_scope, {}, function->signature.failure.reason_type());
            if (fails_type.decl != nullptr && fails_type.decl->kind == ast::DeclKind::Reason) {
                grantor_reason = static_cast<const ast::ReasonDecl*>(fails_type.decl);
            }
        }

        ValueEnv scoped_env = push_scope(env);
        const Type permit_type = resolve_type(*callee_scope, {}, function->signature.authority.permit_type());
        bind_value(scoped_env, expr.binder_name, permit_type, expr.span);
        ExprType result = type_block(*expr.body, context, scoped_env, expected_type);
        if (result.reachability == ControlFlowReachability::ContinuesToFollowingCode) {
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
            diagnostics_.error(expr.span, "unknown proof type '"
                                              + final_path_segment_or_malformed_path(expr.path) + "'");
            return make_expr(error_type());
        }
        if (symbol->kind != ast::DeclKind::Proof) {
            diagnostics_.error(expr.span,
                               "'prove' expects a proof type, found '"
                                   + std::string(ast::decl_kind_name(symbol->kind)) + "'");
            return make_expr(error_type());
        }
        if (check_import_access(context.scope, expr.path, expr.span)
            == CrossModuleReferenceAccess::MatchingImportRequired) {
            return make_expr(error_type());
        }

        const auto* proof_decl = static_cast<const ast::ProofDecl*>(symbol->decl);
        if (context.proves_proofs.empty()) {
            diagnostics_.error(expr.span, "'prove' is only valid inside a function with 'proves'");
            return make_expr(named_type(symbol->decl));
        }
        ProofMintingAuthorization authorization =
            ProofMintingAuthorization::ProofNotListedInFunctionContract;
        for (const ast::ProofDecl* allowed : context.proves_proofs) {
            if (allowed == proof_decl) {
                authorization = ProofMintingAuthorization::AuthorizedByFunctionContract;
                break;
            }
        }
        if (authorization != ProofMintingAuthorization::AuthorizedByFunctionContract) {
            diagnostics_.error(expr.span,
                              "'prove' must name one of the proof types listed in the function's `proves` clauses");
        }
        if (check_initializer_fields(proof_decl, proof_decl->fields, expr.fields, context, env, expr.span)
            != FieldInitializationCheck::Accepted) {
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

        VariantResolution variant = resolve_variant(
            context.scope,
            expr.path,
            VariantOwnerKind::ReasonDeclaration);
        if (variant.state == VariantResolutionState::AmbiguousVariant) {
            diagnostics_.error(expr.span, "ambiguous reason variant '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (variant.variant == nullptr || variant.owner_decl == nullptr) {
            diagnostics_.error(expr.span, "unknown reason variant '" + expr.path.back() + "'");
            return make_expr(error_type());
        }
        if (check_import_access(context.scope, expr.path, expr.span)
            == CrossModuleReferenceAccess::MatchingImportRequired) {
            return make_expr(error_type());
        }
        if (variant.owner_decl != context.fails_reason) {
            diagnostics_.error(expr.span,
                              "'fail' must use a variant of failure reason '" + context.fails_reason->name + "'");
        }
        if (check_initializer_fields(variant.owner_decl, variant.variant->fields, expr.fields, context, env, expr.span)
            != FieldInitializationCheck::Accepted) {
            return make_expr(error_type());
        }
        return make_expr(builtin_type("Never"));
    }

    Type unify_match_result(const Type& current, const Type& next, SourceSpan span) {
        if (type_error_state(current) == typesys::TypeErrorState::SuppressesFollowupDiagnostics
            || type_error_state(next) == typesys::TypeErrorState::SuppressesFollowupDiagnostics) {
            return error_type();
        }
        if (never_type_state(current) == typesys::NeverTypeState::DivergesBeforeFollowingCode) {
            return next;
        }
        if (never_type_state(next) == typesys::NeverTypeState::DivergesBeforeFollowingCode) {
            return current;
        }
        if (type_equivalence(current, next) == typesys::TypeEquivalence::Different) {
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

    ExprType type_match_expr(const ast::MatchExpr& expr,
                             const FunctionContext& context,
                             ValueEnv& env,
                             const Type* expected_type = nullptr) {
        ExprType scrutinee = type_expr(*expr.scrutinee, context, env);
        Type result_type = builtin_type("Never");
        MatchArmResultCoverage arm_result_coverage = MatchArmResultCoverage::NoArmResultAvailable;
        std::vector<BranchState> arm_states;

        if (scrutinee.yielded_reason != nullptr) {
            FailingMatchSuccessCoverage success_coverage = FailingMatchSuccessCoverage::SucceededArmMissing;
            std::unordered_set<std::string> seen_failed;
            for (const ast::MatchArm& arm : expr.arms) {
                ValueEnv arm_env = push_scope(env);
                switch (arm.pattern->kind) {
                case ast::PatternKind::Succeeded: {
                    const auto& pattern = static_cast<const ast::SucceededPattern&>(*arm.pattern);
                    if (success_coverage == FailingMatchSuccessCoverage::SucceededArmObserved) {
                        diagnostics_.error(arm.pattern->span, "duplicate succeeded(...) arm");
                    }
                    success_coverage = FailingMatchSuccessCoverage::SucceededArmObserved;
                    if (pattern.binding == ast::SuccessPatternBinding::NamedBinding) {
                        bind_value(arm_env, pattern.binding_name, scrutinee.value, arm.pattern->span);
                    }
                    break;
                }
                case ast::PatternKind::Failed: {
                    const auto& pattern = static_cast<const ast::FailedPattern&>(*arm.pattern);
                    if (pattern.variant->path.size() == 1 && pattern.variant->path.front() == "_") {
                        diagnostics_.error(pattern.span, "wildcard patterns are not allowed");
                        break;
                    }
                    VariantResolution failure = resolve_variant(
                        context.scope,
                        pattern.variant->path,
                        VariantOwnerKind::ReasonDeclaration);
                    if (failure.state == VariantResolutionState::AmbiguousVariant) {
                        diagnostics_.error(pattern.span, "ambiguous failed(...) pattern");
                    } else if (failure.variant == nullptr || failure.owner_decl != scrutinee.yielded_reason) {
                        diagnostics_.error(pattern.span,
                                          "failed(...) arm must match a variant of reason '"
                                              + scrutinee.yielded_reason->name + "'");
                    } else if (check_import_access(context.scope, pattern.variant->path, pattern.span)
                               == CrossModuleReferenceAccess::MatchingImportRequired) {
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

                ExprType arm_type = type_expr(*arm.body, context, arm_env, expected_type);
                if (arm_type.yielded_reason != nullptr) {
                    diagnostics_.error(arm.body->span,
                                        "failing expression must be handled with `try` or `match`");
                }
                result_type = unify_match_result(result_type, arm_type.value, arm.span);
                arm_result_coverage = MatchArmResultCoverage::ArmResultAvailable;
                arm_states.push_back(BranchState{std::move(arm_env), arm_type.reachability});
            }

            if (success_coverage == FailingMatchSuccessCoverage::SucceededArmMissing) {
                diagnostics_.error(expr.span, "match over a `fails` call must include a succeeded(...) arm");
            }
            for (const ast::Variant& variant : scrutinee.yielded_reason->variants) {
                if (!seen_failed.contains(variant.name)) {
                    diagnostics_.error(expr.span,
                                      "non-exhaustive failed(...) coverage; missing '" + variant.name + "'");
                }
            }
            merge_branch_envs(env, arm_states);
            return make_expr(arm_result_coverage == MatchArmResultCoverage::ArmResultAvailable
                ? result_type
                : builtin_type("Unit"));
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
            VariantResolution resolution = resolve_variant(
                context.scope,
                pattern.path,
                VariantOwnerKind::StateDeclaration);
            if (resolution.state == VariantResolutionState::AmbiguousVariant) {
                diagnostics_.error(pattern.span, "ambiguous variant pattern '" + pattern.path.back() + "'");
                continue;
            }
            if (resolution.variant == nullptr || resolution.owner_decl != state_decl) {
                diagnostics_.error(pattern.span,
                                  "pattern must match a variant of state '" + state_decl->name + "'");
                continue;
            }
            if (check_import_access(context.scope, pattern.path, pattern.span)
                == CrossModuleReferenceAccess::MatchingImportRequired) {
                continue;
            }
            if (!seen_variants.insert(resolution.variant->name).second) {
                diagnostics_.error(pattern.span,
                                  "duplicate match arm for variant '" + resolution.variant->name + "'");
            }

            ValueEnv arm_env = push_scope(env);
            bind_variant_pattern(pattern, *resolution.variant, context, arm_env);
            ExprType arm_type = type_expr(*arm.body, context, arm_env, expected_type);
            if (arm_type.yielded_reason != nullptr) {
                diagnostics_.error(arm.body->span,
                                    "failing expression must be handled with `try` or `match`");
            }
            result_type = unify_match_result(result_type, arm_type.value, arm.span);
            arm_result_coverage = MatchArmResultCoverage::ArmResultAvailable;
            arm_states.push_back(BranchState{std::move(arm_env), arm_type.reachability});
        }

        for (const ast::Variant& variant : state_decl->variants) {
            if (!seen_variants.contains(variant.name)) {
                diagnostics_.error(expr.span,
                                  "non-exhaustive match over state '" + state_decl->name + "'; missing '"
                                      + variant.name + "'");
            }
        }
        merge_branch_envs(env, arm_states);
        return make_expr(arm_result_coverage == MatchArmResultCoverage::ArmResultAvailable
            ? result_type
            : builtin_type("Unit"));
    }

    ExprType type_block(const ast::BlockExpr& block,
                        const FunctionContext& context,
                        ValueEnv& parent_env,
                        const Type* expected_type = nullptr) {
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
                if (init_type.reachability != ControlFlowReachability::ContinuesToFollowingCode) {
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
                if (expr_type.reachability != ControlFlowReachability::ContinuesToFollowingCode) {
                    return expr_type;
                }
                break;
            }
            }
        }

        if (block.result != nullptr) {
            ExprType result = type_expr(*block.result, context, local, expected_type);
            if (result.reachability == ControlFlowReachability::ContinuesToFollowingCode) {
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
                    if (permit_type_state(binding->type) == PermitTypeState::PermitValueType) {
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
        if (type_error_state(recv.value) == typesys::TypeErrorState::SuppressesFollowupDiagnostics) {
            return recv;
        }

        const CollectionCompanionRecordField companion_field =
            collection_companion_record_field_type(recv.value, expr.field_name);
        switch (companion_field.state) {
        case CollectionCompanionRecordFieldState::NotCollectionCompanionRecord:
            break;
        case CollectionCompanionRecordFieldState::MalformedCollectionCompanionRecord:
            return make_expr(error_type());
        case CollectionCompanionRecordFieldState::FieldFound:
            if (typesys::discipline_movement(discipline(recv.value)) == typesys::DisciplineMovement::Affine
                && discipline(companion_field.type) != typesys::UseDiscipline::Copyable) {
                diagnostics_.error(expr.span, "field access on affine value requires a copyable field type");
                return make_expr(error_type());
            }
            return make_expr(companion_field.type);
        case CollectionCompanionRecordFieldState::FieldNotFound:
            diagnostics_.error(expr.span,
                               "type '" + type_name(recv.value) + "' has no field '" + expr.field_name + "'");
            return make_expr(error_type());
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
                        && scope_containment(owner_scope, &context.scope)
                            == ScopeContainment::SeparateScopeBranch) {
                        diagnostics_.error(expr.span, "field '" + expr.field_name + "' is private");
                        return make_expr(error_type());
                    }
                    const Type field_ty = resolve_member_type(decl, recv.value.args, f.type);
                    if (typesys::discipline_movement(discipline(recv.value)) == typesys::DisciplineMovement::Affine
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
                        && scope_containment(owner_scope, &context.scope)
                            == ScopeContainment::SeparateScopeBranch) {
                        diagnostics_.error(expr.span, "field '" + expr.field_name + "' is private");
                        return make_expr(error_type());
                    }
                    const Type field_ty = resolve_member_type(decl, recv.value.args, f.type);
                    if (typesys::discipline_movement(discipline(recv.value)) == typesys::DisciplineMovement::Affine
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
                        && scope_containment(owner_scope, &context.scope)
                            == ScopeContainment::SeparateScopeBranch) {
                        diagnostics_.error(expr.span, "field '" + expr.field_name + "' is private");
                        return make_expr(error_type());
                    }
                    const Type field_ty = resolve_member_type(decl, recv.value.args, f.type);
                    if (typesys::discipline_movement(discipline(recv.value)) == typesys::DisciplineMovement::Affine
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

    ExprType type_expr(const ast::Expr& expr,
                       const FunctionContext& context,
                       ValueEnv& env,
                       const Type* expected_type = nullptr) {
        switch (expr.kind) {
        case ast::ExprKind::NumberLiteral: {
            const auto& literal = static_cast<const ast::NumberLiteralExpr&>(expr);
            if (number_literal_kind(literal.lexeme) == NumberLiteralKind::Float) {
                if (!is_admitted_float_literal(literal.lexeme)) {
                    diagnostics_.error(expr.span,
                                       "float literal '" + literal.lexeme
                                           + "' is outside the finite Float range");
                    return make_expr(error_type());
                }
                return make_expr(builtin_type("Float"));
            }
            return make_expr(builtin_type("Int"));
        }
        case ast::ExprKind::StringLiteral:
            if (expected_type != nullptr
                && string_literal_typing_state(*expected_type) == StringLiteralTypingState::AcceptsStringLiteral) {
                return make_expr(*expected_type);
            }
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
            return type_match_expr(static_cast<const ast::MatchExpr&>(expr), context, env, expected_type);
        case ast::ExprKind::Block:
            return type_block(static_cast<const ast::BlockExpr&>(expr), context, env, expected_type);
        case ast::ExprKind::Fail:
            return type_fail_expr(static_cast<const ast::FailExpr&>(expr), context, env);
        case ast::ExprKind::Grant:
            return type_grant_expr(static_cast<const ast::GrantExpr&>(expr), context, env, expected_type);
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
        if (function_decl.signature.failure.behavior() == ast::FunctionFailureBehavior::YieldsReason) {
            const Type fails_type = resolve_type(scope,
                                                 context.generics,
                                                 context.substitutions,
                                                 function_decl.signature.failure.reason_type());
            context.fails_reason = fails_type.decl != nullptr ? static_cast<const ast::ReasonDecl*>(fails_type.decl) : nullptr;
        }
        for (const ast::TypeRef& proves_ast : function_decl.signature.proves_types) {
            const Type proves_type = resolve_type(scope, context.generics, context.substitutions, proves_ast);
            if (proves_type.decl != nullptr && proves_type.decl->kind == ast::DeclKind::Proof) {
                context.proves_proofs.push_back(static_cast<const ast::ProofDecl*>(proves_type.decl));
            }
        }

        ValueEnv env = make_root_env();
        std::unordered_set<std::string> bound_params;
        for (const ast::Parameter& param : function_decl.signature.params) {
            if (!bound_params.insert(param.name).second) {
                continue;
            }
            bind_value(env,
                       param.name,
                       resolve_type(scope, context.generics, context.substitutions, param.type),
                       param.span);
        }

        ExprType body_type = type_block(*function_decl.body, context, env, &context.return_type);
        if (body_type.yielded_reason != nullptr) {
            diagnostics_.error(function_decl.body->span,
                                "failing expression must be handled with `try` or `match`");
        }
        if (assignment_compatibility(context.return_type, body_type.value)
            == AssignmentCompatibility::TypeMismatch) {
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
