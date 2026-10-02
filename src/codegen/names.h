#ifndef F2C_CODEGEN_NAMES_H
#define F2C_CODEGEN_NAMES_H

#include "internal/f2c.h"

/* Owned identifier for a scoped implementation local. */
char *f2c_codegen_local_name(Unit *unit, const char *base);

#endif
