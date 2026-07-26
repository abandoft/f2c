#include "semantic/constant/private.h"

#include "internal/f2c.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static const F2cExpr *argument(const F2cExpr *expression, const char *name, size_t position) {
    return f2c_intrinsic_argument(expression->children, expression->child_count, name, position);
}

static int store_real(int kind, double candidate, double *value) {
    if (kind == 4) {
        *value = (double)(float)candidate;
        return 1;
    }
    if (kind == 8) {
        *value = candidate;
        return 1;
    }
    return 0;
}

static const F2cExpr *extremum_argument(const F2cExpr *expression, size_t index) {
    char name[24];
    const int length = snprintf(name, sizeof(name), "a%zu", index + 1U);
    return length > 0 && (size_t)length < sizeof(name) ? argument(expression, name, index) : NULL;
}

int f2c_constant_evaluate_mathematical_integer(F2cConstantEvaluation *evaluation,
                                               const F2cExpr *expression, int64_t *value,
                                               size_t depth) {
    const F2cExpr *source;
    int64_t current;
    size_t index;
    if (expression == NULL || expression->rank != 0U || expression->type != TYPE_INTEGER ||
        !f2c_intrinsic_is_mathematical(expression->intrinsic))
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_ABS) {
        source = argument(expression, "a", 0U);
        if (source == NULL ||
            !f2c_constant_evaluate_integer(evaluation, source, &current, depth + 1U) ||
            current == INT64_MIN)
            return 0;
        *value = current < 0 ? -current : current;
        return 1;
    }
    if (expression->intrinsic != F2C_INTRINSIC_MAX && expression->intrinsic != F2C_INTRINSIC_MIN)
        return 0;
    source = extremum_argument(expression, 0U);
    if (source == NULL || !f2c_constant_evaluate_integer(evaluation, source, value, depth + 1U))
        return 0;
    for (index = 1U; index < expression->child_count; ++index) {
        source = extremum_argument(expression, index);
        if (source == NULL ||
            !f2c_constant_evaluate_integer(evaluation, source, &current, depth + 1U))
            return 0;
        if ((expression->intrinsic == F2C_INTRINSIC_MAX && current > *value) ||
            (expression->intrinsic == F2C_INTRINSIC_MIN && current < *value))
            *value = current;
    }
    return 1;
}

static int evaluate_unary(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                          double *value, size_t depth) {
    const char *name = expression->intrinsic == F2C_INTRINSIC_ABS ? "a" : "x";
    const F2cExpr *source = argument(expression, name, 0U);
    double input;
    double result;
    const int kind = source != NULL ? expression_kind(source) : 0;
    if (source == NULL || (source->type != TYPE_REAL && source->type != TYPE_DOUBLE) ||
        !f2c_constant_evaluate_real(evaluation, source, &input, depth + 1U))
        return 0;
    if (kind == 4) {
        const float x = (float)input;
        float computed;
        switch (expression->intrinsic) {
        case F2C_INTRINSIC_ABS:
            computed = fabsf(x);
            break;
        case F2C_INTRINSIC_ACOS:
            if (fabsf(x) > 1.0f)
                return 0;
            computed = acosf(x);
            break;
        case F2C_INTRINSIC_ASIN:
            if (fabsf(x) > 1.0f)
                return 0;
            computed = asinf(x);
            break;
        case F2C_INTRINSIC_ATAN:
            computed = atanf(x);
            break;
        case F2C_INTRINSIC_COS:
            computed = cosf(x);
            break;
        case F2C_INTRINSIC_COSH:
            computed = coshf(x);
            break;
        case F2C_INTRINSIC_EXP:
            computed = expf(x);
            break;
        case F2C_INTRINSIC_LOG:
            if (x <= 0.0f)
                return 0;
            computed = logf(x);
            break;
        case F2C_INTRINSIC_LOG10:
            if (x <= 0.0f)
                return 0;
            computed = log10f(x);
            break;
        case F2C_INTRINSIC_SIN:
            computed = sinf(x);
            break;
        case F2C_INTRINSIC_SINH:
            computed = sinhf(x);
            break;
        case F2C_INTRINSIC_SQRT:
            if (x < 0.0f)
                return 0;
            computed = sqrtf(x);
            break;
        case F2C_INTRINSIC_TAN:
            computed = tanf(x);
            break;
        case F2C_INTRINSIC_TANH:
            computed = tanhf(x);
            break;
        case F2C_INTRINSIC_NONE:
        default:
            return 0;
        }
        result = (double)computed;
    } else if (kind == 8) {
        switch (expression->intrinsic) {
        case F2C_INTRINSIC_ABS:
            result = fabs(input);
            break;
        case F2C_INTRINSIC_ACOS:
            if (fabs(input) > 1.0)
                return 0;
            result = acos(input);
            break;
        case F2C_INTRINSIC_ASIN:
            if (fabs(input) > 1.0)
                return 0;
            result = asin(input);
            break;
        case F2C_INTRINSIC_ATAN:
            result = atan(input);
            break;
        case F2C_INTRINSIC_COS:
            result = cos(input);
            break;
        case F2C_INTRINSIC_COSH:
            result = cosh(input);
            break;
        case F2C_INTRINSIC_EXP:
            result = exp(input);
            break;
        case F2C_INTRINSIC_LOG:
            if (input <= 0.0)
                return 0;
            result = log(input);
            break;
        case F2C_INTRINSIC_LOG10:
            if (input <= 0.0)
                return 0;
            result = log10(input);
            break;
        case F2C_INTRINSIC_SIN:
            result = sin(input);
            break;
        case F2C_INTRINSIC_SINH:
            result = sinh(input);
            break;
        case F2C_INTRINSIC_SQRT:
            if (input < 0.0)
                return 0;
            result = sqrt(input);
            break;
        case F2C_INTRINSIC_TAN:
            result = tan(input);
            break;
        case F2C_INTRINSIC_TANH:
            result = tanh(input);
            break;
        case F2C_INTRINSIC_NONE:
        default:
            return 0;
        }
    } else {
        return 0;
    }
    return store_real(expression_kind(expression), result, value);
}

static int evaluate_atan2(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                          double *value, size_t depth) {
    const F2cExpr *y = argument(expression, "y", 0U);
    const F2cExpr *x = argument(expression, "x", 1U);
    double y_value;
    double x_value;
    const int kind = y != NULL ? expression_kind(y) : 0;
    if (y == NULL || x == NULL ||
        !f2c_constant_evaluate_real(evaluation, y, &y_value, depth + 1U) ||
        !f2c_constant_evaluate_real(evaluation, x, &x_value, depth + 1U))
        return 0;
    return store_real(expression_kind(expression),
                      kind == 4 ? (double)atan2f((float)y_value, (float)x_value)
                                : atan2(y_value, x_value),
                      value);
}

static int evaluate_dprod(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                          double *value, size_t depth) {
    const F2cExpr *x = argument(expression, "x", 0U);
    const F2cExpr *y = argument(expression, "y", 1U);
    double x_value;
    double y_value;
    if (x == NULL || y == NULL ||
        !f2c_constant_evaluate_real(evaluation, x, &x_value, depth + 1U) ||
        !f2c_constant_evaluate_real(evaluation, y, &y_value, depth + 1U))
        return 0;
    *value = (double)(float)x_value * (double)(float)y_value;
    return 1;
}

static int evaluate_real_extremum(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  double *value, size_t depth) {
    const F2cExpr *source = extremum_argument(expression, 0U);
    double current;
    size_t index;
    if (source == NULL || !f2c_constant_evaluate_real(evaluation, source, value, depth + 1U) ||
        isnan(*value))
        return 0;
    for (index = 1U; index < expression->child_count; ++index) {
        source = extremum_argument(expression, index);
        if (source == NULL ||
            !f2c_constant_evaluate_real(evaluation, source, &current, depth + 1U) || isnan(current))
            return 0;
        if ((expression->intrinsic == F2C_INTRINSIC_MAX && current > *value) ||
            (expression->intrinsic == F2C_INTRINSIC_MIN && current < *value))
            *value = current;
    }
    return store_real(expression_kind(expression), *value, value);
}

int f2c_constant_evaluate_mathematical_real(F2cConstantEvaluation *evaluation,
                                            const F2cExpr *expression, double *value,
                                            size_t depth) {
    if (expression == NULL || expression->rank != 0U ||
        (expression->type != TYPE_REAL && expression->type != TYPE_DOUBLE) ||
        !f2c_intrinsic_is_mathematical(expression->intrinsic))
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_ATAN2)
        return evaluate_atan2(evaluation, expression, value, depth);
    if (expression->intrinsic == F2C_INTRINSIC_DPROD)
        return evaluate_dprod(evaluation, expression, value, depth);
    if (expression->intrinsic == F2C_INTRINSIC_MAX || expression->intrinsic == F2C_INTRINSIC_MIN)
        return evaluate_real_extremum(evaluation, expression, value, depth);
    return evaluate_unary(evaluation, expression, value, depth);
}
