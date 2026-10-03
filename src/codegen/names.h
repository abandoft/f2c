#ifndef F2C_CODEGEN_NAMES_H
#define F2C_CODEGEN_NAMES_H

#include "internal/f2c.h"

/* Owned identifier for a scoped implementation local. */
char *f2c_codegen_local_name(Unit *unit, const char *base);
/* Reserve a preferred prefix together with every generated local suffix. */
char *f2c_codegen_local_family(Unit *unit, const char *preferred, const char *const *suffixes,
                               size_t count);

#endif
