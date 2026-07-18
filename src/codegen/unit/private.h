#ifndef F2C_CODEGEN_UNIT_PRIVATE_H
#define F2C_CODEGEN_UNIT_PRIVATE_H

#include "internal/f2c.h"

void f2c_unit_indent(Buffer *output, int depth);
Symbol *f2c_unit_function_result(Unit *unit);
const char *f2c_unit_function_return_type(Unit *unit);
void f2c_unit_emit_named_signature(Buffer *output, Unit *unit, const char *name,
                                   int restricted_arguments);
void f2c_unit_emit_signature(Buffer *output, Unit *unit);
char *f2c_unit_data_array_initializer(Unit *unit, const Symbol *symbol);

#endif
