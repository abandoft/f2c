#ifndef F2C_SEMANTIC_VALIDATION_INTRINSIC_ARGUMENTS_H
#define F2C_SEMANTIC_VALIDATION_INTRINSIC_ARGUMENTS_H

#include "internal/f2c.h"

#define F2C_INTRINSIC_ARGUMENT_LIMIT 5U

typedef struct F2cBoundIntrinsicArguments {
    const F2cExpr *values[F2C_INTRINSIC_ARGUMENT_LIMIT];
} F2cBoundIntrinsicArguments;

F2cBoundIntrinsicArguments f2c_validation_bind_intrinsic_arguments(
    Context *context, size_t line, const char *statement_text, const char *intrinsic_name,
    F2cExpr *const *arguments, size_t argument_count, const char *const *names,
    size_t name_count, size_t required_count);

#endif
