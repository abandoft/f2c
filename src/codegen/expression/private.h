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
char *f2c_expression_bit_intrinsic(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_character_intrinsic(Unit *unit, const F2cExpr *expression, int *supported);
char *f2c_expression_array_inquiry(Unit *unit, const F2cExpr *expression, int *supported);
int f2c_expression_array_view(Unit *unit, const F2cExpr *array, char **pointer, char **count,
                              char **stride, int *supported);
char *f2c_expression_relation_reduction(Unit *unit, const F2cExpr *expression, int *supported,
                                        int *matched);
char *f2c_expression_statement_function(Unit *unit, const F2cExpr *expression, int *supported);

#endif
