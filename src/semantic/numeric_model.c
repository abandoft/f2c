#include "semantic/numeric_model.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static const F2cNumericModel models[] = {
    {TYPE_INTEGER, 1, 2, 7, 0, 2, 0, 0, INT8_MAX},
    {TYPE_INTEGER, 2, 2, 15, 0, 4, 0, 0, INT16_MAX},
    {TYPE_INTEGER, 4, 2, 31, 0, 9, 0, 0, INT32_MAX},
    {TYPE_INTEGER, 8, 2, 63, 0, 18, 0, 0, INT64_MAX},
    {TYPE_REAL, 4, 2, 24, 6, 37, -125, 128, 0},
    {TYPE_REAL, 8, 2, 53, 15, 307, -1021, 1024, 0},
};

static Type model_type(Type type) {
    switch (type) {
    case TYPE_REAL:
    case TYPE_DOUBLE:
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        return TYPE_REAL;
    case TYPE_INTEGER:
        return TYPE_INTEGER;
    case TYPE_UNKNOWN:
    case TYPE_LOGICAL:
    case TYPE_CHARACTER:
    case TYPE_DERIVED:
    default:
        return TYPE_UNKNOWN;
    }
}

const F2cNumericModel *f2c_numeric_model(Type type, int kind) {
    const Type category = model_type(type);
    size_t index;
    if (category == TYPE_UNKNOWN)
        return NULL;
    for (index = 0U; index < sizeof(models) / sizeof(models[0]); ++index)
        if (models[index].type == category && models[index].kind == kind)
            return &models[index];
    return NULL;
}

int f2c_selected_int_kind_value(int64_t range) {
    size_t index;
    for (index = 0U; index < sizeof(models) / sizeof(models[0]); ++index)
        if (models[index].type == TYPE_INTEGER && range <= models[index].range)
            return models[index].kind;
    return -1;
}

int f2c_selected_real_kind_value(int64_t precision, int has_precision, int64_t range,
                                 int has_range, int64_t radix, int has_radix) {
    int precision_supported = 0;
    int range_supported = 0;
    int radix_supported = !has_radix;
    size_t index;
    for (index = 0U; index < sizeof(models) / sizeof(models[0]); ++index) {
        const F2cNumericModel *model = &models[index];
        const int matches_radix = !has_radix || radix == model->radix;
        if (model->type != TYPE_REAL)
            continue;
        if (matches_radix)
            radix_supported = 1;
        if (matches_radix && (!has_precision || precision <= model->precision))
            precision_supported = 1;
        if (matches_radix && (!has_range || range <= model->range))
            range_supported = 1;
        if (matches_radix && (!has_precision || precision <= model->precision) &&
            (!has_range || range <= model->range))
            return model->kind;
    }
    if (!radix_supported)
        return -5;
    if (!precision_supported && !range_supported)
        return -3;
    if (!precision_supported)
        return -1;
    if (!range_supported)
        return -2;
    return -4;
}
