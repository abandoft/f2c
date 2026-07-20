#ifndef F2C_CODEGEN_VALUE_PRIVATE_H
#define F2C_CODEGEN_VALUE_PRIVATE_H

#include "internal/f2c.h"

int f2c_emit_derived_clone_expression(Buffer *output, Unit *unit, const F2cExpr *source,
                                      const char *destination, const char *scope, size_t identifier,
                                      int depth);

#endif
