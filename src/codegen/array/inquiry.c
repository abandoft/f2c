#include "codegen/array/private.h"

#include "codegen/descriptor/private.h"

#include <stdlib.h>
#include <string.h>

static const F2cExpr *argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static const F2cExpr *inquiry_array(const F2cExpr *call) {
    const char *keyword;
    size_t positional = 0U;
    size_t argument;
    if (call == NULL || call->text == NULL)
        return NULL;
    keyword = strcmp(call->text, "shape") == 0 ? "source" : "array";
    for (argument = 0U; argument < call->child_count; ++argument) {
        const F2cExpr *actual = call->children[argument];
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (actual->text != NULL && strcmp(actual->text, keyword) == 0)
                return argument_value(actual);
        } else if (positional++ == 0U) {
            return actual;
        }
    }
    return NULL;
}

static char *inquiry_lower(Unit *unit, const F2cExpr *array, size_t dimension) {
    if (array != NULL &&
        (array->kind == F2C_EXPR_NAME ||
         (array->kind == F2C_EXPR_COMPONENT && array->child_count == 1U)) &&
        array->symbol != NULL)
        return f2c_descriptor_dimension_lower(unit, array, dimension);
    return f2c_strdup("1");
}

static char *inquiry_extent(Unit *unit, const F2cExpr *call, const F2cExpr *array,
                            size_t dimension) {
    if (call != NULL && call->text != NULL && strcmp(call->text, "lbound") == 0 &&
        f2c_expression_is_whole_assumed_size(array) && dimension + 1U == array->rank)
        return f2c_strdup("1U");
    return f2c_array_expression_extent(unit, array, dimension);
}

char *f2c_array_inquiry_dimension(Unit *unit, const F2cExpr *call, size_t dimension) {
    const F2cExpr *array = inquiry_array(call);
    char *extent;
    char *lower;
    Buffer result = {0};
    if (unit == NULL || call == NULL || call->text == NULL || array == NULL ||
        dimension >= array->rank ||
        (strcmp(call->text, "shape") != 0 && strcmp(call->text, "lbound") != 0 &&
         strcmp(call->text, "ubound") != 0))
        return NULL;
    extent = inquiry_extent(unit, call, array, dimension);
    lower = inquiry_lower(unit, array, dimension);
    if (extent == NULL || lower == NULL) {
        free(extent);
        free(lower);
        return NULL;
    }
    f2c_buffer_printf(&result, "((%s)", f2c_expression_c_type(call));
    if (strcmp(call->text, "shape") == 0)
        f2c_buffer_printf(&result, "f2c_inquiry_size_integer((size_t)(%s), %d))", extent,
                          call->type_kind);
    else if (strcmp(call->text, "lbound") == 0)
        f2c_buffer_printf(&result,
                          "f2c_inquiry_bound_integer(f2c_inquiry_lower_bound("
                          "(int64_t)(%s), (size_t)(%s)), %d))",
                          lower, extent, call->type_kind);
    else
        f2c_buffer_printf(&result,
                          "f2c_inquiry_bound_integer(f2c_inquiry_upper("
                          "f2c_inquiry_lower_bound((int64_t)(%s), (size_t)(%s)), "
                          "(size_t)(%s)), %d))",
                          lower, extent, extent, call->type_kind);
    free(extent);
    free(lower);
    return f2c_buffer_take(&result);
}

char *f2c_array_inquiry_element(Unit *unit, const F2cExpr *call, const char *ordinal) {
    const F2cExpr *array = inquiry_array(call);
    const char *type;
    Buffer values = {0};
    Buffer result = {0};
    size_t dimension;
    if (unit == NULL || call == NULL || ordinal == NULL || array == NULL || array->rank == 0U ||
        call->rank != 1U)
        return NULL;
    type = f2c_expression_c_type(call);
    for (dimension = 0U; dimension < array->rank; ++dimension) {
        char *value = f2c_array_inquiry_dimension(unit, call, dimension);
        if (value == NULL) {
            free(values.data);
            return NULL;
        }
        f2c_buffer_printf(&values, "%s%s", dimension == 0U ? "" : ", ", value);
        free(value);
    }
    f2c_buffer_printf(&result,
                      "((size_t)(%s) < %zuU ? ((const %s[%zu]){%s})[(size_t)(%s)] : "
                      "(abort(), (%s)0))",
                      ordinal, array->rank, type, array->rank,
                      values.data != NULL ? values.data : "", ordinal, type);
    free(values.data);
    return f2c_buffer_take(&result);
}
