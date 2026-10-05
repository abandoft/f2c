#ifndef F2C_SEMANTIC_CONSTANT_RELATION_H
#define F2C_SEMANTIC_CONSTANT_RELATION_H

#include "frontend/operator.h"
#include "ir/constant.h"

/* Compare model values using the same operand typing as runtime operators.
 * Inputs remain borrowed and unchanged, including owned character payloads. */
int f2c_constant_value_relation(F2cOperator operator_kind, const F2cConstantValue *left,
                                 const F2cConstantValue *right, int64_t *result);

#endif
