#include "codegen/expression/private.h"

#include "codegen/literal/integer.h"
#include "codegen/literal/real.h"

#include <stdint.h>
#include <stdlib.h>
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

static const char *real_function(F2cIntrinsicId intrinsic, int kind) {
    const int single = kind == 4;
    switch (intrinsic) {
    case F2C_INTRINSIC_ACOS:
        return single ? "acosf" : "acos";
    case F2C_INTRINSIC_ASIN:
        return single ? "asinf" : "asin";
    case F2C_INTRINSIC_ATAN:
        return single ? "atanf" : "atan";
    case F2C_INTRINSIC_ATAN2:
        return single ? "atan2f" : "atan2";
    case F2C_INTRINSIC_COS:
        return single ? "cosf" : "cos";
    case F2C_INTRINSIC_COSH:
        return single ? "coshf" : "cosh";
    case F2C_INTRINSIC_EXP:
        return single ? "expf" : "exp";
    case F2C_INTRINSIC_LOG:
        return single ? "logf" : "log";
    case F2C_INTRINSIC_LOG10:
        return single ? "log10f" : "log10";
    case F2C_INTRINSIC_SIN:
        return single ? "sinf" : "sin";
    case F2C_INTRINSIC_SINH:
        return single ? "sinhf" : "sinh";
    case F2C_INTRINSIC_SQRT:
        return single ? "sqrtf" : "sqrt";
    case F2C_INTRINSIC_TAN:
        return single ? "tanf" : "tan";
    case F2C_INTRINSIC_TANH:
        return single ? "tanhf" : "tanh";
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_ABS:
    case F2C_INTRINSIC_DPROD:
    case F2C_INTRINSIC_MAX:
    case F2C_INTRINSIC_MIN:
    default:
        return NULL;
    }
}

static const char *complex_function(F2cIntrinsicId intrinsic, int kind) {
    const int single = kind == 4;
    switch (intrinsic) {
    case F2C_INTRINSIC_ACOS:
        return single ? "cacosf" : "cacos";
    case F2C_INTRINSIC_ASIN:
        return single ? "casinf" : "casin";
    case F2C_INTRINSIC_ATAN:
        return single ? "catanf" : "catan";
    case F2C_INTRINSIC_COS:
        return single ? "ccosf" : "ccos";
    case F2C_INTRINSIC_COSH:
        return single ? "ccoshf" : "ccosh";
    case F2C_INTRINSIC_EXP:
        return single ? "cexpf" : "cexp";
    case F2C_INTRINSIC_LOG:
        return single ? "clogf" : "clog";
    case F2C_INTRINSIC_SIN:
        return single ? "csinf" : "csin";
    case F2C_INTRINSIC_SINH:
        return single ? "csinhf" : "csinh";
    case F2C_INTRINSIC_SQRT:
        return single ? "csqrtf" : "csqrt";
    case F2C_INTRINSIC_TAN:
        return single ? "ctanf" : "ctan";
    case F2C_INTRINSIC_TANH:
        return single ? "ctanhf" : "ctanh";
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_ABS:
    case F2C_INTRINSIC_ATAN2:
    case F2C_INTRINSIC_DPROD:
    case F2C_INTRINSIC_LOG10:
    case F2C_INTRINSIC_MAX:
    case F2C_INTRINSIC_MIN:
    default:
        return NULL;
    }
}

static char *emit_dprod(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *x = argument(expression, "x", 0U);
    const F2cExpr *y = argument(expression, "y", 1U);
    char *x_code;
    char *y_code;
    Buffer result = {0};
    if (x == NULL || y == NULL || x->rank != 0U || y->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    x_code = f2c_expression_emit(unit, x, supported);
    y_code = *supported ? f2c_expression_emit(unit, y, supported) : NULL;
    if (!*supported || x_code == NULL || y_code == NULL) {
        free(x_code);
        free(y_code);
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "((double)(%s) * (double)(%s))", x_code, y_code);
    free(x_code);
    free(y_code);
    return f2c_buffer_take(&result);
}

static char *emit_abs(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *source = argument(expression, "a", 0U);
    const char *function;
    char *code;
    Buffer result = {0};
    if (source == NULL || source->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    code = f2c_expression_emit(unit, source, supported);
    if (!*supported || code == NULL) {
        free(code);
        return NULL;
    }
    if (source->type == TYPE_INTEGER) {
        f2c_buffer_printf(&result, "F2C_ABS(%s)", code);
    } else {
        function = source->type == TYPE_REAL             ? "fabsf"
                   : source->type == TYPE_DOUBLE         ? "fabs"
                   : source->type == TYPE_COMPLEX        ? "cabsf"
                   : source->type == TYPE_DOUBLE_COMPLEX ? "cabs"
                                                         : NULL;
        if (function == NULL) {
            free(code);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, "%s(%s)", function, code);
    }
    free(code);
    return f2c_buffer_take(&result);
}

static char *emit_unary(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *source = argument(expression, "x", 0U);
    const char *function;
    char *code;
    Buffer result = {0};
    if (source == NULL || source->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    function = is_complex(source->type)
                   ? complex_function(expression->intrinsic, expression_kind(source))
                   : real_function(expression->intrinsic, expression_kind(source));
    if (function == NULL) {
        *supported = 0;
        return NULL;
    }
    code = f2c_expression_emit(unit, source, supported);
    if (!*supported || code == NULL) {
        free(code);
        return NULL;
    }
    f2c_buffer_printf(&result, "%s(%s)", function, code);
    free(code);
    return f2c_buffer_take(&result);
}

static char *emit_atan2(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *y = argument(expression, "y", 0U);
    const F2cExpr *x = argument(expression, "x", 1U);
    const char *function;
    char *y_code;
    char *x_code;
    Buffer result = {0};
    if (y == NULL || x == NULL || y->rank != 0U || x->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    function = real_function(F2C_INTRINSIC_ATAN2, expression_kind(y));
    y_code = f2c_expression_emit(unit, y, supported);
    x_code = *supported ? f2c_expression_emit(unit, x, supported) : NULL;
    if (!*supported || function == NULL || y_code == NULL || x_code == NULL) {
        free(y_code);
        free(x_code);
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "%s(%s, %s)", function, y_code, x_code);
    free(y_code);
    free(x_code);
    return f2c_buffer_take(&result);
}

static size_t extremum_index(const F2cExpr *actual) {
    char *end;
    unsigned long value;
    if (actual == NULL || actual->kind != F2C_EXPR_KEYWORD_ARGUMENT || actual->text == NULL ||
        actual->text[0] != 'a' || actual->text[1] == '\0')
        return SIZE_MAX;
    value = strtoul(actual->text + 1, &end, 10);
    return *end == '\0' && value >= 1UL && value <= 64UL ? (size_t)value - 1U : SIZE_MAX;
}

static char *emit_extremum(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *values[64] = {0};
    const char *macro =
        expression->intrinsic == F2C_INTRINSIC_MAX ? "F2C_FORTRAN_MAX" : "F2C_FORTRAN_MIN";
    const char *comparison_type;
    const char *result_type = f2c_expression_c_type(expression);
    size_t positional = 0U;
    size_t count = 0U;
    size_t index;
    Buffer result = {0};
    for (index = 0U; index < expression->child_count; ++index) {
        const F2cExpr *actual = expression->children[index];
        size_t slot;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            slot = extremum_index(actual);
            actual = actual->child_count == 1U ? actual->children[0] : NULL;
        } else {
            slot = positional++;
        }
        if (slot >= 64U || actual == NULL || values[slot] != NULL) {
            *supported = 0;
            return NULL;
        }
        values[slot] = actual;
        if (slot + 1U > count)
            count = slot + 1U;
    }
    if (count < 2U || values[0] == NULL || values[1] == NULL) {
        *supported = 0;
        return NULL;
    }
    comparison_type = f2c_expression_c_type(values[0]);
    for (index = 0U; index < count; ++index) {
        char *code;
        if (values[index] == NULL || values[index]->rank != 0U) {
            free(result.data);
            *supported = 0;
            return NULL;
        }
        code = f2c_expression_emit(unit, values[index], supported);
        if (!*supported || code == NULL) {
            free(code);
            free(result.data);
            return NULL;
        }
        if (index == 0U) {
            f2c_buffer_printf(&result, "((%s)(%s))", comparison_type, code);
        } else {
            char *previous = f2c_buffer_take(&result);
            f2c_buffer_printf(&result, "%s(%s, ((%s)(%s)))", macro, previous, comparison_type,
                              code);
            free(previous);
        }
        free(code);
    }
    if (expression->type != values[0]->type ||
        expression_kind(expression) != expression_kind(values[0])) {
        char *selected = f2c_buffer_take(&result);
        if (expression->type == TYPE_INTEGER &&
            (values[0]->type == TYPE_REAL || values[0]->type == TYPE_DOUBLE)) {
            f2c_buffer_printf(&result, "((%s)f2c_int_integer((double)(%s), %d))", result_type,
                              selected, expression_kind(expression));
        } else {
            f2c_buffer_printf(&result, "((%s)(%s))", result_type, selected);
        }
        free(selected);
    }
    return f2c_buffer_take(&result);
}

char *f2c_expression_mathematical_intrinsic(Unit *unit, const F2cExpr *expression, int *supported) {
    int64_t integer_value;
    double real_value;
    char *constant;
    if (unit == NULL || expression == NULL || supported == NULL || expression->rank != 0U ||
        !f2c_intrinsic_is_mathematical(expression->intrinsic)) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    if (expression->type == TYPE_INTEGER &&
        f2c_evaluate_integer_constant(unit, expression, &integer_value)) {
        constant = f2c_integer_constant_literal(integer_value, expression_kind(expression));
        if (constant != NULL)
            return constant;
    }
    if ((expression->type == TYPE_REAL || expression->type == TYPE_DOUBLE) &&
        f2c_evaluate_real_constant(unit, expression, &real_value)) {
        constant = f2c_real_constant_literal(real_value, expression_kind(expression));
        if (constant != NULL)
            return constant;
    }
    if (expression->intrinsic == F2C_INTRINSIC_ABS)
        return emit_abs(unit, expression, supported);
    if (expression->intrinsic == F2C_INTRINSIC_ATAN2)
        return emit_atan2(unit, expression, supported);
    if (expression->intrinsic == F2C_INTRINSIC_DPROD)
        return emit_dprod(unit, expression, supported);
    if (expression->intrinsic == F2C_INTRINSIC_MAX || expression->intrinsic == F2C_INTRINSIC_MIN)
        return emit_extremum(unit, expression, supported);
    return emit_unary(unit, expression, supported);
}
