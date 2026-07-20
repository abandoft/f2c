#ifndef F2C_SEMANTIC_NUMERIC_MODEL_H
#define F2C_SEMANTIC_NUMERIC_MODEL_H

#include "ir/type.h"

#include <stdint.h>

typedef struct F2cNumericModel {
    Type type;
    int kind;
    int radix;
    int digits;
    int precision;
    int range;
    int min_exponent;
    int max_exponent;
    int64_t integer_huge;
} F2cNumericModel;

const F2cNumericModel *f2c_numeric_model(Type type, int kind);
int f2c_selected_int_kind_value(int64_t range);
int f2c_selected_real_kind_value(int64_t precision, int has_precision, int64_t range,
                                 int has_range, int64_t radix, int has_radix);

#endif
