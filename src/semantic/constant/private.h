#ifndef F2C_SEMANTIC_CONSTANT_PRIVATE_H
#define F2C_SEMANTIC_CONSTANT_PRIVATE_H

#include "ir/constant.h"
#include "ir/intrinsic.h"
#include "ir/type.h"

#include <stddef.h>
#include <stdint.h>

typedef struct F2cConstantEvaluation {
    Unit *unit;
    Context *context;
    size_t steps;
    struct F2cConstantArrayCache *arrays;
} F2cConstantEvaluation;

typedef struct F2cComplexConstant {
    double real;
    double imaginary;
} F2cComplexConstant;

void f2c_constant_evaluation_finish(F2cConstantEvaluation *evaluation);
int f2c_constant_evaluate_array(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                F2cConstantArray *result, size_t depth);
int f2c_constant_evaluate_storage(F2cConstantEvaluation *evaluation, const Symbol *symbol,
                                  F2cConstantArray *result, size_t depth);
int f2c_constant_storage_shape(F2cConstantEvaluation *evaluation, const Symbol *symbol,
                               F2cShape *shape, size_t *character_length, size_t depth);
const F2cConstantValue *f2c_constant_array_element(F2cConstantEvaluation *evaluation,
                                                   const F2cExpr *expression, size_t depth);
int f2c_constant_evaluate_value(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                F2cConstantValue *result, size_t depth);
int f2c_constant_value_convert(F2cConstantValue *value, F2cScalarType type,
                               const F2cDerivedType *derived, size_t character_length);
int f2c_constant_array_allocate(F2cConstantEvaluation *evaluation, F2cConstantArray *array,
                                size_t count);
int f2c_constant_transform_supported(F2cIntrinsicId intrinsic);
int f2c_constant_evaluate_transform(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                    F2cConstantArray *result, size_t depth);

int f2c_constant_consume_step(F2cConstantEvaluation *evaluation, size_t depth);
int f2c_constant_evaluate_integer(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  int64_t *value, size_t depth);
int f2c_constant_evaluate_real(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                               double *value, size_t depth);
int f2c_constant_evaluate_complex(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  F2cComplexConstant *value, size_t depth);
int f2c_constant_evaluate_character(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                    char **value, size_t *length, size_t depth);
int f2c_constant_evaluate_logical_operator(F2cConstantEvaluation *evaluation,
                                           const F2cExpr *expression, int64_t *value, size_t depth);
int f2c_constant_evaluate_numeric_integer(F2cConstantEvaluation *evaluation,
                                          const F2cExpr *expression, int64_t *value, size_t depth);
int f2c_constant_evaluate_numeric_real(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                       double *value, size_t depth);
int f2c_constant_evaluate_conversion_integer(F2cConstantEvaluation *evaluation,
                                             const F2cExpr *expression, int64_t *value,
                                             size_t depth);
int f2c_constant_evaluate_conversion_real(F2cConstantEvaluation *evaluation,
                                          const F2cExpr *expression, double *value, size_t depth);
int f2c_constant_evaluate_mathematical_integer(F2cConstantEvaluation *evaluation,
                                               const F2cExpr *expression, int64_t *value,
                                               size_t depth);
int f2c_constant_evaluate_mathematical_real(F2cConstantEvaluation *evaluation,
                                            const F2cExpr *expression, double *value, size_t depth);

int f2c_constant_fold_bit_intrinsic(F2cIntrinsicId intrinsic, int integer_kind,
                                    const int64_t *arguments, size_t argument_count,
                                    int64_t *result);
int f2c_constant_fold_numeric_model(F2cIntrinsicId intrinsic, Type model_type, int model_kind,
                                    const int64_t *arguments, unsigned int present,
                                    int64_t *result);

#endif
