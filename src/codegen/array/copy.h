#ifndef F2C_CODEGEN_ARRAY_COPY_H
#define F2C_CODEGEN_ARRAY_COPY_H

#include "internal/f2c.h"

/* Source is a distinct, completely evaluated value buffer. */
void f2c_array_copy_snapshot(Buffer *output, Unit *unit, const char *target, const char *source,
                             const char *count, unsigned int target_qualifiers, int depth);

#endif
