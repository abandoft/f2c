#ifndef F2C_SEMANTIC_OPERATOR_H
#define F2C_SEMANTIC_OPERATOR_H

#include "frontend/operator.h"
#include "ir/type.h"

typedef enum F2cOperatorStatus {
    F2C_OPERATOR_VALID,
    F2C_OPERATOR_DEFERRED,
    F2C_OPERATOR_INVALID_OPERANDS,
    F2C_OPERATOR_INCOMPATIBLE_KINDS
} F2cOperatorStatus;

typedef struct F2cOperatorTyping {
    F2cScalarType result;
    F2cScalarType left;
    F2cScalarType right;
} F2cOperatorTyping;

F2cScalarType f2c_scalar_type(Type type, int kind);
F2cScalarType f2c_expression_scalar_type(const F2cExpr *expression);
F2cOperatorStatus f2c_operator_typing(F2cOperator operator_kind, int unary, F2cScalarType left,
                                      F2cScalarType right, F2cOperatorTyping *typing);
F2cOperatorStatus f2c_expression_refresh_operator_type(F2cExpr *expression);

#endif
