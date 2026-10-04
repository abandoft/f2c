#ifndef F2C_IR_CONSTANT_H
#define F2C_IR_CONSTANT_H

#include "ir/expression.h"

/* Owned, processor-model values, never C source fragments. Array elements are
 * stored in Fortran column-major order; bounds belong to the array shape. */
typedef struct F2cConstantValue {
    F2cScalarType type;
    union {
        int64_t integer;
        struct {
            double real, imaginary;
        } number;
        struct {
            char *bytes;
            size_t length;
        } character;
        F2cExpr *derived;
    } payload;
} F2cConstantValue;

typedef struct F2cConstantArray {
    F2cScalarType type;
    F2cDerivedType *derived_type;
    size_t character_length;
    F2cShape shape;
    F2cConstantValue *values;
    size_t count;
    size_t capacity;
} F2cConstantArray;

void f2c_constant_value_free(F2cConstantValue *value);
int f2c_constant_value_copy(F2cConstantValue *target, const F2cConstantValue *source);
void f2c_constant_array_free(F2cConstantArray *array);
int f2c_constant_array_copy(F2cConstantArray *target, const F2cConstantArray *source);
int f2c_constant_array_reserve(F2cConstantArray *array, size_t count);
int f2c_constant_shape_count(const F2cShape *shape, size_t *count);

#endif
