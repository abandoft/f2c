#include "semantic/constant/private.h"

#include "internal/f2c.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static int is_complex(Type type) { return type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX; }

static const F2cExpr *argument(const F2cExpr *expression, const char *name, size_t position) {
    return f2c_intrinsic_argument(expression->children, expression->child_count, name, position);
}

static int store_complex(int kind, double real, double imaginary, F2cComplexConstant *value) {
    if (kind == 4) {
        value->real = (double)(float)real;
        value->imaginary = (double)(float)imaginary;
        return 1;
    }
    if (kind == 8) {
        value->real = real;
        value->imaginary = imaginary;
        return 1;
    }
    return 0;
}

static int evaluate_component(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                              double *value, size_t depth) {
    int64_t integer;
    if (expression == NULL || is_complex(expression->type))
        return 0;
    if (expression->type == TYPE_INTEGER) {
        if (!f2c_constant_evaluate_integer(evaluation, expression, &integer, depth))
            return 0;
        *value = (double)integer;
        return 1;
    }
    if (expression->type != TYPE_REAL && expression->type != TYPE_DOUBLE)
        return 0;
    return f2c_constant_evaluate_real(evaluation, expression, value, depth);
}

static int evaluate_numeric(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                            F2cComplexConstant *value, size_t depth) {
    double component;
    if (expression == NULL)
        return 0;
    if (is_complex(expression->type))
        return f2c_constant_evaluate_complex(evaluation, expression, value, depth);
    if (!evaluate_component(evaluation, expression, &component, depth))
        return 0;
    value->real = component;
    value->imaginary = 0.0;
    return 1;
}

static F2cComplexConstant complex_multiply(F2cComplexConstant left, F2cComplexConstant right,
                                           int kind) {
    F2cComplexConstant result;
    if (kind == 4) {
        const float left_real = (float)left.real;
        const float left_imaginary = (float)left.imaginary;
        const float right_real = (float)right.real;
        const float right_imaginary = (float)right.imaginary;
        result.real = (double)(left_real * right_real - left_imaginary * right_imaginary);
        result.imaginary = (double)(left_real * right_imaginary + left_imaginary * right_real);
    } else {
        result.real = left.real * right.real - left.imaginary * right.imaginary;
        result.imaginary = left.real * right.imaginary + left.imaginary * right.real;
    }
    return result;
}

static int complex_divide(F2cComplexConstant left, F2cComplexConstant right, int kind,
                          F2cComplexConstant *result) {
    if (kind == 4) {
        const float a = (float)left.real;
        const float b = (float)left.imaginary;
        const float c = (float)right.real;
        const float d = (float)right.imaginary;
        const float scale = fmaxf(fabsf(c), fabsf(d));
        float scaled_a;
        float scaled_b;
        float scaled_c;
        float scaled_d;
        float denominator;
        if (!isfinite(scale) || scale == 0.0f)
            return 0;
        scaled_a = a / scale;
        scaled_b = b / scale;
        scaled_c = c / scale;
        scaled_d = d / scale;
        denominator = scaled_c * scaled_c + scaled_d * scaled_d;
        result->real = (double)((scaled_a * scaled_c + scaled_b * scaled_d) / denominator);
        result->imaginary = (double)((scaled_b * scaled_c - scaled_a * scaled_d) / denominator);
        return 1;
    }
    if (kind == 8) {
        const double scale = fmax(fabs(right.real), fabs(right.imaginary));
        double scaled_left_real;
        double scaled_left_imaginary;
        double scaled_right_real;
        double scaled_right_imaginary;
        double denominator;
        if (!isfinite(scale) || scale == 0.0)
            return 0;
        scaled_left_real = left.real / scale;
        scaled_left_imaginary = left.imaginary / scale;
        scaled_right_real = right.real / scale;
        scaled_right_imaginary = right.imaginary / scale;
        denominator =
            scaled_right_real * scaled_right_real + scaled_right_imaginary * scaled_right_imaginary;
        result->real = (scaled_left_real * scaled_right_real +
                        scaled_left_imaginary * scaled_right_imaginary) /
                       denominator;
        result->imaginary = (scaled_left_imaginary * scaled_right_real -
                             scaled_left_real * scaled_right_imaginary) /
                            denominator;
        return 1;
    }
    return 0;
}

static F2cComplexConstant complex_exp(F2cComplexConstant input, int kind) {
    F2cComplexConstant result;
    if (kind == 4) {
        const float magnitude = expf((float)input.real);
        result.real = (double)(magnitude * cosf((float)input.imaginary));
        result.imaginary = (double)(magnitude * sinf((float)input.imaginary));
    } else {
        const double magnitude = exp(input.real);
        result.real = magnitude * cos(input.imaginary);
        result.imaginary = magnitude * sin(input.imaginary);
    }
    return result;
}

static F2cComplexConstant complex_log(F2cComplexConstant input, int kind) {
    F2cComplexConstant result;
    if (kind == 4) {
        result.real = (double)logf(hypotf((float)input.real, (float)input.imaginary));
        result.imaginary = (double)atan2f((float)input.imaginary, (float)input.real);
    } else {
        result.real = log(hypot(input.real, input.imaginary));
        result.imaginary = atan2(input.imaginary, input.real);
    }
    return result;
}

static F2cComplexConstant complex_sqrt(F2cComplexConstant input, int kind) {
    F2cComplexConstant result;
    if (kind == 4) {
        const float real = (float)input.real;
        const float imaginary = (float)input.imaginary;
        const float magnitude = hypotf(real, imaginary);
        float scale;
        if (real == 0.0f && imaginary == 0.0f) {
            result.real = 0.0;
            result.imaginary = (double)imaginary;
        } else if (real >= 0.0f) {
            scale = sqrtf((magnitude + real) * 0.5f);
            result.real = (double)scale;
            result.imaginary = (double)(imaginary / (2.0f * scale));
        } else {
            scale = sqrtf((magnitude - real) * 0.5f);
            result.real = (double)(fabsf(imaginary) / (2.0f * scale));
            result.imaginary = (double)copysignf(scale, imaginary);
        }
    } else {
        const double magnitude = hypot(input.real, input.imaginary);
        double scale;
        if (input.real == 0.0 && input.imaginary == 0.0) {
            result.real = 0.0;
            result.imaginary = input.imaginary;
        } else if (input.real >= 0.0) {
            scale = sqrt((magnitude + input.real) * 0.5);
            result.real = scale;
            result.imaginary = input.imaginary / (2.0 * scale);
        } else {
            scale = sqrt((magnitude - input.real) * 0.5);
            result.real = fabs(input.imaginary) / (2.0 * scale);
            result.imaginary = copysign(scale, input.imaginary);
        }
    }
    return result;
}

static F2cComplexConstant complex_sin(F2cComplexConstant input, int kind) {
    F2cComplexConstant result;
    if (kind == 4) {
        const float real = (float)input.real;
        const float imaginary = (float)input.imaginary;
        result.real = (double)(sinf(real) * coshf(imaginary));
        result.imaginary = (double)(cosf(real) * sinhf(imaginary));
    } else {
        result.real = sin(input.real) * cosh(input.imaginary);
        result.imaginary = cos(input.real) * sinh(input.imaginary);
    }
    return result;
}

static F2cComplexConstant complex_cos(F2cComplexConstant input, int kind) {
    F2cComplexConstant result;
    if (kind == 4) {
        const float real = (float)input.real;
        const float imaginary = (float)input.imaginary;
        result.real = (double)(cosf(real) * coshf(imaginary));
        result.imaginary = (double)(-sinf(real) * sinhf(imaginary));
    } else {
        result.real = cos(input.real) * cosh(input.imaginary);
        result.imaginary = -sin(input.real) * sinh(input.imaginary);
    }
    return result;
}

static F2cComplexConstant complex_sinh(F2cComplexConstant input, int kind) {
    F2cComplexConstant result;
    if (kind == 4) {
        const float real = (float)input.real;
        const float imaginary = (float)input.imaginary;
        result.real = (double)(sinhf(real) * cosf(imaginary));
        result.imaginary = (double)(coshf(real) * sinf(imaginary));
    } else {
        result.real = sinh(input.real) * cos(input.imaginary);
        result.imaginary = cosh(input.real) * sin(input.imaginary);
    }
    return result;
}

static F2cComplexConstant complex_cosh(F2cComplexConstant input, int kind) {
    F2cComplexConstant result;
    if (kind == 4) {
        const float real = (float)input.real;
        const float imaginary = (float)input.imaginary;
        result.real = (double)(coshf(real) * cosf(imaginary));
        result.imaginary = (double)(sinhf(real) * sinf(imaginary));
    } else {
        result.real = cosh(input.real) * cos(input.imaginary);
        result.imaginary = sinh(input.real) * sin(input.imaginary);
    }
    return result;
}

static int complex_asin(F2cComplexConstant input, int kind, F2cComplexConstant *result) {
    F2cComplexConstant square = complex_multiply(input, input, kind);
    F2cComplexConstant root_input = {1.0 - square.real, -square.imaginary};
    F2cComplexConstant root = complex_sqrt(root_input, kind);
    F2cComplexConstant logarithm_input = {root.real - input.imaginary, root.imaginary + input.real};
    F2cComplexConstant logarithm;
    if (logarithm_input.real == 0.0 && logarithm_input.imaginary == 0.0)
        return 0;
    logarithm = complex_log(logarithm_input, kind);
    result->real = logarithm.imaginary;
    result->imaginary = -logarithm.real;
    return 1;
}

static int complex_atan(F2cComplexConstant input, int kind, F2cComplexConstant *result) {
    const F2cComplexConstant left_input = {1.0 + input.imaginary, -input.real};
    const F2cComplexConstant right_input = {1.0 - input.imaginary, input.real};
    F2cComplexConstant difference;
    F2cComplexConstant left;
    F2cComplexConstant right;
    if ((left_input.real == 0.0 && left_input.imaginary == 0.0) ||
        (right_input.real == 0.0 && right_input.imaginary == 0.0))
        return 0;
    left = complex_log(left_input, kind);
    right = complex_log(right_input, kind);
    difference.real = left.real - right.real;
    difference.imaginary = left.imaginary - right.imaginary;
    result->real = -0.5 * difference.imaginary;
    result->imaginary = 0.5 * difference.real;
    return 1;
}

static int evaluate_conversion(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                               F2cComplexConstant *value, size_t depth) {
    const F2cExpr *source;
    const F2cExpr *imaginary;
    F2cComplexConstant input;
    double real_value;
    double imaginary_value = 0.0;
    if (expression->intrinsic == F2C_INTRINSIC_CONJG) {
        source = argument(expression, "z", 0U);
        if (source == NULL ||
            !f2c_constant_evaluate_complex(evaluation, source, &input, depth + 1U))
            return 0;
        return store_complex(expression_kind(expression), input.real, -input.imaginary, value);
    }
    if (expression->intrinsic != F2C_INTRINSIC_CMPLX)
        return 0;
    source = argument(expression, "x", 0U);
    imaginary = argument(expression, "y", 1U);
    if (source == NULL)
        return 0;
    if (is_complex(source->type)) {
        if (imaginary != NULL ||
            !f2c_constant_evaluate_complex(evaluation, source, &input, depth + 1U))
            return 0;
        return store_complex(expression_kind(expression), input.real, input.imaginary, value);
    }
    if (!evaluate_component(evaluation, source, &real_value, depth + 1U))
        return 0;
    if (imaginary != NULL &&
        !evaluate_component(evaluation, imaginary, &imaginary_value, depth + 1U))
        return 0;
    return store_complex(expression_kind(expression), real_value, imaginary_value, value);
}

static int evaluate_mathematical(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                 F2cComplexConstant *value, size_t depth) {
    const F2cExpr *source;
    F2cComplexConstant input;
    F2cComplexConstant result;
    const int kind = expression_kind(expression);
    if (!f2c_intrinsic_is_mathematical(expression->intrinsic) ||
        expression->intrinsic == F2C_INTRINSIC_ABS)
        return 0;
    source = argument(expression, "x", 0U);
    if (source == NULL || !f2c_constant_evaluate_complex(evaluation, source, &input, depth + 1U))
        return 0;
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_ACOS:
        if (!complex_asin(input, kind, &result))
            return 0;
        result.real = kind == 4 ? (double)(1.57079632679489661923f - (float)result.real)
                                : 1.57079632679489661923 - result.real;
        result.imaginary = -result.imaginary;
        break;
    case F2C_INTRINSIC_ASIN:
        if (!complex_asin(input, kind, &result))
            return 0;
        break;
    case F2C_INTRINSIC_ATAN:
        if (!complex_atan(input, kind, &result))
            return 0;
        break;
    case F2C_INTRINSIC_COS:
        result = complex_cos(input, kind);
        break;
    case F2C_INTRINSIC_COSH:
        result = complex_cosh(input, kind);
        break;
    case F2C_INTRINSIC_EXP:
        result = complex_exp(input, kind);
        break;
    case F2C_INTRINSIC_LOG:
        if (input.real == 0.0 && input.imaginary == 0.0)
            return 0;
        result = complex_log(input, kind);
        break;
    case F2C_INTRINSIC_SIN:
        result = complex_sin(input, kind);
        break;
    case F2C_INTRINSIC_SINH:
        result = complex_sinh(input, kind);
        break;
    case F2C_INTRINSIC_SQRT:
        result = complex_sqrt(input, kind);
        break;
    case F2C_INTRINSIC_TAN: {
        const F2cComplexConstant numerator = complex_sin(input, kind);
        const F2cComplexConstant denominator = complex_cos(input, kind);
        if (!complex_divide(numerator, denominator, kind, &result))
            return 0;
        break;
    }
    case F2C_INTRINSIC_TANH: {
        const F2cComplexConstant numerator = complex_sinh(input, kind);
        const F2cComplexConstant denominator = complex_cosh(input, kind);
        if (!complex_divide(numerator, denominator, kind, &result))
            return 0;
        break;
    }
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_ABS:
    case F2C_INTRINSIC_ATAN2:
    case F2C_INTRINSIC_DPROD:
    case F2C_INTRINSIC_LOG10:
    case F2C_INTRINSIC_MAX:
    case F2C_INTRINSIC_MIN:
    default:
        return 0;
    }
    return store_complex(kind, result.real, result.imaginary, value);
}

static int evaluate_power(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                          F2cComplexConstant left, F2cComplexConstant *value, size_t depth) {
    const F2cExpr *exponent = expression->children[1];
    F2cComplexConstant result = {1.0, 0.0};
    F2cComplexConstant base = left;
    F2cComplexConstant power;
    int64_t integer;
    uint64_t magnitude;
    const int kind = expression_kind(expression);
    if (exponent->type == TYPE_INTEGER &&
        f2c_constant_evaluate_integer(evaluation, exponent, &integer, depth + 1U)) {
        const int negative = integer < 0;
        magnitude = negative ? UINT64_C(0) - (uint64_t)integer : (uint64_t)integer;
        while (magnitude != 0U) {
            if ((magnitude & UINT64_C(1)) != 0U)
                result = complex_multiply(result, base, kind);
            magnitude >>= 1U;
            if (magnitude != 0U)
                base = complex_multiply(base, base, kind);
        }
        if (negative) {
            const F2cComplexConstant one = {1.0, 0.0};
            if (!complex_divide(one, result, kind, &result))
                return 0;
        }
        return store_complex(kind, result.real, result.imaginary, value);
    }
    if (!evaluate_numeric(evaluation, exponent, &power, depth + 1U))
        return 0;
    if (left.real == 0.0 && left.imaginary == 0.0) {
        if (power.imaginary != 0.0 || power.real < 0.0)
            return 0;
        return store_complex(kind, power.real == 0.0 ? 1.0 : 0.0, 0.0, value);
    }
    result = complex_multiply(power, complex_log(left, kind), kind);
    result = complex_exp(result, kind);
    return store_complex(kind, result.real, result.imaginary, value);
}

int f2c_constant_evaluate_complex(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  F2cComplexConstant *value, size_t depth) {
    F2cComplexConstant left;
    F2cComplexConstant right;
    Unit *unit = evaluation->unit;
    const int kind = expression_kind(expression);
    if (expression == NULL || value == NULL || !is_complex(expression->type) ||
        !f2c_constant_consume_step(evaluation, depth))
        return 0;
    if (expression->kind == F2C_EXPR_COMPLEX_LITERAL && expression->child_count == 2U) {
        double real;
        double imaginary;
        if (!evaluate_component(evaluation, expression->children[0], &real, depth + 1U) ||
            !evaluate_component(evaluation, expression->children[1], &imaginary, depth + 1U))
            return 0;
        return store_complex(kind, real, imaginary, value);
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
        result = f2c_constant_evaluate_complex(evaluation, initializer, value, depth + 1U);
        f2c_expr_free(temporary);
        return result && store_complex(kind, value->real, value->imaginary, value);
    }
    if (expression->kind == F2C_EXPR_UNARY && expression->child_count == 1U &&
        evaluate_numeric(evaluation, expression->children[0], &left, depth + 1U)) {
        if (strcmp(expression->text, "+") == 0)
            return store_complex(kind, left.real, left.imaginary, value);
        if (strcmp(expression->text, "-") == 0)
            return store_complex(kind, -left.real, -left.imaginary, value);
        return 0;
    }
    if (expression->kind == F2C_EXPR_CALL && expression->text != NULL) {
        if (evaluate_conversion(evaluation, expression, value, depth))
            return 1;
        return evaluate_mathematical(evaluation, expression, value, depth);
    }
    if (expression->kind != F2C_EXPR_BINARY || expression->child_count != 2U ||
        !evaluate_numeric(evaluation, expression->children[0], &left, depth + 1U))
        return 0;
    if (strcmp(expression->text, "**") == 0)
        return evaluate_power(evaluation, expression, left, value, depth);
    if (!evaluate_numeric(evaluation, expression->children[1], &right, depth + 1U))
        return 0;
    if (strcmp(expression->text, "+") == 0)
        return store_complex(kind, left.real + right.real, left.imaginary + right.imaginary, value);
    if (strcmp(expression->text, "-") == 0)
        return store_complex(kind, left.real - right.real, left.imaginary - right.imaginary, value);
    if (strcmp(expression->text, "*") == 0) {
        left = complex_multiply(left, right, kind);
        return store_complex(kind, left.real, left.imaginary, value);
    }
    if (strcmp(expression->text, "/") == 0 && complex_divide(left, right, kind, &left))
        return store_complex(kind, left.real, left.imaginary, value);
    return 0;
}

int f2c_evaluate_complex_constant(Unit *unit, const F2cExpr *expression, double *real,
                                  double *imaginary) {
    F2cConstantEvaluation evaluation = {unit, unit != NULL ? unit->context : NULL, 0U};
    F2cComplexConstant value;
    if (real == NULL || imaginary == NULL || expression == NULL)
        return 0;
    if (!is_complex(expression->type)) {
        if (!evaluate_component(&evaluation, expression, &value.real, 0U))
            return 0;
        value.imaginary = 0.0;
    } else if (!f2c_constant_evaluate_complex(&evaluation, expression, &value, 0U)) {
        return 0;
    }
    *real = value.real;
    *imaginary = value.imaginary;
    return 1;
}
