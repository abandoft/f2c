#include "semantic/constant/private.h"

#include "internal/f2c.h"

#include <math.h>
#include <stdint.h>

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static const F2cExpr *argument(const F2cExpr *expression, const char *name, size_t position) {
    return f2c_intrinsic_argument(expression->children, expression->child_count, name, position);
}

static int integer_bounds(int kind, double *minimum, double *limit) {
    if (kind != 1 && kind != 2 && kind != 4 && kind != 8)
        return 0;
    *limit = ldexp(1.0, kind * 8 - 1);
    *minimum = -*limit;
    return 1;
}

static int integer_value_fits(int kind, int64_t value) {
    switch (kind) {
    case 1:
        return value >= INT8_MIN && value <= INT8_MAX;
    case 2:
        return value >= INT16_MIN && value <= INT16_MAX;
    case 4:
        return value >= INT32_MIN && value <= INT32_MAX;
    case 8:
        return 1;
    default:
        return 0;
    }
}

int f2c_constant_evaluate_conversion_integer(F2cConstantEvaluation *evaluation,
                                             const F2cExpr *expression, int64_t *value,
                                             size_t depth) {
    const F2cExpr *source;
    int64_t integer;
    double real;
    double minimum;
    double limit;
    if (expression == NULL || expression->rank != 0U ||
        expression->intrinsic != F2C_INTRINSIC_INT ||
        !integer_bounds(expression_kind(expression), &minimum, &limit))
        return 0;
    source = argument(expression, "a", 0U);
    if (source == NULL)
        return 0;
    if (source->type == TYPE_INTEGER) {
        if (!f2c_constant_evaluate_integer(evaluation, source, &integer, depth + 1U) ||
            !integer_value_fits(expression_kind(expression), integer))
            return 0;
        *value = integer;
        return 1;
    }
    if ((source->type != TYPE_REAL && source->type != TYPE_DOUBLE) ||
        !f2c_constant_evaluate_real(evaluation, source, &real, depth + 1U) || !isfinite(real))
        return 0;
    real = trunc(real);
    if (real < minimum || real >= limit)
        return 0;
    *value = (int64_t)real;
    return 1;
}

int f2c_constant_evaluate_conversion_real(F2cConstantEvaluation *evaluation,
                                          const F2cExpr *expression, double *value, size_t depth) {
    const F2cExpr *source;
    int64_t integer;
    double real;
    const int kind = expression_kind(expression);
    if (expression == NULL || expression->rank != 0U ||
        (expression->intrinsic != F2C_INTRINSIC_REAL &&
         expression->intrinsic != F2C_INTRINSIC_DBLE) ||
        (kind != 4 && kind != 8))
        return 0;
    source = argument(expression, "a", 0U);
    if (source == NULL)
        return 0;
    if (source->type == TYPE_INTEGER) {
        if (!f2c_constant_evaluate_integer(evaluation, source, &integer, depth + 1U))
            return 0;
        real = (double)integer;
    } else if (source->type == TYPE_REAL || source->type == TYPE_DOUBLE) {
        if (!f2c_constant_evaluate_real(evaluation, source, &real, depth + 1U))
            return 0;
    } else {
        return 0;
    }
    *value = kind == 4 ? (double)(float)real : real;
    return 1;
}
