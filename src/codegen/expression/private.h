#ifndef F2C_CODEGEN_EXPRESSION_PRIVATE_H
#define F2C_CODEGEN_EXPRESSION_PRIVATE_H

#include "internal/f2c.h"

char *f2c_expression_emit(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_emit_array_reference(Unit *unit, const F2cExpr *expression, int *supported);
void f2c_expression_append_component(Buffer *output, const char *base,
                                     const F2cDerivedType *dynamic_type, const Symbol *component);
char *f2c_expression_real_literal(const F2cExpr *expression);
char *f2c_expression_integer_literal(const F2cExpr *expression);
char *f2c_expression_string_literal(const char *text);
char *f2c_expression_boz_literal(const char *text);
char *f2c_expression_name(Unit *unit, const F2cExpr *expression, int *supported);
void f2c_expression_free_arguments(char **arguments, Type *types, size_t count);
int f2c_expression_children(Unit *unit, const F2cExpr *expression, char ***arguments_out,
                            Type **types_out);
char *f2c_expression_call(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_associated_scalar_target(Unit *unit, const F2cExpr *target, int *supported);
char *f2c_expression_associated_array_target(Unit *unit, const F2cExpr *pointer,
                                             const F2cExpr *target, const char *pointer_storage,
                                             int *supported);
char *f2c_expression_descriptor_actual(Buffer *setup, Buffer *cleanup, Unit *unit,
                                       const F2cExpr *actual, F2cIntent intent, int *supported);
char *f2c_expression_wrap_contiguous_call(const F2cExpr *expression, int allocatable_result,
                                          Buffer *setup, Buffer *cleanup, char *call,
                                          int *supported);
char *f2c_expression_bit_intrinsic(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_character_intrinsic(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_numeric_model_intrinsic(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_numeric_operation_intrinsic(Unit *unit, const F2cExpr *expression,
                                                 int *supported);
char *f2c_expression_derived_actual_pointer(Unit *unit, const F2cExpr *expression, int *supported);
void f2c_expression_append_derived_actual_releases(Buffer *output, const F2cExpr *expression,
                                                   size_t first);
char *f2c_expression_real_representation_intrinsic(Unit *unit, const F2cExpr *expression,
                                                   int *supported);
char *f2c_expression_array_inquiry(Unit *unit, const F2cExpr *expression, int *supported);
int f2c_expression_array_view(Unit *unit, const F2cExpr *array, char **pointer, char **count,
                              char **stride, int *supported);
char *f2c_expression_relation_reduction(Unit *unit, const F2cExpr *expression, int *supported,
                                        int *matched);
char *f2c_expression_statement_function(Unit *unit, const F2cExpr *expression, int *supported);

#endif
