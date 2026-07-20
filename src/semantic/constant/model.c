#include "semantic/constant/private.h"

#include "semantic/numeric_model.h"

int f2c_constant_fold_numeric_model(F2cIntrinsicId intrinsic, Type model_type, int model_kind,
                                    const int64_t *arguments, unsigned int present,
                                    int64_t *result) {
    const F2cNumericModel *model;
    if (result == NULL)
        return 0;
    if (intrinsic == F2C_INTRINSIC_SELECTED_INT_KIND) {
        if (arguments == NULL || (present & 1U) == 0U)
            return 0;
        *result = f2c_selected_int_kind_value(arguments[0]);
        return 1;
    }
    if (intrinsic == F2C_INTRINSIC_SELECTED_REAL_KIND) {
        if (arguments == NULL || (present & 3U) == 0U)
            return 0;
        *result =
            f2c_selected_real_kind_value(arguments[0], (present & 1U) != 0U, arguments[1],
                                         (present & 2U) != 0U, arguments[2], (present & 4U) != 0U);
        return 1;
    }
    if (intrinsic == F2C_INTRINSIC_KIND) {
        if (model_kind <= 0)
            return 0;
        *result = model_kind;
        return 1;
    }
    model = f2c_numeric_model(model_type, model_kind);
    if (model == NULL)
        return 0;
    switch (intrinsic) {
    case F2C_INTRINSIC_DIGITS:
        *result = model->digits;
        return 1;
    case F2C_INTRINSIC_HUGE:
        if (model->type != TYPE_INTEGER)
            return 0;
        *result = model->integer_huge;
        return 1;
    case F2C_INTRINSIC_MAXEXPONENT:
        *result = model->max_exponent;
        return model->type == TYPE_REAL;
    case F2C_INTRINSIC_MINEXPONENT:
        *result = model->min_exponent;
        return model->type == TYPE_REAL;
    case F2C_INTRINSIC_PRECISION:
        *result = model->precision;
        return model->type == TYPE_REAL;
    case F2C_INTRINSIC_RADIX:
        *result = model->radix;
        return 1;
    case F2C_INTRINSIC_RANGE:
        *result = model->range;
        return 1;
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
}
