#ifndef F2C_CODEGEN_ARRAY_PRIVATE_H
#define F2C_CODEGEN_ARRAY_PRIVATE_H

#include "ast/internal.h"

void f2c_array_indent(Buffer *output, int depth);
char *f2c_array_emit_expression(Unit *unit, const F2cExpr *expression);
int f2c_array_emit_numeric_constructor(Context *context, Unit *unit, Symbol *left_symbol,
                                       const F2cExpr *constructor, const char *element_count,
                                       int depth);
int f2c_array_emit_allocatable_numeric_constructor(Context *context, Unit *unit, Symbol *target,
                                                   const F2cExpr *constructor, int depth);
int f2c_array_emit_allocatable_character_constructor(Context *context, Unit *unit, Symbol *target,
                                                     const F2cExpr *constructor, int depth);
int f2c_array_emit_whole_character_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                              const F2cExpr *right, Symbol *right_symbol,
                                              const char *element_count, int depth);

#endif
