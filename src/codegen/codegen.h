#ifndef F2C_CODEGEN_CODEGEN_H
#define F2C_CODEGEN_CODEGEN_H

#include "internal/context.h"

void f2c_emit_supported_modules(Context *context);
void f2c_emit_project_modules(Context *context);
void f2c_emit_derived_types(Context *context);
void f2c_emit_procedure_pointer_type(Buffer *output, const Symbol *procedure, const char *name);
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
char *f2c_emit_expression_ast(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_emit_pointer_designator(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_emit_typed_expression(Unit *unit, const F2cExpr *expression);
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
void f2c_emit_call(Buffer *output, Unit *unit, const char *name,
                   F2cExpr *const *argument_expressions, size_t count, int depth);
void f2c_emit_call_with_signature(Buffer *output, Unit *unit, const char *name,
                                  const Symbol *callee, F2cExpr *const *argument_expressions,
                                  size_t count, int depth);
char *f2c_bridge_implicit_mutable_actual(const Symbol *callee, size_t parameter,
                                         const F2cExpr *actual, const char *code);
int f2c_emit_statement(Context *context, Unit *unit, const F2cStatement *statement,
                       Line *source_line, int *depth);
void f2c_emit_data_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth);
int f2c_emit_rank2_section_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                      const F2cExpr *right, int depth);
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
