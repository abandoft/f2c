#ifndef F2C_SEMANTIC_IMPLICIT_PRIVATE_H
#define F2C_SEMANTIC_IMPLICIT_PRIVATE_H

#include "internal/context.h"

void f2c_parse_implicit_statement(Context *context, Unit *unit, const Line *line);
void f2c_discover_implicit_line_symbols(Context *context, Unit *unit, const Line *line);

#endif
