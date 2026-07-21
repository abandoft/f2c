#include "codegen/transform/private.h"

#include "codegen/array/private.h"

#include <stdlib.h>
#include <string.h>

char *f2c_transform_inquiry_element(Unit *unit, const F2cExpr *value, size_t index) {
    return f2c_array_inquiry_dimension(unit, value, index);
}

int f2c_transform_emit_inquiry(Context *context, Unit *unit, const F2cExpr *left,
                               const F2cExpr *call, size_t line, int depth) {
    Symbol *target = left != NULL ? left->symbol : NULL;
    const F2cExpr *array =
        f2c_transform_argument(call, strcmp(call->text, "shape") == 0 ? "source" : "array", 0U);
    char *values[F2C_MAX_RANK] = {0};
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
        values[dimension] = f2c_array_inquiry_dimension(unit, call, dimension);
        if (values[dimension] == NULL) {
            size_t cleanup;
            for (cleanup = 0U; cleanup <= dimension; ++cleanup)
                free(values[cleanup]);
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
    for (dimension = 0U; dimension < array->rank; ++dimension) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "f2c_inquiry_result[%zuU] = (%s)f2c_inquiry_bound_integer("
                          "(int64_t)(%s), %d);\n",
                          dimension, f2c_symbol_c_type(target), values[dimension],
                          target->kind != 0 ? target->kind : 4);
    }

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
    for (dimension = 0U; dimension < array->rank; ++dimension)
        free(values[dimension]);
    return 1;
}
