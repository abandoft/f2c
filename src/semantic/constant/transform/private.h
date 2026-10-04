#ifndef F2C_SEMANTIC_CONSTANT_TRANSFORM_PRIVATE_H
#define F2C_SEMANTIC_CONSTANT_TRANSFORM_PRIVATE_H

#include "semantic/constant/private.h"

const F2cExpr *f2c_constant_argument(const F2cExpr *call, const char *name, size_t position);
int f2c_constant_integer_argument(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  int64_t *value, size_t depth);
int f2c_constant_same_element(const F2cConstantArray *left, const F2cConstantArray *right);
int f2c_constant_conformable(const F2cShape *left, const F2cShape *right);
void f2c_constant_result_model(F2cConstantArray *result, const F2cConstantArray *source);
void f2c_constant_shape_dimension(F2cShape *shape, size_t dimension, uint64_t extent);
int f2c_constant_transform_reorder(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                   F2cConstantArray *result, size_t depth);
int f2c_constant_transform_pack(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                F2cConstantArray *result, size_t depth);
int f2c_constant_transform_shift(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                 F2cConstantArray *result, size_t depth);
int f2c_constant_transform_findloc(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                   F2cConstantArray *result, size_t depth);

#endif
