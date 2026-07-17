#include "codegen/transform/private.h"

#include "codegen/array/private.h"

#include <stdlib.h>
#include <string.h>

static char *inquiry_lower(Unit *unit, const F2cExpr *array, size_t dimension) {
    if (array->kind == F2C_EXPR_NAME && array->symbol != NULL)
        return f2c_symbol_dimension_lower(unit, array->symbol, dimension);
    return f2c_strdup("1");
}

char *f2c_transform_inquiry_element(Unit *unit, const F2cExpr *value, size_t index) {
    const F2cExpr *array =
        f2c_transform_argument(value, strcmp(value->text, "shape") == 0 ? "source" : "array", 0U);
    char *extent;
    char *lower;
    Buffer result = {0};
    if (array == NULL || index >= array->rank)
        return NULL;
    extent = f2c_array_expression_extent(unit, array, index);
    lower = inquiry_lower(unit, array, index);
    if (extent == NULL || lower == NULL) {
        free(extent);
        free(lower);
        return NULL;
    }
    f2c_buffer_printf(&result, "((%s)", f2c_expression_c_type(value));
    if (strcmp(value->text, "shape") == 0)
        f2c_buffer_printf(&result, "f2c_inquiry_size_integer((size_t)(%s), %d))", extent,
                          value->type_kind);
    else if (strcmp(value->text, "lbound") == 0)
        f2c_buffer_printf(&result,
                          "f2c_inquiry_bound_integer(f2c_inquiry_lower_bound("
                          "(int64_t)(%s), (size_t)(%s)), %d))",
                          lower, extent, value->type_kind);
    else
        f2c_buffer_printf(&result,
                          "f2c_inquiry_bound_integer(f2c_inquiry_upper("
                          "f2c_inquiry_lower_bound((int64_t)(%s), (size_t)(%s)), "
                          "(size_t)(%s)), %d))",
                          lower, extent, extent, value->type_kind);
    free(extent);
    free(lower);
    return f2c_buffer_take(&result);
}

static void emit_inquiry_value(Context *context, const Symbol *target, const F2cExpr *call,
                               const char *extent, const char *lower, size_t dimension, int depth) {
    const int result_kind = call->type_kind != 0 ? call->type_kind : 4;
    const int target_kind = target->kind != 0 ? target->kind : 4;
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "f2c_inquiry_result[%zuU] = (%s)", dimension,
                      f2c_symbol_c_type(target));
    if (strcmp(call->text, "shape") == 0) {
        f2c_buffer_printf(&context->output,
                          "f2c_inquiry_bound_integer(f2c_inquiry_size_integer((size_t)(%s), "
                          "%d), %d);\n",
                          extent, result_kind, target_kind);
    } else if (strcmp(call->text, "lbound") == 0) {
        f2c_buffer_printf(&context->output,
                          "f2c_inquiry_bound_integer(f2c_inquiry_bound_integer("
                          "f2c_inquiry_lower_bound((int64_t)(%s), (size_t)(%s)), %d), %d);\n",
                          lower, extent, result_kind, target_kind);
    } else {
        f2c_buffer_printf(&context->output,
                          "f2c_inquiry_bound_integer(f2c_inquiry_bound_integer("
                          "f2c_inquiry_upper(f2c_inquiry_lower_bound((int64_t)(%s), "
                          "(size_t)(%s)), (size_t)(%s)), %d), %d);\n",
                          lower, extent, extent, result_kind, target_kind);
    }
}

int f2c_transform_emit_inquiry(Context *context, Unit *unit, const F2cExpr *left,
                               const F2cExpr *call, size_t line, int depth) {
    Symbol *target = left != NULL ? left->symbol : NULL;
    const F2cExpr *array =
        f2c_transform_argument(call, strcmp(call->text, "shape") == 0 ? "source" : "array", 0U);
    char *extents[F2C_MAX_RANK] = {0};
    char *lowers[F2C_MAX_RANK] = {0};
    size_t dimension;
    if (target == NULL || left->kind != F2C_EXPR_NAME || target->type != TYPE_INTEGER ||
        target->rank != 1U || array == NULL || array->rank == 0U || array->rank > F2C_MAX_RANK ||
        call->rank != 1U) {
        f2c_diagnostic(context, line, 1,
                       "%s array result requires a whole rank-one INTEGER target and a "
                       "non-scalar source",
                       call->text);
        return 1;
    }
    for (dimension = 0U; dimension < array->rank; ++dimension) {
        extents[dimension] = f2c_array_expression_extent(unit, array, dimension);
        lowers[dimension] = inquiry_lower(unit, array, dimension);
        if (extents[dimension] == NULL || lowers[dimension] == NULL) {
            size_t cleanup;
            for (cleanup = 0U; cleanup <= dimension; ++cleanup) {
                free(extents[cleanup]);
                free(lowers[cleanup]);
            }
            f2c_diagnostic(context, line, 1, "%s source shape is not available in typed IR",
                           call->text);
            return 1;
        }
    }

    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_inquiry_count = %zuU;\n", array->rank);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "%s *f2c_inquiry_result = (%s *)malloc("
                      "f2c_inquiry_count * sizeof(*f2c_inquiry_result));\n",
                      f2c_symbol_c_type(target), f2c_symbol_c_type(target));
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_inquiry_result == NULL) abort();\n");
    for (dimension = 0U; dimension < array->rank; ++dimension)
        emit_inquiry_value(context, target, call, extents[dimension], lowers[dimension], dimension,
                           depth + 1);

    if (target->allocatable) {
        const char *name = f2c_symbol_c_name(unit, target);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "free(%s);\n", name);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_inquiry_result;\n", name);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "%s_lower_1 = 1; %s_extent_1 = (int32_t)f2c_inquiry_count;\n", name,
                          name);
    } else {
        const char *name = f2c_symbol_c_name(unit, target);
        char *target_extent = f2c_symbol_dimension_extent(unit, target, 0U);
        f2c_transform_indent(&context->output, depth + 1);
        if (target->pointer)
            f2c_buffer_printf(&context->output, "if (%s == NULL) abort();\n", name);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if ((size_t)(%s) != f2c_inquiry_count) abort();\n",
                          target_extent != NULL ? target_extent : "0U");
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "memmove(%s, f2c_inquiry_result, "
                          "f2c_inquiry_count * sizeof(*f2c_inquiry_result));\n",
                          name);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "free(f2c_inquiry_result);\n");
        free(target_extent);
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    for (dimension = 0U; dimension < array->rank; ++dimension) {
        free(extents[dimension]);
        free(lowers[dimension]);
    }
    return 1;
}
