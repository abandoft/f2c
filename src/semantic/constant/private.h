#ifndef F2C_SEMANTIC_CONSTANT_PRIVATE_H
#define F2C_SEMANTIC_CONSTANT_PRIVATE_H

#include "ir/intrinsic.h"
#include "ir/type.h"

#include <stddef.h>
#include <stdint.h>

typedef struct F2cConstantEvaluation {
    Unit *unit;
    Context *context;
    size_t steps;
} F2cConstantEvaluation;

int f2c_constant_consume_step(F2cConstantEvaluation *evaluation, size_t depth);
int f2c_constant_evaluate_integer(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  int64_t *value, size_t depth);
int f2c_constant_evaluate_real(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                               double *value, size_t depth);
int f2c_constant_evaluate_numeric_integer(F2cConstantEvaluation *evaluation,
                                          const F2cExpr *expression, int64_t *value, size_t depth);
int f2c_constant_evaluate_numeric_real(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                       double *value, size_t depth);

int f2c_constant_fold_bit_intrinsic(F2cIntrinsicId intrinsic, int integer_kind,
                                    const int64_t *arguments, size_t argument_count,
                                    int64_t *result);
int f2c_constant_fold_numeric_model(F2cIntrinsicId intrinsic, Type model_type, int model_kind,
                                    const int64_t *arguments, unsigned int present,
                                    int64_t *result);

#endif
