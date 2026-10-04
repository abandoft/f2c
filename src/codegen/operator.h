#ifndef F2C_CODEGEN_OPERATOR_H
#define F2C_CODEGEN_OPERATOR_H

#include "semantic/operator.h"

typedef struct F2cScalarOperand {
    const char *code;
    F2cScalarType type;
} F2cScalarOperand;

/* Assignment and operator consumers share kind-aware scalar conversion. */
char *f2c_emit_scalar_conversion(F2cScalarOperand operand, F2cScalarType target);

/* Inputs are scalar values of the typed operands (including elementized array
 * values). Expected is the resolved IR result, never a guessed C result type. */
char *f2c_emit_scalar_operator(F2cOperator operator_kind, int unary, F2cScalarOperand left,
                               F2cScalarOperand right, F2cScalarType expected);

#endif
