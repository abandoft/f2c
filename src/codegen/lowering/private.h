#ifndef F2C_CODEGEN_LOWERING_PRIVATE_H
#define F2C_CODEGEN_LOWERING_PRIVATE_H

#include "internal/context.h"

const char *f2c_lowering_code(const Unit *unit, const F2cExpr *expression);
const char *f2c_lowering_extent(const Unit *unit, const F2cExpr *expression);
const char *f2c_lowering_character_length(const Unit *unit, const F2cExpr *expression);
int f2c_lowering_is_array_temporary(const Unit *unit, const F2cExpr *expression);
int f2c_lowering_argument_materialized(const Unit *unit, const F2cExpr *expression);

int f2c_lowering_take_code(Unit *unit, const F2cExpr *expression, char *code);
int f2c_lowering_take_extent(Unit *unit, const F2cExpr *expression, char *extent);
int f2c_lowering_take_character_length(Unit *unit, const F2cExpr *expression, char *length);
int f2c_lowering_copy_code(Unit *unit, const F2cExpr *expression, const char *code);
int f2c_lowering_copy_extent(Unit *unit, const F2cExpr *expression, const char *extent);
int f2c_lowering_copy_character_length(Unit *unit, const F2cExpr *expression, const char *length);
int f2c_lowering_set_array_temporary(Unit *unit, const F2cExpr *expression, int value);
int f2c_lowering_set_argument_materialized(Unit *unit, const F2cExpr *expression, int value);
int f2c_lowering_clone(Unit *unit, const F2cExpr *target, const F2cExpr *source);

void f2c_lowering_forget(Unit *unit, const F2cExpr *expression);
void f2c_lowering_forget_tree(Unit *unit, const F2cExpr *expression);
void f2c_lowering_clear(Context *context);
void f2c_codegen_expression_free(Unit *unit, F2cExpr *expression);

#endif
