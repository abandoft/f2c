#include "codegen/expression/private.h"

#include <stdlib.h>
#include <string.h>

static const F2cExpr *argument(const F2cExpr *expression, const char *name, size_t position) {
    return f2c_intrinsic_argument(expression->children, expression->child_count, name, position);
}

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static int is_complex(Type type) { return type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX; }

static const char *real_component_function(Type type) {
    return type == TYPE_COMPLEX ? "crealf" : type == TYPE_DOUBLE_COMPLEX ? "creal" : NULL;
}

static char *emit_real_component(Unit *unit, const F2cExpr *source, const char *result_type,
                                 int *supported) {
    const char *component = real_component_function(source->type);
    char *code = f2c_expression_emit(unit, source, supported);
    Buffer result = {0};
    if (!*supported || code == NULL) {
        free(code);
        return NULL;
    }
    if (component != NULL)
        f2c_buffer_printf(&result, "((%s)%s(%s))", result_type, component, code);
    else
        f2c_buffer_printf(&result, "((%s)(%s))", result_type, code);
    free(code);
    return f2c_buffer_take(&result);
}

static char *emit_aimag(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *source = argument(expression, "z", 0U);
    const char *function;
    char *code;
    Buffer result = {0};
    if (source == NULL || source->rank != 0U || !is_complex(source->type)) {
        *supported = 0;
        return NULL;
    }
    function = source->type == TYPE_COMPLEX ? "cimagf" : "cimag";
    code = f2c_expression_emit(unit, source, supported);
    if (!*supported || code == NULL) {
        free(code);
        return NULL;
    }
    f2c_buffer_printf(&result, "%s(%s)", function, code);
    free(code);
    return f2c_buffer_take(&result);
}

static char *emit_conjg(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *source = argument(expression, "z", 0U);
    const char *function;
    char *code;
    Buffer result = {0};
    if (source == NULL || source->rank != 0U || !is_complex(source->type)) {
        *supported = 0;
        return NULL;
    }
    function = source->type == TYPE_COMPLEX ? "conjf" : "conj";
    code = f2c_expression_emit(unit, source, supported);
    if (!*supported || code == NULL) {
        free(code);
        return NULL;
    }
    f2c_buffer_printf(&result, "%s(%s)", function, code);
    free(code);
    return f2c_buffer_take(&result);
}

static char *emit_int(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *source = argument(expression, "a", 0U);
    const int kind = expression_kind(expression);
    const char *result_type = f2c_expression_c_type(expression);
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
        f2c_buffer_printf(&result, "((%s)f2c_checked_integer_from_i64((int64_t)(%s), %d))",
                          result_type, code, kind);
    } else {
        const char *component = real_component_function(source->type);
        if (component != NULL)
            f2c_buffer_printf(&result, "((%s)f2c_int_integer((double)%s(%s), %d))", result_type,
                              component, code, kind);
        else
            f2c_buffer_printf(&result, "((%s)f2c_int_integer((double)(%s), %d))", result_type, code,
                              kind);
    }
    free(code);
    return f2c_buffer_take(&result);
}

static char *emit_real(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *source = argument(expression, "a", 0U);
    if (source == NULL || source->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    return emit_real_component(unit, source, f2c_expression_c_type(expression), supported);
}

static char *emit_cmplx(Unit *unit, const F2cExpr *expression, int *supported) {
    const int specific = strcmp(expression->text, "dcmplx") == 0;
    const F2cExpr *real_source = argument(expression, "x", 0U);
    const F2cExpr *imaginary_source = argument(expression, "y", 1U);
    const int wide = expression_kind(expression) == 8;
    const char *component_type = wide ? "double" : "float";
    char *real_code;
    char *imaginary_code = NULL;
    Buffer result = {0};
    (void)specific;
    if (real_source == NULL || real_source->rank != 0U ||
        (imaginary_source != NULL && imaginary_source->rank != 0U)) {
        *supported = 0;
        return NULL;
    }
    real_code = f2c_expression_emit(unit, real_source, supported);
    if (imaginary_source != NULL && *supported)
        imaginary_code = f2c_expression_emit(unit, imaginary_source, supported);
    if (!*supported || real_code == NULL || (imaginary_source != NULL && imaginary_code == NULL)) {
        free(real_code);
        free(imaginary_code);
        return NULL;
    }
    if (is_complex(real_source->type) && imaginary_source == NULL) {
        if (wide && real_source->type == TYPE_COMPLEX)
            f2c_buffer_printf(&result, "f2c_c_to_z(%s)", real_code);
        else if (!wide && real_source->type == TYPE_DOUBLE_COMPLEX)
            f2c_buffer_printf(&result, "f2c_z_to_c(%s)", real_code);
        else
            f2c_buffer_append(&result, real_code);
    } else {
        f2c_buffer_printf(&result, "%s((%s)(%s), (%s)(%s))", wide ? "f2c_make_z" : "f2c_make_c",
                          component_type, real_code, component_type,
                          imaginary_code != NULL ? imaginary_code : "0");
    }
    free(real_code);
    free(imaginary_code);
    return f2c_buffer_take(&result);
}

char *f2c_expression_conversion_intrinsic(Unit *unit, const F2cExpr *expression, int *supported) {
    if (unit == NULL || expression == NULL || supported == NULL || expression->rank != 0U ||
        !f2c_intrinsic_is_conversion(expression->intrinsic)) {
        if (supported != NULL)
            *supported = 0;
        return NULL;
    }
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_AIMAG:
        return emit_aimag(unit, expression, supported);
    case F2C_INTRINSIC_CMPLX:
        return emit_cmplx(unit, expression, supported);
    case F2C_INTRINSIC_CONJG:
        return emit_conjg(unit, expression, supported);
    case F2C_INTRINSIC_DBLE:
    case F2C_INTRINSIC_REAL:
        return emit_real(unit, expression, supported);
    case F2C_INTRINSIC_INT:
        return emit_int(unit, expression, supported);
    case F2C_INTRINSIC_NONE:
    default:
        *supported = 0;
        return NULL;
    }
}
