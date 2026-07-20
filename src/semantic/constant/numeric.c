#include "semantic/constant/private.h"

#include "internal/f2c.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static int integer_bounds(int kind, int64_t *minimum, int64_t *maximum) {
    if (minimum == NULL || maximum == NULL)
        return 0;
    switch (kind) {
    case 1:
        *minimum = INT8_MIN;
        *maximum = INT8_MAX;
        return 1;
    case 2:
        *minimum = INT16_MIN;
        *maximum = INT16_MAX;
        return 1;
    case 4:
        *minimum = INT32_MIN;
        *maximum = INT32_MAX;
        return 1;
    case 8:
        *minimum = INT64_MIN;
        *maximum = INT64_MAX;
        return 1;
    default:
        return 0;
    }
}

static int store_integer(int kind, int64_t candidate, int64_t *value) {
    int64_t minimum;
    int64_t maximum;
    if (value == NULL || !integer_bounds(kind, &minimum, &maximum) || candidate < minimum ||
        candidate > maximum)
        return 0;
    *value = candidate;
    return 1;
}

static int store_real(int kind, double candidate, double *value) {
    if (value == NULL)
        return 0;
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

static const F2cExpr *argument(const F2cExpr *expression, const char *name, size_t position) {
    return expression != NULL ? f2c_intrinsic_argument(expression->children,
                                                       expression->child_count, name, position)
                              : NULL;
}

static int evaluate_mask(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                         int64_t *mask, size_t depth) {
    const F2cExpr *condition = argument(expression, "mask", 2U);
    return condition != NULL && condition->type == TYPE_LOGICAL && condition->rank == 0U &&
           f2c_constant_evaluate_integer(evaluation, condition, mask, depth + 1U);
}

static int round_to_integer(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                            int64_t *value, size_t depth) {
    const F2cExpr *source = argument(expression, "a", 0U);
    double input;
    double rounded;
    double limit;
    const int kind = expression_kind(expression);
    if (source == NULL || source->rank != 0U ||
        !f2c_constant_evaluate_real(evaluation, source, &input, depth + 1U) || !isfinite(input) ||
        (kind != 1 && kind != 2 && kind != 4 && kind != 8))
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_CEILING)
        rounded = ceil(input);
    else if (expression->intrinsic == F2C_INTRINSIC_FLOOR)
        rounded = floor(input);
    else if (expression->intrinsic == F2C_INTRINSIC_NINT)
        rounded = round(input);
    else
        return 0;
    limit = ldexp(1.0, kind * 8 - 1);
    if (rounded < -limit || rounded >= limit)
        return 0;
    return store_integer(kind, (int64_t)rounded, value);
}

static int evaluate_integer_binary(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                   int64_t *value, size_t depth) {
    const char *first_name = expression->intrinsic == F2C_INTRINSIC_DIM ? "x" : "a";
    const char *second_name = expression->intrinsic == F2C_INTRINSIC_DIM    ? "y"
                              : expression->intrinsic == F2C_INTRINSIC_SIGN ? "b"
                                                                            : "p";
    const F2cExpr *first = argument(expression, first_name, 0U);
    const F2cExpr *second = argument(expression, second_name, 1U);
    int64_t left;
    int64_t right;
    int64_t minimum;
    int64_t maximum;
    int64_t result;
    const int kind = first != NULL ? expression_kind(first) : 0;
    if (first == NULL || second == NULL || first->type != TYPE_INTEGER ||
        second->type != TYPE_INTEGER || first->rank != 0U || second->rank != 0U ||
        !integer_bounds(kind, &minimum, &maximum) ||
        !f2c_constant_evaluate_integer(evaluation, first, &left, depth + 1U) ||
        !f2c_constant_evaluate_integer(evaluation, second, &right, depth + 1U) || left < minimum ||
        left > maximum || right < minimum || right > maximum)
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_DIM) {
        if (left <= right)
            result = 0;
        else if (right < 0 && left > maximum + right)
            return 0;
        else
            result = left - right;
    } else if (expression->intrinsic == F2C_INTRINSIC_MOD ||
               expression->intrinsic == F2C_INTRINSIC_MODULO) {
        if (right == 0)
            return 0;
        result = left == minimum && right == -1 ? 0 : left % right;
        if (expression->intrinsic == F2C_INTRINSIC_MODULO && result != 0 &&
            ((result < 0) != (right < 0)))
            result += right;
    } else if (expression->intrinsic == F2C_INTRINSIC_SIGN) {
        if (left == minimum) {
            if (right >= 0)
                return 0;
            result = minimum;
        } else {
            const int64_t magnitude = left < 0 ? -left : left;
            result = right < 0 ? -magnitude : magnitude;
        }
    } else {
        return 0;
    }
    return store_integer(kind, result, value);
}

int f2c_constant_evaluate_numeric_integer(F2cConstantEvaluation *evaluation,
                                          const F2cExpr *expression, int64_t *value, size_t depth) {
    int64_t mask;
    const F2cExpr *selected;
    if (expression == NULL || expression->rank != 0U ||
        !f2c_intrinsic_is_numeric_operation(expression->intrinsic))
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_MERGE) {
        if ((expression->type != TYPE_INTEGER && expression->type != TYPE_LOGICAL) ||
            !evaluate_mask(evaluation, expression, &mask, depth))
            return 0;
        selected = argument(expression, mask != 0 ? "tsource" : "fsource", mask != 0 ? 0U : 1U);
        return f2c_constant_evaluate_integer(evaluation, selected, value, depth + 1U);
    }
    if (expression->intrinsic == F2C_INTRINSIC_CEILING ||
        expression->intrinsic == F2C_INTRINSIC_FLOOR || expression->intrinsic == F2C_INTRINSIC_NINT)
        return round_to_integer(evaluation, expression, value, depth);
    if (expression->type == TYPE_INTEGER)
        return evaluate_integer_binary(evaluation, expression, value, depth);
    return 0;
}

static int evaluate_real_unary(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                               double *value, size_t depth) {
    const F2cExpr *source = argument(expression, "a", 0U);
    double input;
    double result;
    const int source_kind = source != NULL ? expression_kind(source) : 0;
    if (source == NULL || source->rank != 0U ||
        (source->type != TYPE_REAL && source->type != TYPE_DOUBLE) ||
        !f2c_constant_evaluate_real(evaluation, source, &input, depth + 1U))
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_AINT)
        result = source_kind == 4 ? (double)truncf((float)input) : trunc(input);
    else if (expression->intrinsic == F2C_INTRINSIC_ANINT)
        result = source_kind == 4 ? (double)roundf((float)input) : round(input);
    else
        return 0;
    return store_real(expression_kind(expression), result, value);
}

static int evaluate_real_binary(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                double *value, size_t depth) {
    const char *first_name = expression->intrinsic == F2C_INTRINSIC_DIM ? "x" : "a";
    const char *second_name = expression->intrinsic == F2C_INTRINSIC_DIM    ? "y"
                              : expression->intrinsic == F2C_INTRINSIC_SIGN ? "b"
                                                                            : "p";
    const F2cExpr *first = argument(expression, first_name, 0U);
    const F2cExpr *second = argument(expression, second_name, 1U);
    double left;
    double right;
    double result;
    const int kind = first != NULL ? expression_kind(first) : 0;
    if (first == NULL || second == NULL || first->rank != 0U || second->rank != 0U ||
        (first->type != TYPE_REAL && first->type != TYPE_DOUBLE) ||
        (second->type != TYPE_REAL && second->type != TYPE_DOUBLE) ||
        !f2c_constant_evaluate_real(evaluation, first, &left, depth + 1U) ||
        !f2c_constant_evaluate_real(evaluation, second, &right, depth + 1U))
        return 0;
    if (kind == 4) {
        const float left_value = (float)left;
        const float right_value = (float)right;
        float real_result;
        if (expression->intrinsic == F2C_INTRINSIC_DIM) {
            real_result = left_value > right_value ? left_value - right_value : 0.0f;
        } else if (expression->intrinsic == F2C_INTRINSIC_MOD ||
                   expression->intrinsic == F2C_INTRINSIC_MODULO) {
            if (right_value == 0.0f)
                return 0;
            real_result = fmodf(left_value, right_value);
            if (expression->intrinsic == F2C_INTRINSIC_MODULO) {
                if (real_result == 0.0f)
                    real_result = copysignf(0.0f, right_value);
                else if ((real_result < 0.0f) != (right_value < 0.0f))
                    real_result += right_value;
            }
        } else if (expression->intrinsic == F2C_INTRINSIC_SIGN) {
            real_result = copysignf(fabsf(left_value), right_value);
        } else {
            return 0;
        }
        result = (double)real_result;
    } else if (kind == 8) {
        if (expression->intrinsic == F2C_INTRINSIC_DIM) {
            result = left > right ? left - right : 0.0;
        } else if (expression->intrinsic == F2C_INTRINSIC_MOD ||
                   expression->intrinsic == F2C_INTRINSIC_MODULO) {
            if (right == 0.0)
                return 0;
            result = fmod(left, right);
            if (expression->intrinsic == F2C_INTRINSIC_MODULO) {
                if (result == 0.0)
                    result = copysign(0.0, right);
                else if ((result < 0.0) != (right < 0.0))
                    result += right;
            }
        } else if (expression->intrinsic == F2C_INTRINSIC_SIGN) {
            result = copysign(fabs(left), right);
        } else {
            return 0;
        }
    } else {
        return 0;
    }
    return store_real(expression_kind(expression), result, value);
}

int f2c_constant_evaluate_numeric_real(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                       double *value, size_t depth) {
    int64_t mask;
    const F2cExpr *selected;
    if (expression == NULL || expression->rank != 0U ||
        !f2c_intrinsic_is_numeric_operation(expression->intrinsic))
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_MERGE) {
        if ((expression->type != TYPE_REAL && expression->type != TYPE_DOUBLE) ||
            !evaluate_mask(evaluation, expression, &mask, depth))
            return 0;
        selected = argument(expression, mask != 0 ? "tsource" : "fsource", mask != 0 ? 0U : 1U);
        return f2c_constant_evaluate_real(evaluation, selected, value, depth + 1U) &&
               store_real(expression_kind(expression), *value, value);
    }
    if (expression->intrinsic == F2C_INTRINSIC_AINT || expression->intrinsic == F2C_INTRINSIC_ANINT)
        return evaluate_real_unary(evaluation, expression, value, depth);
    if (expression->type == TYPE_REAL || expression->type == TYPE_DOUBLE)
        return evaluate_real_binary(evaluation, expression, value, depth);
    return 0;
}
