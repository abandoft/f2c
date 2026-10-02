#ifndef F2C_SEMANTIC_SCOPE_H
#define F2C_SEMANTIC_SCOPE_H

#include "semantic/model.h"

/* Resolve a stable lexical scope identity without retaining pointers into movable Unit tables. */
Unit *f2c_symbol_declaration_scope(Unit *current, const Symbol *symbol);
Unit *f2c_symbol_specification_scope(Unit *current, const Symbol *symbol);

#endif
