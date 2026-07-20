#ifndef F2C_SEMANTIC_MODEL_H
#define F2C_SEMANTIC_MODEL_H

#include "frontend/token.h"
#include "ir/type.h"

typedef struct F2cDerivedType F2cDerivedType;
typedef struct F2cStatement F2cStatement;

typedef enum F2cAccessibility {
    F2C_ACCESS_UNSPECIFIED,
    F2C_ACCESSIBILITY_PUBLIC,
    F2C_ACCESSIBILITY_PRIVATE
} F2cAccessibility;

typedef struct F2cModuleAccessEntry {
    char *key;
    F2cAccessibility access;
    F2cSourceSpan span;
} F2cModuleAccessEntry;

struct Symbol {
    char *name;
    char *c_name;
    Type type;
    Type kind_type;
    int kind;
    F2cValueCategory value_category;
    F2cShape shape;
    Dimension dimensions[F2C_MAX_RANK];
    F2cTokenRange dimension_lower_syntax[F2C_MAX_RANK];
    F2cTokenRange dimension_upper_syntax[F2C_MAX_RANK];
    size_t rank;
    int argument;
    F2cIntent intent;
    int parameter;
    int external;
    int external_declared;
    int external_subroutine;
    int external_result_allocatable;
    size_t external_result_rank;
    int external_signature_observed;
    int external_signature_explicit;
    Unit *procedure_interface;
    char *procedure_interface_name;
    Unit **generic_candidates;
    size_t generic_candidate_count;
    Unit *generic_origin_scope;
    char *generic_origin_name;
    int saved;
    int statement_function;
    int statement_dummy;
    char **statement_function_arguments;
    size_t statement_function_argument_count;
    char *statement_function_text;
    F2cTokenRange statement_function_syntax;
    F2cExpr *statement_function_expression;
    size_t statement_function_line;
    int statement_function_expanding;
    int allocatable;
    int pointer;
    int procedure_pointer;
    int type_bound;
    int type_bound_deferred;
    int type_bound_nopass;
    size_t type_bound_pass_index;
    int polymorphic;
    int target;
    int module_entity;
    int use_associated;
    F2cAccessibility access;
    F2cSourceSpan access_span;
    int deferred_character;
    int optional;
    int automatic_character;
    Type *external_parameter_types;
    int *external_parameter_kinds;
    size_t *external_parameter_ranks;
    F2cIntent *external_parameter_intents;
    int *external_parameter_optional;
    int *external_parameter_allocatable;
    int *external_parameter_pointer;
    int *external_parameter_descriptor;
    F2cDerivedType **external_parameter_derived_types;
    int *external_parameter_polymorphic;
    Symbol **external_parameter_procedures;
    int *external_parameter_const;
    size_t external_parameter_count;
    size_t external_parameter_capacity;
    char *character_length;
    F2cTokenRange character_length_syntax;
    F2cExpr *character_length_expression;
    char *initializer;
    F2cTokenRange initializer_syntax;
    F2cExpr *initializer_expression;
    int data_initializer;
    F2cExpr **data_element_initializers;
    size_t data_element_initializer_count;
    char *alias_to;
    int64_t alias_offset;
    int equivalence_associated;
    size_t equivalence_group;
    uint64_t equivalence_offset;
    uint64_t equivalence_size;
    uint64_t equivalence_alignment;
    char *common_block;
    size_t common_index;
    uint64_t common_offset;
    uint64_t common_size;
    uint64_t common_alignment;
    F2cSourceSpan common_span;
    size_t declaration_line;
    F2cSourceSpan declaration_span;
    size_t first_seen_line;
    size_t scope_begin_line;
    size_t scope_end_line;
    char *derived_type_name;
    char *c_type;
    F2cDerivedType *derived_type;
    F2cDerivedType *derived_owner;
};

typedef enum UnitKind {
    UNIT_PROGRAM,
    UNIT_SUBROUTINE,
    UNIT_FUNCTION,
    UNIT_MODULE,
    UNIT_BLOCK_DATA
} UnitKind;

typedef enum F2cUnitPhase {
    F2C_UNIT_DISCOVERED,
    F2C_UNIT_SYMBOLS_RESOLVED,
    F2C_UNIT_TYPED_IR
} F2cUnitPhase;

typedef struct F2cNamelistGroup {
    char *name;
    char **members;
    size_t member_count;
} F2cNamelistGroup;

typedef struct F2cEquivalenceMember {
    char *symbol_name;
    int64_t element_offset;
    F2cSourceSpan span;
} F2cEquivalenceMember;

typedef struct F2cEquivalenceGroup {
    F2cEquivalenceMember *members;
    size_t member_count;
} F2cEquivalenceGroup;

typedef enum F2cDefinedIoKind {
    F2C_DEFINED_IO_READ_FORMATTED,
    F2C_DEFINED_IO_WRITE_FORMATTED,
    F2C_DEFINED_IO_READ_UNFORMATTED,
    F2C_DEFINED_IO_WRITE_UNFORMATTED,
    F2C_DEFINED_IO_COUNT
} F2cDefinedIoKind;

typedef struct F2cTypeBinding {
    char *name;
    char *target_name;
    char *interface_name;
    char *pass_name;
    int deferred;
    int nopass;
    int non_overridable;
    struct F2cTypeBinding *overridden;
    F2cDerivedType *owner;
    F2cDerivedType *storage_owner;
    Symbol procedure;
} F2cTypeBinding;

struct F2cDerivedType {
    char *name;
    char *c_name;
    char *parent_name;
    F2cDerivedType *parent;
    Symbol *components;
    size_t component_count;
    size_t component_capacity;
    char **finalizers;
    size_t finalizer_count;
    Unit **finalizer_procedures;
    size_t *finalizer_ranks;
    F2cTypeBinding *bindings;
    size_t binding_count;
    size_t binding_capacity;
    char *defined_io_bindings[F2C_DEFINED_IO_COUNT];
    int abstract_type;
    F2cAccessibility access;
    F2cSourceSpan access_span;
    size_t begin;
    size_t end;
};

typedef struct F2cImportedDerivedType {
    char *local_name;
    F2cDerivedType *type;
    F2cSourceSpan association_span;
    F2cAccessibility access;
    F2cSourceSpan access_span;
} F2cImportedDerivedType;

struct Unit {
    Context *context;
    UnitKind kind;
    F2cUnitPhase phase;
    F2cSourceSpan header_span;
    F2cSourceSpan name_span;
    F2cAccessibility access;
    F2cSourceSpan access_span;
    F2cAccessibility default_access;
    F2cSourceSpan default_access_span;
    int default_access_explicit;
    F2cModuleAccessEntry *access_entries;
    size_t access_entry_count;
    size_t access_entry_capacity;
    char *name;
    char *fortran_name;
    char **arguments;
    F2cSourceSpan *argument_spans;
    size_t argument_count;
    size_t begin;
    size_t end;
    size_t container_end;
    Type return_type;
    int return_kind;
    int return_type_explicit;
    F2cSourceSpan return_type_span;
    char *result_name;
    F2cSourceSpan result_name_span;
    char *result_character_length;
    F2cTokenRange result_character_length_syntax;
    char *result_derived_type_name;
    Symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    F2cStatement *statements;
    size_t statement_count;
    F2cNamelistGroup *namelists;
    size_t namelist_count;
    size_t namelist_capacity;
    F2cEquivalenceGroup *equivalence_groups;
    size_t equivalence_group_count;
    size_t equivalence_group_capacity;
    F2cDerivedType *derived_types;
    size_t derived_type_count;
    size_t derived_type_capacity;
    F2cImportedDerivedType *imported_derived_types;
    size_t imported_derived_type_count;
    size_t imported_derived_type_capacity;
    Type implicit_types[26];
    int implicit_kinds[26];
    char *implicit_character_lengths[26];
    F2cTokenRange implicit_character_length_syntax[26];
    uint32_t implicit_explicit_mask;
    int implicit_map_initialized;
    int implicit_none;
    int implicit_none_external;
    int save_all;
    int recursive;
    F2cSourceSpan recursive_span;
    int pure;
    F2cSourceSpan pure_span;
    int elemental;
    F2cSourceSpan elemental_span;
    int impure;
    F2cSourceSpan impure_span;
    int module_procedure;
    F2cSourceSpan module_procedure_span;
    int internal;
    int interface_body;
    int interface_abstract;
    size_t host_index;
    Unit *signature_host;
    char *interface_generic_name;
    Unit *interfaces;
    size_t interface_count;
    size_t interface_capacity;
    F2cOptions options;
};

typedef struct Procedure {
    const char *name;
    Unit *definition;
} Procedure;

typedef struct Procedures {
    Procedure *items;
    size_t count;
    size_t capacity;
} Procedures;

#endif
