#ifndef F2C_INTERNAL_H
#define F2C_INTERNAL_H

#include "f2c/f2c.h"

#include <stddef.h>
#include <stdint.h>

typedef struct Buffer {
    char *data;
    size_t length;
    size_t capacity;
    int failed;
} Buffer;

typedef enum F2cTokenKind {
    F2C_TOKEN_END,
    F2C_TOKEN_IDENTIFIER,
    F2C_TOKEN_NUMBER,
    F2C_TOKEN_STRING,
    F2C_TOKEN_HOLLERITH,
    F2C_TOKEN_BOZ,
    F2C_TOKEN_LEFT_PAREN,
    F2C_TOKEN_RIGHT_PAREN,
    F2C_TOKEN_LEFT_BRACKET,
    F2C_TOKEN_RIGHT_BRACKET,
    F2C_TOKEN_ARRAY_BEGIN,
    F2C_TOKEN_ARRAY_END,
    F2C_TOKEN_COMMA,
    F2C_TOKEN_COLON,
    F2C_TOKEN_DOUBLE_COLON,
    F2C_TOKEN_SEMICOLON,
    F2C_TOKEN_PERCENT,
    F2C_TOKEN_OPERATOR,
    F2C_TOKEN_INVALID
} F2cTokenKind;

typedef struct F2cToken {
    F2cTokenKind kind;
    const char *begin;
    size_t length;
    size_t line;
    size_t column;
} F2cToken;

typedef struct F2cLexer {
    const char *source;
    const char *cursor;
    size_t line;
    size_t base_column;
    const char *error_at;
    F2cToken token;
} F2cLexer;

typedef struct Line {
    char *text;
    char *source_name;
    size_t number;
    size_t interface_depth;
    int emit_source_comments;
    F2cToken *tokens;
    size_t token_count;
} Line;

typedef struct Lines {
    Line *items;
    size_t count;
    size_t capacity;
} Lines;

typedef enum Type {
    TYPE_UNKNOWN,
    TYPE_INTEGER,
    TYPE_REAL,
    TYPE_DOUBLE,
    TYPE_COMPLEX,
    TYPE_DOUBLE_COMPLEX,
    TYPE_LOGICAL,
    TYPE_CHARACTER,
    TYPE_DERIVED
} Type;

#define F2C_MAX_RANK 15U

typedef enum F2cValueCategory {
    F2C_VALUE_INVALID,
    F2C_VALUE_CONSTANT,
    F2C_VALUE_VARIABLE,
    F2C_VALUE_TEMPORARY,
    F2C_VALUE_PROCEDURE,
    F2C_VALUE_TYPE
} F2cValueCategory;

typedef enum F2cDimensionKind {
    F2C_DIMENSION_EXPLICIT,
    F2C_DIMENSION_ASSUMED_SIZE,
    F2C_DIMENSION_ASSUMED_SHAPE,
    F2C_DIMENSION_DEFERRED
} F2cDimensionKind;

typedef enum F2cShapeKind {
    F2C_SHAPE_SCALAR,
    F2C_SHAPE_EXPLICIT,
    F2C_SHAPE_ASSUMED_SIZE,
    F2C_SHAPE_ASSUMED_SHAPE,
    F2C_SHAPE_DEFERRED,
    F2C_SHAPE_EXPRESSION,
    F2C_SHAPE_UNKNOWN
} F2cShapeKind;

typedef struct F2cShapeDimension {
    F2cDimensionKind kind;
    int lower_known;
    int extent_known;
    int64_t lower;
    uint64_t extent;
} F2cShapeDimension;

typedef struct F2cShape {
    F2cShapeKind kind;
    size_t rank;
    F2cShapeDimension dimensions[F2C_MAX_RANK];
} F2cShape;

typedef struct F2cExpr F2cExpr;

typedef struct Dimension {
    F2cDimensionKind kind;
    char *lower;
    char *upper;
    F2cExpr *lower_expression;
    F2cExpr *upper_expression;
} Dimension;

typedef enum F2cIntent {
    F2C_INTENT_UNSPECIFIED,
    F2C_INTENT_IN,
    F2C_INTENT_OUT,
    F2C_INTENT_INOUT
} F2cIntent;

struct Unit;
struct F2cDerivedType;

typedef struct Symbol {
    char *name;
    char *c_name;
    Type type;
    Type kind_type;
    int kind;
    F2cValueCategory value_category;
    F2cShape shape;
    Dimension dimensions[F2C_MAX_RANK];
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
    struct Unit *procedure_interface;
    char *procedure_interface_name;
    int saved;
    int statement_function;
    int statement_dummy;
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
    int deferred_character;
    int optional;
    int automatic_character;
    Type external_parameter_types[64];
    int external_parameter_kinds[64];
    size_t external_parameter_ranks[64];
    F2cIntent external_parameter_intents[64];
    int external_parameter_optional[64];
    int external_parameter_allocatable[64];
    int external_parameter_pointer[64];
    struct F2cDerivedType *external_parameter_derived_types[64];
    int external_parameter_polymorphic[64];
    struct Symbol *external_parameter_procedures[64];
    int external_parameter_const[64];
    size_t external_parameter_count;
    char *character_length;
    F2cExpr *character_length_expression;
    char *initializer;
    F2cExpr *initializer_expression;
    char *alias_to;
    int64_t alias_offset;
    char *common_block;
    size_t common_index;
    size_t declaration_line;
    size_t first_seen_line;
    size_t scope_begin_line;
    size_t scope_end_line;
    char *derived_type_name;
    char *c_type;
    struct F2cDerivedType *derived_type;
    struct F2cDerivedType *derived_owner;
} Symbol;

typedef struct F2cStatement F2cStatement;

typedef enum UnitKind { UNIT_PROGRAM, UNIT_SUBROUTINE, UNIT_FUNCTION, UNIT_MODULE } UnitKind;

typedef struct F2cNamelistGroup {
    char *name;
    char **members;
    size_t member_count;
} F2cNamelistGroup;

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
    struct F2cDerivedType *owner;
    struct F2cDerivedType *storage_owner;
    Symbol procedure;
} F2cTypeBinding;

typedef struct F2cDerivedType {
    char *name;
    char *c_name;
    char *parent_name;
    struct F2cDerivedType *parent;
    Symbol *components;
    size_t component_count;
    size_t component_capacity;
    char **finalizers;
    size_t finalizer_count;
    struct Unit **finalizer_procedures;
    size_t *finalizer_ranks;
    F2cTypeBinding *bindings;
    size_t binding_count;
    size_t binding_capacity;
    char *defined_io_bindings[F2C_DEFINED_IO_COUNT];
    int abstract_type;
    size_t begin;
    size_t end;
} F2cDerivedType;

typedef struct Unit {
    UnitKind kind;
    char *name;
    char *fortran_name;
    char **arguments;
    size_t argument_count;
    size_t begin;
    size_t end;
    size_t container_end;
    Type return_type;
    int return_kind;
    int return_type_explicit;
    char *result_name;
    char *result_character_length;
    Symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    F2cStatement *statements;
    size_t statement_count;
    F2cNamelistGroup *namelists;
    size_t namelist_count;
    size_t namelist_capacity;
    F2cDerivedType *derived_types;
    size_t derived_type_count;
    size_t derived_type_capacity;
    F2cDerivedType **imported_derived_types;
    size_t imported_derived_type_count;
    size_t imported_derived_type_capacity;
    Type implicit_types[26];
    int implicit_kinds[26];
    char *implicit_character_lengths[26];
    uint32_t implicit_explicit_mask;
    int implicit_map_initialized;
    int implicit_none;
    int implicit_none_external;
    int save_all;
    int internal;
    int interface_body;
    int interface_abstract;
    size_t host_index;
    struct Unit *signature_host;
    char *interface_generic_name;
    struct Unit *interfaces;
    size_t interface_count;
    size_t interface_capacity;
    F2cOptions options;
} Unit;

typedef struct Procedure {
    const char *name;
    Unit *definition;
} Procedure;

typedef struct Procedures {
    Procedure *items;
    size_t count;
    size_t capacity;
} Procedures;

typedef enum F2cExprKind {
    F2C_EXPR_INVALID,
    F2C_EXPR_INTEGER_LITERAL,
    F2C_EXPR_REAL_LITERAL,
    F2C_EXPR_STRING_LITERAL,
    F2C_EXPR_LOGICAL_LITERAL,
    F2C_EXPR_NAME,
    F2C_EXPR_UNARY,
    F2C_EXPR_BINARY,
    F2C_EXPR_CALL,
    F2C_EXPR_ARRAY_REFERENCE,
    F2C_EXPR_ARRAY_SECTION,
    F2C_EXPR_ARRAY_CONSTRUCTOR,
    F2C_EXPR_IMPLIED_DO,
    F2C_EXPR_KEYWORD_ARGUMENT,
    F2C_EXPR_ABSENT_ARGUMENT,
    F2C_EXPR_SUBSTRING,
    F2C_EXPR_COMPLEX_LITERAL,
    F2C_EXPR_COMPONENT,
    F2C_EXPR_STRUCTURE_CONSTRUCTOR
} F2cExprKind;

struct F2cExpr {
    F2cExprKind kind;
    Type type;
    int type_kind;
    size_t rank;
    int definable;
    F2cValueCategory value_category;
    F2cShape shape;
    char *text;
    char *source;
    char *lowered_c;
    size_t source_offset;
    size_t source_length;
    size_t parse_error_offset;
    Symbol *symbol;
    F2cDerivedType *derived_type;
    size_t temporary_index;
    struct F2cExpr **children;
    size_t child_count;
    size_t child_capacity;
};

typedef enum F2cStatementKind {
    F2C_STMT_EMPTY,
    F2C_STMT_DECLARATION,
    F2C_STMT_END_SELECT,
    F2C_STMT_CASE,
    F2C_STMT_SELECT_CASE,
    F2C_STMT_TYPE_GUARD,
    F2C_STMT_SELECT_TYPE,
    F2C_STMT_BLOCK_SCOPE,
    F2C_STMT_END_BLOCK_SCOPE,
    F2C_STMT_END_BLOCK,
    F2C_STMT_ELSE_IF,
    F2C_STMT_ELSE,
    F2C_STMT_IF,
    F2C_STMT_ARITHMETIC_IF,
    F2C_STMT_DO_WHILE,
    F2C_STMT_DO,
    F2C_STMT_WRITE,
    F2C_STMT_READ,
    F2C_STMT_PRINT,
    F2C_STMT_OPEN,
    F2C_STMT_REWIND,
    F2C_STMT_CLOSE,
    F2C_STMT_ALLOCATE,
    F2C_STMT_DEALLOCATE,
    F2C_STMT_DATA,
    F2C_STMT_CALL,
    F2C_STMT_MOVE_ALLOC,
    F2C_STMT_NULLIFY,
    F2C_STMT_POINTER_ASSIGNMENT,
    F2C_STMT_RETURN,
    F2C_STMT_STOP,
    F2C_STMT_CYCLE,
    F2C_STMT_EXIT,
    F2C_STMT_CONTINUE,
    F2C_STMT_ASSIGN_LABEL,
    F2C_STMT_GOTO,
    F2C_STMT_ASSIGNED_GOTO,
    F2C_STMT_LABEL,
    F2C_STMT_ASSIGNMENT,
    F2C_STMT_UNSUPPORTED
} F2cStatementKind;

typedef struct F2cIoItem {
    char *text;
    F2cExpr *expression;
    struct F2cIoItem *children;
    size_t child_count;
    F2cExpr *iterator;
    F2cExpr *initial;
    F2cExpr *limit;
    F2cExpr *step;
    int implied_do;
} F2cIoItem;

typedef enum F2cIoControlKind {
    F2C_IO_CONTROL_POSITIONAL,
    F2C_IO_CONTROL_UNKNOWN,
    F2C_IO_CONTROL_UNIT,
    F2C_IO_CONTROL_FMT,
    F2C_IO_CONTROL_NML,
    F2C_IO_CONTROL_END,
    F2C_IO_CONTROL_EOR,
    F2C_IO_CONTROL_ERR,
    F2C_IO_CONTROL_IOSTAT,
    F2C_IO_CONTROL_IOMSG,
    F2C_IO_CONTROL_SIZE,
    F2C_IO_CONTROL_ADVANCE,
    F2C_IO_CONTROL_REC,
    F2C_IO_CONTROL_POS,
    F2C_IO_CONTROL_FILE,
    F2C_IO_CONTROL_STATUS,
    F2C_IO_CONTROL_ACCESS,
    F2C_IO_CONTROL_ACTION,
    F2C_IO_CONTROL_FORM,
    F2C_IO_CONTROL_RECL,
    F2C_IO_CONTROL_BLANK,
    F2C_IO_CONTROL_DECIMAL,
    F2C_IO_CONTROL_DELIM,
    F2C_IO_CONTROL_ENCODING,
    F2C_IO_CONTROL_PAD,
    F2C_IO_CONTROL_ROUND,
    F2C_IO_CONTROL_SIGN,
    F2C_IO_CONTROL_ASYNCHRONOUS,
    F2C_IO_CONTROL_ID,
    F2C_IO_CONTROL_NEWUNIT
} F2cIoControlKind;

typedef struct F2cIoControl {
    F2cIoControlKind kind;
    char *keyword;
    F2cExpr *value;
    int asterisk;
} F2cIoControl;

typedef struct F2cDataValue {
    char *text;
    F2cExpr *expression;
    F2cExpr *repeat;
} F2cDataValue;

typedef struct F2cDataGroup {
    F2cIoItem *targets;
    size_t target_count;
    F2cDataValue *values;
    size_t value_count;
} F2cDataGroup;

struct F2cStatement {
    F2cStatementKind kind;
    size_t line;
    char *text;
    char *tail;
    char *name;
    char **items;
    F2cExpr **arguments;
    size_t item_count;
    F2cIoControl *io_controls;
    size_t control_count;
    F2cIoItem *io_items;
    size_t io_item_count;
    F2cDataGroup *data_groups;
    size_t data_group_count;
    F2cExpr *expression;
    F2cExpr *left;
    F2cExpr *right;
    F2cExpr *limit;
    F2cExpr *step;
    F2cExpr *allocation_character_length;
    F2cDerivedType *guard_type;
    char **labels;
    size_t label_count;
    F2cStatement *nested;
    int block;
    int unroll_hint;
};

typedef void (*F2cExpressionVisitor)(F2cExpr *expression, void *state);

typedef struct Units {
    Unit *items;
    size_t count;
    size_t capacity;
} Units;

typedef struct Context {
    F2cResult result;
    Buffer output;
    Buffer header;
    Buffer diagnostics;
    const F2cOptions *options;
    Lines lines;
    Units units;
    Units modules;
    Procedures procedures;
} Context;

void f2c_buffer_append_n(Buffer *buffer, const char *text, size_t length);
void f2c_buffer_append(Buffer *buffer, const char *text);
void f2c_buffer_printf(Buffer *buffer, const char *format, ...);
char *f2c_buffer_take(Buffer *buffer);
char *f2c_strdup_n(const char *text, size_t length);
char *f2c_strdup(const char *text);
char *f2c_trim(char *text);
void f2c_lowercase_code(char *text);
int f2c_starts_word(const char *text, const char *word);
void f2c_diagnostic(Context *context, size_t line, int error, const char *format, ...);
void f2c_diagnostic_at(Context *context, size_t line, size_t column, int error, const char *format,
                       ...);
int f2c_lines_push(Lines *lines, char *text, size_t number, const F2cOptions *options);
void f2c_free_unit(Unit *unit);

void f2c_lexer_init(F2cLexer *lexer, const char *source, size_t line, size_t base_column);
void f2c_lexer_next(F2cLexer *lexer);
int f2c_token_equals(const F2cToken *token, const char *text);
char *f2c_token_text(const F2cToken *token);
int f2c_hollerith_payload(const char *text, const char **payload, size_t *length);
int f2c_tokenize_lines(Context *context);

int f2c_normalize_source(Context *context, const char *source, size_t length, F2cSourceForm form);
int f2c_rewrite_labeled_do(Context *context);

char *f2c_identifier(const char *begin, size_t *consumed);
char **f2c_split_arguments(const char *open, size_t *count);
char **f2c_split_actual_arguments(const char *open, size_t *count);
char **f2c_split_comma_list(const char *text, size_t *count);
int f2c_interface_start_line(const char *line);
int f2c_interface_end_line(const char *line);
int f2c_parse_unit_header(const char *line, Unit *unit);
void f2c_parse_explicit_interfaces(Context *context, Unit *host);
Unit *f2c_find_interface_signature(Context *context, Unit *scope, const char *name,
                                   int include_abstract);
int f2c_discover_units(Context *context);
int f2c_discover_modules(Context *context);
Symbol *f2c_find_symbol(Unit *unit, const char *name);
Symbol *f2c_ensure_symbol(Unit *unit, const char *name);
F2cNamelistGroup *f2c_find_namelist(Unit *unit, const char *name);
F2cDerivedType *f2c_find_derived_type(Unit *unit, const char *name);
const char *f2c_symbol_c_name(Unit *unit, const Symbol *symbol);
int f2c_declaration_line(const char *line);
int f2c_unit_line_is_active(const Unit *unit, const Line *line);
void f2c_analyze_unit(Context *context, Unit *unit);
void f2c_analyze_module(Context *context, Unit *unit);
void f2c_parse_procedure_declaration(Context *context, Unit *unit, Line *source_line);
int f2c_copy_procedure_signature(Symbol *symbol, Unit *signature);
int f2c_copy_function_result_metadata(Symbol *symbol, Unit *signature);
void f2c_build_statement_ir(Context *context, Unit *unit);
void f2c_validate_unit_expressions(Context *context, Unit *unit);
void f2c_resolve_derived_semantics(Context *context);
void f2c_prepare_implicit_map(Context *context, Unit *unit);
void f2c_discover_implicit_symbols(Context *context, Unit *unit);
Type f2c_implicit_type_for_name(const Unit *unit, const char *name);
int f2c_implicit_kind_for_name(const Unit *unit, const char *name);
const char *f2c_implicit_character_length_for_name(const Unit *unit, const char *name);
void f2c_validate_implicit_external(Context *context, Unit *unit);
int f2c_build_procedure_registry(Context *context);
void f2c_import_module(Context *context, Unit *unit, Line *source_line);
void f2c_import_host_module(Context *context, Unit *unit);
int f2c_has_supported_module(const Context *context);
int f2c_supported_module_needs_complex(const Context *context);
void f2c_emit_supported_modules(Context *context);
void f2c_emit_project_modules(Context *context);
void f2c_emit_derived_types(Context *context);
void f2c_emit_procedure_pointer_type(Buffer *output, const Symbol *procedure, const char *name);

const char *f2c_c_type(Type type);
const char *f2c_c_type_kind(Type type, int kind);
const char *f2c_symbol_c_type(const Symbol *symbol);
const char *f2c_expression_c_type(const F2cExpr *expression);
int f2c_default_kind(Type type);
void f2c_shape_from_symbol(Unit *unit, F2cShape *shape, const Symbol *symbol);
int f2c_type_is_numeric(Type type);
Type f2c_common_numeric_type(Type left, Type right);
typedef enum F2cIntrinsicTypeRule {
    F2C_INTRINSIC_TYPE_FIRST,
    F2C_INTRINSIC_TYPE_COMMON,
    F2C_INTRINSIC_TYPE_ABSOLUTE,
    F2C_INTRINSIC_TYPE_DOUBLE,
    F2C_INTRINSIC_TYPE_REAL,
    F2C_INTRINSIC_TYPE_INTEGER,
    F2C_INTRINSIC_TYPE_COMPLEX,
    F2C_INTRINSIC_TYPE_DOUBLE_COMPLEX,
    F2C_INTRINSIC_TYPE_CHARACTER,
    F2C_INTRINSIC_TYPE_LOGICAL,
    F2C_INTRINSIC_TYPE_MOLD
} F2cIntrinsicTypeRule;

typedef enum F2cIntrinsicRankRule {
    F2C_INTRINSIC_RANK_SCALAR,
    F2C_INTRINSIC_RANK_ELEMENTAL,
    F2C_INTRINSIC_RANK_FIRST,
    F2C_INTRINSIC_RANK_MOLD
} F2cIntrinsicRankRule;

typedef struct F2cIntrinsicSignature {
    const char *name;
    size_t minimum_arguments;
    size_t maximum_arguments;
    F2cIntrinsicTypeRule type_rule;
    F2cIntrinsicRankRule rank_rule;
} F2cIntrinsicSignature;

const F2cIntrinsicSignature *f2c_find_intrinsic(const char *name);
int f2c_is_intrinsic_name(const char *name);
Type f2c_resolve_intrinsic_type(const char *name, const Type *arguments, size_t count);
size_t f2c_resolve_intrinsic_rank(const char *name, F2cExpr *const *arguments, size_t count);
char *f2c_emit_intrinsic(const char *name, char **arguments, const Type *argument_types,
                         size_t count, Type result_type);
char *f2c_emit_numeric_conversion(const char *operand, Type actual, Type target);
char *f2c_emit_scalar_temporary_address(const char *c_type, Type type, const char *value);
char *f2c_emit_binary(Unit *unit, const char *left, Type left_type, const char *operator_text,
                      const char *right, Type right_type, Type *result_type);
char *f2c_emit_array_reference(Unit *unit, Symbol *symbol, char **indices, size_t count);
char *f2c_symbol_dimension_lower(Unit *unit, const Symbol *symbol, size_t dimension);
char *f2c_symbol_dimension_upper(Unit *unit, const Symbol *symbol, size_t dimension);
char *f2c_symbol_dimension_extent(Unit *unit, const Symbol *symbol, size_t dimension);
F2cExpr *f2c_parse_expression_ast(Unit *unit, const char *expression, const char **error_at);
F2cExpr *f2c_expr_new_absent(Type type, size_t rank);
void f2c_expr_free(F2cExpr *expression);
char *f2c_emit_expression_ast(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_emit_pointer_designator(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_emit_cached_expression(Unit *unit, const F2cExpr *expression, const char *fallback_text);
Type f2c_expression_type(Unit *unit, const char *expression);
int f2c_evaluate_integer_constant(Unit *unit, const F2cExpr *expression, int64_t *value);
int f2c_evaluate_integer_text(Unit *unit, const char *text, int64_t *value);
int f2c_expression_is_designator(Unit *unit, const char *expression);
char *f2c_translate_expression(Unit *unit, const char *expression);
char *f2c_character_length_expression(Unit *unit, const F2cExpr *expression);
char *f2c_symbol_character_length(Unit *unit, const Symbol *symbol);
char *f2c_character_declaration_initializer(Unit *unit, const Symbol *symbol, int *supported);
char *f2c_character_source_pointer(Unit *unit, const F2cExpr *expression,
                                   const char *expression_code);
char *f2c_emit_character_comparison(Unit *unit, const F2cExpr *left, const char *left_code,
                                    const char *operator_text, const F2cExpr *right,
                                    const char *right_code);
char *f2c_emit_character_concatenation(Unit *unit, const F2cExpr *expression, const char *left_code,
                                       const char *right_code);
int f2c_emit_character_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                  const F2cExpr *left, const F2cExpr *right, const char *left_code,
                                  const char *right_code, int depth);
int f2c_emit_character_storage_assignment(Context *context, Unit *unit, const char *target_pointer,
                                          const char *target_length, const F2cExpr *right,
                                          const char *right_code, int depth);
char *f2c_find_assignment(char *line);
int f2c_parse_statement(Unit *unit, const char *text, size_t line, F2cStatement *statement);
void f2c_statement_free(F2cStatement *statement);
void f2c_visit_expression(F2cExpr *expression, F2cExpressionVisitor visitor, void *state);
void f2c_visit_statement_expressions(F2cStatement *statement, F2cExpressionVisitor visitor,
                                     void *state);

void f2c_emit_prototypes(Context *context);
void f2c_emit_procedure_prototype(Buffer *output, Unit *unit);
void f2c_emit_interface_header(Context *context);
void f2c_emit_common_blocks(Context *context);
void f2c_emit_unit_cleanup(Buffer *output, Unit *unit, int depth);
void f2c_emit_block_scope_begin(Buffer *output, Unit *unit, size_t line, int depth);
void f2c_emit_block_scope_end(Buffer *output, Unit *unit, size_t line, int depth);
void f2c_emit_scope_transfer_cleanup(Buffer *output, Unit *unit, size_t source_line,
                                     size_t target_line, int depth);
int f2c_unit_has_allocatable_result(Unit *unit);
void f2c_emit_call(Buffer *output, Unit *unit, const char *name, char *const *arguments,
                   F2cExpr *const *argument_expressions, size_t count, int depth);
void f2c_emit_call_with_signature(Buffer *output, Unit *unit, const char *name,
                                  const Symbol *callee, char *const *arguments,
                                  F2cExpr *const *argument_expressions, size_t count, int depth);
char *f2c_bridge_implicit_mutable_actual(const Symbol *callee, size_t parameter,
                                         const F2cExpr *actual, const char *code);
int f2c_emit_statement(Context *context, Unit *unit, const F2cStatement *statement,
                       Line *source_line, int *depth);
void f2c_emit_data_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth);
int f2c_emit_rank2_section_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                      const char *left_text, const char *right_text, int depth);
int f2c_emit_array_section_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                      const F2cExpr *right, int depth);
char *f2c_symbol_element_count(Unit *unit, Symbol *symbol);
int f2c_emit_whole_array_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                    const F2cExpr *right, size_t line, int depth);
int f2c_emit_transform_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                  const F2cExpr *right, size_t line, int depth);
int f2c_emit_allocate_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                int depth);
int f2c_emit_deallocate_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int depth);
int f2c_emit_allocatable_array_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                          const F2cExpr *right, int depth);
int f2c_emit_move_alloc_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int depth);
int f2c_emit_read_write_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int input, int depth);
void f2c_emit_namelist_support(Context *context);
void f2c_emit_format_support(Context *context);
int f2c_emit_print_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth);
int f2c_emit_open_statement(Context *context, Unit *unit, const F2cStatement *statement, int depth);
int f2c_emit_rewind_statement(Context *context, Unit *unit, const F2cStatement *statement,
                              int depth);
int f2c_emit_close_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth);
void f2c_emit_unit(Context *context, Unit *unit);

#endif
