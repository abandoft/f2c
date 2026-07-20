#include "codegen/expression/private.h"

#include "codegen/literal/real.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static const char *integer_prefix(int kind) {
    switch (kind) {
    case 1:
        return "INT8_C";
    case 2:
        return "INT16_C";
    case 4:
        return "INT32_C";
    case 8:
        return "INT64_C";
    default:
        return NULL;
    }
}

static char *integer_constant(int64_t value, int kind) {
    const char *prefix = integer_prefix(kind);
    Buffer result = {0};
    if (prefix == NULL)
        return NULL;
    if ((kind == 1 && value == INT8_MIN) || (kind == 2 && value == INT16_MIN) ||
        (kind == 4 && value == INT32_MIN) || (kind == 8 && value == INT64_MIN)) {
        f2c_buffer_printf(&result, "INT%d_MIN", kind * 8);
    } else if (value < 0) {
        f2c_buffer_printf(&result, "-%s(%lld)", prefix, (long long)-value);
    } else {
        f2c_buffer_printf(&result, "%s(%lld)", prefix, (long long)value);
    }
    return f2c_buffer_take(&result);
}

static const F2cExpr *argument(const F2cExpr *expression, const char *name, size_t position) {
    return expression != NULL ? f2c_intrinsic_argument(expression->children,
                                                       expression->child_count, name, position)
                              : NULL;
}

static char *emit_argument(Unit *unit, const F2cExpr *expression, const char *name, size_t position,
                           int *supported) {
    const F2cExpr *actual = argument(expression, name, position);
    if (actual == NULL || actual->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    return f2c_expression_emit(unit, actual, supported);
}

static char *emit_rounding_real(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *source = argument(expression, "a", 0U);
    const char *operation;
    const char *source_type;
    const char *result_type = f2c_expression_c_type(expression);
    char *source_code;
    Buffer result = {0};
    if (source == NULL || source->rank != 0U ||
        (source->type != TYPE_REAL && source->type != TYPE_DOUBLE)) {
        *supported = 0;
        return NULL;
    }
    operation = expression->intrinsic == F2C_INTRINSIC_AINT
                    ? (expression_kind(source) == 4 ? "truncf" : "trunc")
                    : (expression_kind(source) == 4 ? "roundf" : "round");
    source_type = f2c_expression_c_type(source);
    source_code = f2c_expression_emit(unit, source, supported);
    if (!*supported || source_code == NULL) {
        free(source_code);
        return NULL;
    }
    f2c_buffer_printf(&result, "((%s)%s((%s)(%s)))", result_type, operation, source_type,
                      source_code);
    free(source_code);
    return f2c_buffer_take(&result);
}

static char *emit_rounding_integer(Unit *unit, const F2cExpr *expression, int *supported) {
    const char *operation = expression->intrinsic == F2C_INTRINSIC_CEILING ? "ceiling"
                            : expression->intrinsic == F2C_INTRINSIC_FLOOR ? "floor"
                                                                           : "nint";
    const int kind = expression_kind(expression);
    char *source = emit_argument(unit, expression, "a", 0U, supported);
    Buffer result = {0};
    if (!*supported || source == NULL || integer_prefix(kind) == NULL) {
        free(source);
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "((%s)f2c_%s_integer((double)(%s), %d))",
                      f2c_expression_c_type(expression), operation, source, kind);
    free(source);
    return f2c_buffer_take(&result);
}

static const char *operation_suffix(const F2cExpr *expression) {
    const int kind = expression_kind(expression);
    if (expression->type == TYPE_INTEGER) {
        switch (kind) {
        case 1:
            return "i8";
        case 2:
            return "i16";
        case 4:
            return "i32";
        case 8:
            return "i64";
        default:
            return NULL;
        }
    }
    if (expression->type == TYPE_REAL || expression->type == TYPE_DOUBLE)
        return kind == 4 ? "r4" : kind == 8 ? "r8" : NULL;
    return NULL;
}

static char *emit_binary(Unit *unit, const F2cExpr *expression, int *supported) {
    const char *first_name = expression->intrinsic == F2C_INTRINSIC_DIM ? "x" : "a";
    const char *second_name = expression->intrinsic == F2C_INTRINSIC_DIM    ? "y"
                              : expression->intrinsic == F2C_INTRINSIC_SIGN ? "b"
                                                                            : "p";
    const char *operation = expression->intrinsic == F2C_INTRINSIC_DIM      ? "dim"
                            : expression->intrinsic == F2C_INTRINSIC_MOD    ? "mod"
                            : expression->intrinsic == F2C_INTRINSIC_MODULO ? "modulo"
                                                                            : "sign";
    const char *suffix = operation_suffix(expression);
    const char *type = f2c_expression_c_type(expression);
    char *first = emit_argument(unit, expression, first_name, 0U, supported);
    char *second = *supported ? emit_argument(unit, expression, second_name, 1U, supported) : NULL;
    Buffer result = {0};
    if (!*supported || suffix == NULL || first == NULL || second == NULL) {
        free(first);
        free(second);
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result, "f2c_%s_%s((%s)(%s), (%s)(%s))", operation, suffix, type, first,
                      type, second);
    free(first);
    free(second);
    return f2c_buffer_take(&result);
}

static char *emit_character_merge(Unit *unit, const F2cExpr *true_source,
                                  const F2cExpr *false_source, char *true_code, char *false_code,
                                  char *mask_code, int *supported) {
    char *true_pointer = f2c_character_source_pointer(unit, true_source, true_code);
    char *false_pointer = f2c_character_source_pointer(unit, false_source, false_code);
    char *true_length = f2c_character_length_expression(unit, true_source);
    char *false_length = f2c_character_length_expression(unit, false_source);
    Buffer result = {0};
    if (true_pointer == NULL || false_pointer == NULL || true_length == NULL ||
        false_length == NULL) {
        free(true_pointer);
        free(false_pointer);
        free(true_length);
        free(false_length);
        *supported = 0;
        return NULL;
    }
    f2c_buffer_printf(&result,
                      "((size_t)(%s) == (size_t)(%s) ? ((bool)(%s) ? %s : %s) : "
                      "(abort(), (char *)NULL))",
                      true_length, false_length, mask_code, true_pointer, false_pointer);
    free(true_pointer);
    free(false_pointer);
    free(true_length);
    free(false_length);
    return f2c_buffer_take(&result);
}

static char *emit_merge(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *true_source = argument(expression, "tsource", 0U);
    const F2cExpr *false_source = argument(expression, "fsource", 1U);
    const F2cExpr *mask = argument(expression, "mask", 2U);
    char *true_code;
    char *false_code;
    char *mask_code;
    Buffer result = {0};
    if (true_source == NULL || false_source == NULL || mask == NULL || true_source->rank != 0U ||
        false_source->rank != 0U || mask->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    true_code = f2c_expression_emit(unit, true_source, supported);
    false_code = *supported ? f2c_expression_emit(unit, false_source, supported) : NULL;
    mask_code = *supported ? f2c_expression_emit(unit, mask, supported) : NULL;
    if (!*supported || true_code == NULL || false_code == NULL || mask_code == NULL) {
        free(true_code);
        free(false_code);
        free(mask_code);
        *supported = 0;
        return NULL;
    }
    if (expression->type == TYPE_CHARACTER) {
        char *character = emit_character_merge(unit, true_source, false_source, true_code,
                                               false_code, mask_code, supported);
        free(true_code);
        free(false_code);
        free(mask_code);
        return character;
    }
    if (expression->type == TYPE_DERIVED && true_source->definable && false_source->definable)
        f2c_buffer_printf(&result, "(*((bool)(%s) ? &(%s) : &(%s)))", mask_code, true_code,
                          false_code);
    else
        f2c_buffer_printf(&result, "((bool)(%s) ? (%s) : (%s))", mask_code, true_code, false_code);
    free(true_code);
    free(false_code);
    free(mask_code);
    return f2c_buffer_take(&result);
}

char *f2c_expression_numeric_operation_intrinsic(Unit *unit, const F2cExpr *expression,
                                                 int *supported) {
    int64_t integer_value;
    double real_value;
    char *constant;
    if (unit == NULL || expression == NULL || supported == NULL || expression->rank != 0U ||
        !f2c_intrinsic_is_numeric_operation(expression->intrinsic)) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    if ((expression->type == TYPE_INTEGER || expression->type == TYPE_LOGICAL) &&
        f2c_evaluate_integer_constant(unit, expression, &integer_value)) {
        if (expression->type == TYPE_LOGICAL)
            return f2c_strdup(integer_value != 0 ? "true" : "false");
        constant = integer_constant(integer_value, expression_kind(expression));
        if (constant != NULL)
            return constant;
    }
    if ((expression->type == TYPE_REAL || expression->type == TYPE_DOUBLE) &&
        f2c_evaluate_real_constant(unit, expression, &real_value)) {
        constant = f2c_real_constant_literal(real_value, expression_kind(expression));
        if (constant != NULL)
            return constant;
    }
    if (expression->intrinsic == F2C_INTRINSIC_AINT || expression->intrinsic == F2C_INTRINSIC_ANINT)
        return emit_rounding_real(unit, expression, supported);
    if (expression->intrinsic == F2C_INTRINSIC_CEILING ||
        expression->intrinsic == F2C_INTRINSIC_FLOOR || expression->intrinsic == F2C_INTRINSIC_NINT)
        return emit_rounding_integer(unit, expression, supported);
    if (expression->intrinsic == F2C_INTRINSIC_MERGE)
        return emit_merge(unit, expression, supported);
    return emit_binary(unit, expression, supported);
}
