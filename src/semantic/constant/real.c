#include "internal/f2c.h"
#include "semantic/constant/private.h"

#include "semantic/numeric_model.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int real_kind(const F2cExpr *expression) {
    if (expression == NULL)
        return 0;
    return expression->type_kind != 0 ? expression->type_kind : f2c_default_kind(expression->type);
}

static int store_rounded(const F2cExpr *expression, double input, double *output) {
    const int kind = real_kind(expression);
    if (kind == 4) {
        *output = (double)(float)input;
        return 1;
    }
    if (kind == 8) {
        *output = input;
        return 1;
    }
    return 0;
}

static int parse_real_literal(const F2cExpr *expression, double *value) {
    const char *text = expression->text;
    const char *suffix;
    char *copy;
    char *end;
    char *cursor;
    size_t length;
    double parsed;
    if (text == NULL)
        return 0;
    suffix = strchr(text, '_');
    length = suffix != NULL ? (size_t)(suffix - text) : strlen(text);
    copy = f2c_strdup_n(text, length);
    if (copy == NULL)
        return 0;
    for (cursor = copy; *cursor != '\0'; ++cursor)
        if (*cursor == 'd' || *cursor == 'D' || *cursor == 'q' || *cursor == 'Q')
            *cursor = 'e';
    parsed = strtod(copy, &end);
    if (end == copy || *end != '\0') {
        free(copy);
        return 0;
    }
    free(copy);
    return store_rounded(expression, parsed, value);
}

static int evaluate_numeric(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                            double *value, size_t depth) {
    int64_t integer;
    if (expression != NULL && expression->type == TYPE_INTEGER) {
        if (!f2c_constant_evaluate_integer(evaluation, expression, &integer, depth))
            return 0;
        *value = (double)integer;
        return 1;
    }
    return f2c_constant_evaluate_real(evaluation, expression, value, depth);
}

static double scale_value(double value, int64_t power, int kind) {
    if (!isfinite(value) || value == 0.0)
        return value;
    if (power > INT_MAX)
        return copysign(INFINITY, value);
    if (power < INT_MIN)
        return copysign(0.0, value);
    return kind == 4 ? (double)scalbnf((float)value, (int)power) : scalbn(value, (int)power);
}

static int evaluate_model_value(const F2cExpr *expression, double *value) {
    const F2cExpr *model =
        f2c_intrinsic_argument(expression->children, expression->child_count, "x", 0U);
    const F2cNumericModel *numeric_model;
    int kind;
    if (model == NULL || (expression->intrinsic != F2C_INTRINSIC_EPSILON &&
                          expression->intrinsic != F2C_INTRINSIC_HUGE &&
                          expression->intrinsic != F2C_INTRINSIC_TINY))
        return 0;
    kind = real_kind(model);
    numeric_model = f2c_numeric_model(model->type, kind);
    if (numeric_model == NULL || numeric_model->type != TYPE_REAL)
        return 0;
    if (kind == 4)
        *value = expression->intrinsic == F2C_INTRINSIC_EPSILON ? (double)FLT_EPSILON
                 : expression->intrinsic == F2C_INTRINSIC_HUGE  ? (double)FLT_MAX
                                                                : (double)FLT_MIN;
    else if (kind == 8)
        *value = expression->intrinsic == F2C_INTRINSIC_EPSILON ? DBL_EPSILON
                 : expression->intrinsic == F2C_INTRINSIC_HUGE  ? DBL_MAX
                                                                : DBL_MIN;
    else
        return 0;
    return store_rounded(expression, *value, value);
}

static int evaluate_representation(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                   double *value, size_t depth) {
    const F2cExpr *x;
    const F2cExpr *second = NULL;
    double x_value;
    double second_value = 0.0;
    int64_t power = 0;
    int exponent = 0;
    int kind;
    if (!f2c_intrinsic_is_real_representation(expression->intrinsic) ||
        expression->intrinsic == F2C_INTRINSIC_EXPONENT || expression->rank != 0U)
        return 0;
    x = f2c_intrinsic_argument(expression->children, expression->child_count, "x", 0U);
    if (x == NULL || !f2c_constant_evaluate_real(evaluation, x, &x_value, depth + 1U))
        return 0;
    kind = real_kind(x);
    if (kind != 4 && kind != 8)
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_NEAREST) {
        second = f2c_intrinsic_argument(expression->children, expression->child_count, "s", 1U);
        if (second == NULL ||
            !f2c_constant_evaluate_real(evaluation, second, &second_value, depth + 1U) ||
            second_value == 0.0 || isnan(second_value))
            return 0;
    } else if (expression->intrinsic == F2C_INTRINSIC_SCALE ||
               expression->intrinsic == F2C_INTRINSIC_SET_EXPONENT) {
        second = f2c_intrinsic_argument(expression->children, expression->child_count, "i", 1U);
        if (second == NULL ||
            !f2c_constant_evaluate_integer(evaluation, second, &power, depth + 1U))
            return 0;
    }
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_FRACTION:
        *value = !isfinite(x_value) ? NAN
                 : kind == 4        ? (double)frexpf((float)x_value, &exponent)
                                    : frexp(x_value, &exponent);
        break;
    case F2C_INTRINSIC_NEAREST:
        *value = isnan(x_value) ? NAN
                 : kind == 4
                     ? (double)nextafterf((float)x_value, second_value > 0.0 ? INFINITY : -INFINITY)
                     : nextafter(x_value, second_value > 0.0 ? INFINITY : -INFINITY);
        break;
    case F2C_INTRINSIC_RRSPACING: {
        double fraction;
        if (!isfinite(x_value)) {
            *value = NAN;
        } else if (kind == 4) {
            fraction = (double)frexpf((float)x_value, &exponent);
            *value = (double)scalbnf(fabsf((float)fraction), FLT_MANT_DIG);
        } else {
            fraction = frexp(x_value, &exponent);
            *value = scalbn(fabs(fraction), DBL_MANT_DIG);
        }
        break;
    }
    case F2C_INTRINSIC_SCALE:
        *value = scale_value(x_value, power, kind);
        break;
    case F2C_INTRINSIC_SET_EXPONENT:
        if (!isfinite(x_value)) {
            *value = NAN;
        } else {
            const double fraction =
                kind == 4 ? (double)frexpf((float)x_value, &exponent) : frexp(x_value, &exponent);
            *value = scale_value(fraction, power, kind);
        }
        break;
    case F2C_INTRINSIC_SPACING:
        if (!isfinite(x_value)) {
            *value = NAN;
        } else if (x_value == 0.0) {
            *value = kind == 4 ? (double)FLT_MIN : DBL_MIN;
        } else if (kind == 4) {
            (void)frexpf((float)x_value, &exponent);
            exponent -= FLT_MANT_DIG;
            if (exponent < FLT_MIN_EXP - 1)
                exponent = FLT_MIN_EXP - 1;
            *value = (double)scalbnf(1.0f, exponent);
        } else {
            (void)frexp(x_value, &exponent);
            exponent -= DBL_MANT_DIG;
            if (exponent < DBL_MIN_EXP - 1)
                exponent = DBL_MIN_EXP - 1;
            *value = scalbn(1.0, exponent);
        }
        break;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_EXPONENT:
    default:
        return 0;
    }
    return store_rounded(expression, *value, value);
}

int f2c_constant_evaluate_real(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                               double *value, size_t depth) {
    double left;
    double right;
    Unit *unit = evaluation->unit;
    if (expression == NULL || value == NULL || !f2c_constant_consume_step(evaluation, depth))
        return 0;
    if (expression->kind == F2C_EXPR_REAL_LITERAL)
        return parse_real_literal(expression, value);
    if (expression->kind == F2C_EXPR_INTEGER_LITERAL) {
        int64_t integer;
        if (!f2c_constant_evaluate_integer(evaluation, expression, &integer, depth + 1U))
            return 0;
        return store_rounded(expression, (double)integer, value);
    }
    if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
        expression->symbol->parameter && expression->symbol->initializer != NULL) {
        F2cExpr *temporary = NULL;
        const F2cExpr *initializer = expression->symbol->initializer_expression;
        int result;
        if (initializer == NULL && expression->symbol->initializer_syntax.count != 0U) {
            temporary =
                f2c_parse_expression_tokens(unit, expression->symbol->initializer_syntax.tokens,
                                            expression->symbol->initializer_syntax.count,
                                            expression->symbol->initializer_syntax.source, NULL);
            initializer = temporary;
        }
        result = f2c_constant_evaluate_real(evaluation, initializer, value, depth + 1U);
        f2c_expr_free(temporary);
        return result && store_rounded(expression, *value, value);
    }
    if (expression->kind == F2C_EXPR_UNARY && expression->child_count == 1U &&
        evaluate_numeric(evaluation, expression->children[0], &left, depth + 1U)) {
        if (strcmp(expression->text, "+") == 0)
            return store_rounded(expression, left, value);
        if (strcmp(expression->text, "-") == 0)
            return store_rounded(expression, -left, value);
        return 0;
    }
    if (expression->kind == F2C_EXPR_CALL && expression->text != NULL) {
        if (f2c_constant_evaluate_numeric_real(evaluation, expression, value, depth))
            return 1;
        if (evaluate_model_value(expression, value) ||
            evaluate_representation(evaluation, expression, value, depth))
            return 1;
        if ((strcmp(expression->text, "real") == 0 || strcmp(expression->text, "dble") == 0 ||
             strcmp(expression->text, "float") == 0) &&
            expression->child_count != 0U &&
            evaluate_numeric(evaluation, expression->children[0], value, depth + 1U))
            return store_rounded(expression, *value, value);
        return 0;
    }
    if (expression->kind != F2C_EXPR_BINARY || expression->child_count != 2U ||
        !evaluate_numeric(evaluation, expression->children[0], &left, depth + 1U) ||
        !evaluate_numeric(evaluation, expression->children[1], &right, depth + 1U))
        return 0;
    if (strcmp(expression->text, "+") == 0)
        return store_rounded(expression, left + right, value);
    if (strcmp(expression->text, "-") == 0)
        return store_rounded(expression, left - right, value);
    if (strcmp(expression->text, "*") == 0)
        return store_rounded(expression, left * right, value);
    if (strcmp(expression->text, "/") == 0)
        return store_rounded(expression, left / right, value);
    if (strcmp(expression->text, "**") == 0)
        return store_rounded(expression,
                             real_kind(expression) == 4 ? (double)powf((float)left, (float)right)
                                                        : pow(left, right),
                             value);
    return 0;
}

int f2c_evaluate_real_constant(Unit *unit, const F2cExpr *expression, double *value) {
    F2cConstantEvaluation evaluation = {unit, unit != NULL ? unit->context : NULL, 0U};
    return f2c_constant_evaluate_real(&evaluation, expression, value, 0U);
}
