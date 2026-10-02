#ifndef F2C_CODEGEN_TYPE_INITIALIZATION_H
#define F2C_CODEGEN_TYPE_INITIALIZATION_H

#include "internal/context.h"

/* Brace initializers are valid both for C17 static storage and fresh automatic
 * objects. They never require a mutable first-entry flag or a startup runtime. */
char *f2c_derived_storage_initializer(Unit *unit, const F2cDerivedType *derived);
char *f2c_derived_entity_initializer(Unit *unit, const Symbol *symbol);
char *f2c_derived_constructor_initializer(Unit *unit, const F2cExpr *constructor);

#endif
