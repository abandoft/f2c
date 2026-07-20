#ifndef F2C_SEMANTIC_SEMANTIC_H
#define F2C_SEMANTIC_SEMANTIC_H

#include "internal/context.h"
#include "semantic/intrinsic.h"

void f2c_validate_unit_expressions(Context *context, Unit *unit);
void f2c_resolve_derived_semantics(Context *context);
int f2c_symbol_resize_external_parameters(Symbol *symbol, size_t count);
int f2c_symbol_uses_descriptor(const Symbol *symbol);
int f2c_evaluate_integer_constant(Unit *unit, const F2cExpr *expression, int64_t *value);
int f2c_evaluate_real_constant(Unit *unit, const F2cExpr *expression, double *value);
int f2c_evaluate_integer_syntax(Unit *unit, F2cTokenRange syntax, int64_t *value);
int f2c_expression_is_initialization_constant(const F2cExpr *expression);
int f2c_integer_iteration_count(int64_t first, int64_t last, int64_t step, uint64_t *count);
size_t f2c_character_literal_length(const char *text);
char *f2c_character_literal_bytes(const char *text, size_t *length);
int f2c_evaluate_character_constant(Unit *unit, const F2cExpr *expression, char **value,
                                    size_t *length);
char *f2c_character_length_expression(Unit *unit, const F2cExpr *expression);
char *f2c_symbol_character_length(Unit *unit, const Symbol *symbol);
char *f2c_character_declaration_initializer(Unit *unit, const Symbol *symbol, int *supported);
char *f2c_character_source_pointer(Unit *unit, const F2cExpr *expression,
                                   const char *expression_code);

#endif
