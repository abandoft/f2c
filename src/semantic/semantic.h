#ifndef F2C_SEMANTIC_SEMANTIC_H
#define F2C_SEMANTIC_SEMANTIC_H

#include "internal/context.h"
#include "semantic/intrinsic.h"

void f2c_validate_unit_expressions(Context *context, Unit *unit);
void f2c_resolve_derived_semantics(Context *context);
int f2c_symbol_resize_external_parameters(Symbol *symbol, size_t count);
int f2c_evaluate_integer_constant(Unit *unit, const F2cExpr *expression, int64_t *value);
int f2c_evaluate_integer_text(Unit *unit, const char *text, int64_t *value);
char *f2c_character_length_expression(Unit *unit, const F2cExpr *expression);
char *f2c_symbol_character_length(Unit *unit, const Symbol *symbol);
char *f2c_character_declaration_initializer(Unit *unit, const Symbol *symbol, int *supported);
char *f2c_character_source_pointer(Unit *unit, const F2cExpr *expression,
                                   const char *expression_code);

#endif
