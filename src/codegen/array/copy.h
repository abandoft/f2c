#ifndef F2C_CODEGEN_ARRAY_COPY_H
#define F2C_CODEGEN_ARRAY_COPY_H

#include "internal/f2c.h"

/* Source and target are distinct storage regions with stable addresses.
 * A qualified source or destination requires typed element accesses. */
void f2c_array_copy_snapshot(Buffer *output, Unit *unit, const char *target, const char *source,
                             const char *count, const char *element_c_type,
                             unsigned int target_qualifiers, unsigned int source_qualifiers,
                             int depth);
void f2c_array_copy_to_symbol(Buffer *output, Unit *unit, Symbol *target, const char *source,
                              const char *count, int depth);

#endif
