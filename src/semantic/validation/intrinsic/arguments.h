#ifndef F2C_SEMANTIC_VALIDATION_INTRINSIC_ARGUMENTS_H
#define F2C_SEMANTIC_VALIDATION_INTRINSIC_ARGUMENTS_H

#include "internal/f2c.h"

typedef struct F2cBoundIntrinsicArguments {
    const F2cExpr *values[F2C_INTRINSIC_ARGUMENT_LIMIT];
} F2cBoundIntrinsicArguments;

F2cBoundIntrinsicArguments f2c_validation_bind_registered_intrinsic_arguments(
    Context *context, size_t line, const char *statement_text, const char *intrinsic_name,
    F2cIntrinsicId intrinsic, size_t maximum_arguments, F2cExpr *const *arguments,
    size_t argument_count);

F2cBoundIntrinsicArguments f2c_validation_bind_intrinsic_expression(
    Context *context, size_t line, const char *statement_text, F2cExpr *expression);

F2cBoundIntrinsicArguments f2c_validation_bind_intrinsic_statement(
    Context *context, F2cStatement *statement);

#endif
