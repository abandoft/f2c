#ifndef F2C_SEMANTIC_CONSTANT_ARRAY_H
#define F2C_SEMANTIC_CONSTANT_ARRAY_H

#include "ir/constant.h"

/* Results own their elements, including character/derived payloads. Failure
 * leaves an empty result; no partially evaluated initializer may escape. */
int f2c_evaluate_constant_array(Unit *unit, const F2cExpr *expression, F2cConstantArray *result);
int f2c_evaluate_constant_storage(Unit *unit, const Symbol *symbol, F2cConstantArray *result);
int f2c_evaluate_constant_storage_layout(Unit *unit, const Symbol *symbol, F2cShape *shape,
                                         size_t *character_length);

#endif
