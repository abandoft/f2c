#ifndef F2C_CODEGEN_UNIT_PRIVATE_H
#define F2C_CODEGEN_UNIT_PRIVATE_H

#include "internal/f2c.h"

void f2c_unit_indent(Buffer *output, int depth);
Symbol *f2c_unit_function_result(Unit *unit);
const char *f2c_unit_function_return_type(Unit *unit);
void f2c_unit_emit_named_signature(Buffer *output, Unit *unit, const char *name,
                                   int restricted_arguments);
void f2c_unit_emit_signature(Buffer *output, Unit *unit);
void f2c_unit_emit_automatic_array_declaration(Buffer *output, Unit *unit, Symbol *symbol,
                                               int depth);
void f2c_unit_emit_automatic_array_allocation(Buffer *output, Unit *unit, Symbol *symbol,
                                              int depth);
void f2c_unit_emit_automatic_array_cleanup(Buffer *output, Unit *unit, Symbol *symbol, int depth);
char *f2c_unit_data_array_initializer(Unit *unit, const Symbol *symbol);
int f2c_unit_expression_is_character_temporary(const F2cExpr *expression);
int f2c_unit_statement_is_function_definition(const Unit *unit, size_t statement);
void f2c_unit_prepare_expression_temporaries(Unit *unit);
void f2c_unit_emit_expression_temporaries(Buffer *output, Unit *unit);
void f2c_unit_emit_equivalence_declarations(Context *context, Unit *unit);

#endif
