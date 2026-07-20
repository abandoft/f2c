#include "codegen/expression/private.h"

#include "semantic/numeric_model.h"

#include <stdlib.h>

static char *integer_constant(int64_t value) {
    Buffer result = {0};
    if (value < 0)
        f2c_buffer_printf(&result, "-INT32_C(%lld)", (long long)-value);
    else
        f2c_buffer_printf(&result, "INT32_C(%lld)", (long long)value);
    return f2c_buffer_take(&result);
}

static const char *real_model_constant(F2cIntrinsicId intrinsic, int kind) {
    if (kind == 4) {
        if (intrinsic == F2C_INTRINSIC_EPSILON)
            return "FLT_EPSILON";
        if (intrinsic == F2C_INTRINSIC_HUGE)
            return "FLT_MAX";
        if (intrinsic == F2C_INTRINSIC_TINY)
            return "FLT_MIN";
    } else if (kind == 8) {
        if (intrinsic == F2C_INTRINSIC_EPSILON)
            return "DBL_EPSILON";
        if (intrinsic == F2C_INTRINSIC_HUGE)
            return "DBL_MAX";
        if (intrinsic == F2C_INTRINSIC_TINY)
            return "DBL_MIN";
    }
    return NULL;
}

static const char *integer_huge_constant(int kind) {
    switch (kind) {
    case 1:
        return "INT8_MAX";
    case 2:
        return "INT16_MAX";
    case 4:
        return "INT32_MAX";
    case 8:
        return "INT64_MAX";
    default:
        return NULL;
    }
}

static char *model_inquiry(const F2cExpr *expression, int *supported) {
    const F2cExpr *argument =
        f2c_intrinsic_argument(expression->children, expression->child_count, "x", 0U);
    const F2cNumericModel *model;
    const char *real_constant;
    const char *huge_constant;
    int kind;
    int64_t value;
    if (argument == NULL) {
        *supported = 0;
        return NULL;
    }
    kind = argument->type_kind != 0 ? argument->type_kind : f2c_default_kind(argument->type);
    if (expression->intrinsic == F2C_INTRINSIC_KIND)
        return integer_constant(kind);
    model = f2c_numeric_model(argument->type, kind);
    if (model == NULL) {
        *supported = 0;
        return NULL;
    }
    real_constant =
        model->type == TYPE_REAL ? real_model_constant(expression->intrinsic, kind) : NULL;
    if (real_constant != NULL)
        return f2c_strdup(real_constant);
    if (expression->intrinsic == F2C_INTRINSIC_HUGE && model->type == TYPE_INTEGER) {
        huge_constant = integer_huge_constant(kind);
        if (huge_constant != NULL)
            return f2c_strdup(huge_constant);
    }
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_DIGITS:
        value = model->digits;
        break;
    case F2C_INTRINSIC_MAXEXPONENT:
        value = model->max_exponent;
        break;
    case F2C_INTRINSIC_MINEXPONENT:
        value = model->min_exponent;
        break;
    case F2C_INTRINSIC_PRECISION:
        value = model->precision;
        break;
    case F2C_INTRINSIC_RADIX:
        value = model->radix;
        break;
    case F2C_INTRINSIC_RANGE:
        value = model->range;
        break;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_EPSILON:
    case F2C_INTRINSIC_HUGE:
    case F2C_INTRINSIC_KIND:
    case F2C_INTRINSIC_SELECTED_INT_KIND:
    case F2C_INTRINSIC_SELECTED_REAL_KIND:
    case F2C_INTRINSIC_TINY:
    default:
        *supported = 0;
        return NULL;
    }
    return integer_constant(value);
}

static char *selected_int_kind(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *range =
        f2c_intrinsic_argument(expression->children, expression->child_count, "r", 0U);
    char *range_code;
    Buffer result = {0};
    if (range == NULL) {
        *supported = 0;
        return NULL;
    }
    range_code = f2c_expression_emit(unit, range, supported);
    if (!*supported || range_code == NULL) {
        free(range_code);
        return NULL;
    }
    f2c_buffer_printf(&result, "f2c_selected_int_kind((int64_t)(%s))", range_code);
    free(range_code);
    return f2c_buffer_take(&result);
}

static char *selected_real_kind(Unit *unit, const F2cExpr *expression, int *supported) {
    static const char *const keywords[] = {"p", "r", "radix"};
    char *arguments[3] = {NULL, NULL, NULL};
    int present[3] = {0, 0, 0};
    Buffer result = {0};
    size_t index;
    for (index = 0U; index < 3U; ++index) {
        const F2cExpr *argument = f2c_intrinsic_argument(
            expression->children, expression->child_count, keywords[index], index);
        if (argument == NULL)
            continue;
        present[index] = 1;
        arguments[index] = f2c_expression_emit(unit, argument, supported);
        if (!*supported || arguments[index] == NULL)
            goto failure;
    }
    f2c_buffer_printf(
        &result, "f2c_selected_real_kind((int64_t)(%s), %s, (int64_t)(%s), %s, (int64_t)(%s), %s)",
        arguments[0] != NULL ? arguments[0] : "0", present[0] ? "true" : "false",
        arguments[1] != NULL ? arguments[1] : "0", present[1] ? "true" : "false",
        arguments[2] != NULL ? arguments[2] : "0", present[2] ? "true" : "false");
    for (index = 0U; index < 3U; ++index)
        free(arguments[index]);
    return f2c_buffer_take(&result);

failure:
    for (index = 0U; index < 3U; ++index)
        free(arguments[index]);
    free(result.data);
    return NULL;
}

char *f2c_expression_numeric_model_intrinsic(Unit *unit, const F2cExpr *expression,
                                             int *supported) {
    int64_t constant;
    if (expression == NULL || !f2c_intrinsic_is_numeric_model(expression->intrinsic)) {
        *supported = 0;
        return NULL;
    }
    if ((expression->intrinsic == F2C_INTRINSIC_SELECTED_INT_KIND ||
         expression->intrinsic == F2C_INTRINSIC_SELECTED_REAL_KIND) &&
        f2c_evaluate_integer_constant(unit, expression, &constant))
        return integer_constant(constant);
    if (expression->intrinsic == F2C_INTRINSIC_SELECTED_INT_KIND)
        return selected_int_kind(unit, expression, supported);
    if (expression->intrinsic == F2C_INTRINSIC_SELECTED_REAL_KIND)
        return selected_real_kind(unit, expression, supported);
    return model_inquiry(expression, supported);
}
