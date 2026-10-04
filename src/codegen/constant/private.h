#ifndef F2C_CODEGEN_CONSTANT_PRIVATE_H
#define F2C_CODEGEN_CONSTANT_PRIVATE_H

#include "ir/constant.h"

char *f2c_emit_constant_value(Unit *unit, const F2cConstantValue *value);
char *f2c_constant_storage_initializer(Unit *unit, const Symbol *symbol);

#endif
