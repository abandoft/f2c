#ifndef F2C_CODEGEN_CHARACTER_PRIVATE_H
#define F2C_CODEGEN_CHARACTER_PRIVATE_H

#include "internal/f2c.h"

int f2c_emit_deferred_character_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                           const F2cExpr *right, const char *right_code, int depth);

#endif
