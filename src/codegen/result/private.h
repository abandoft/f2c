#ifndef F2C_CODEGEN_RESULT_PRIVATE_H
#define F2C_CODEGEN_RESULT_PRIVATE_H

#include "codegen/array/private.h"

int f2c_result_requires_materialization(const Unit *unit, const F2cExpr *expression);
int f2c_result_materialize(Unit *unit, F2cExpr *expression, size_t identifier, const char *role,
                           size_t *temporary, Buffer *prelude, F2cArrayCleanupList *cleanup,
                           int depth);

#endif
