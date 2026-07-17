#include "codegen/transform/private.h"

#include <stdlib.h>

int f2c_transform_emit_findloc(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                               size_t line, int depth) {
    TransformArray source = {0};
    TransformArray mask_array = {0};
    const F2cExpr *value_expression = f2c_transform_argument(call, "value", 1U);
    const F2cExpr *dim_expression = f2c_transform_argument(call, "dim", 2U);
    const F2cExpr *mask = f2c_transform_argument(call, "mask", 3U);
    const F2cExpr *mask_value = f2c_transform_argument_value(mask);
    const F2cExpr *back_expression = f2c_transform_argument(call, "back", 5U);
    char *value = f2c_transform_emit_expression(unit, value_expression);
    char *back = back_expression != NULL ? f2c_transform_emit_expression(unit, back_expression)
                                         : f2c_strdup("false");
    char *dimension_code =
        dim_expression != NULL ? f2c_transform_emit_expression(unit, dim_expression) : NULL;
    char *condition = NULL;
    char *comparison = NULL;
    size_t dimension;
    if (target->type != TYPE_INTEGER ||
        !f2c_transform_array_view(unit, f2c_transform_argument(call, "array", 0U), &source) ||
        value == NULL || back == NULL ||
        (mask_value != NULL &&
         (mask_value->type != TYPE_LOGICAL ||
          (mask_value->rank != 0U && (!f2c_transform_array_view(unit, mask_value, &mask_array) ||
                                      mask_array.rank != source.rank)))) ||
        (dim_expression == NULL
             ? target->rank != 1U
             : source.rank < 2U || target->rank + 1U != source.rank || dimension_code == NULL)) {
        f2c_diagnostic(context, line, 1,
                       "FINDLOC requires stored ARRAY, scalar VALUE/BACK, conforming MASK, and a "
                       "result rank selected by DIM");
        f2c_transform_free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        f2c_transform_free_array(&mask_array);
        return 1;
    }
    if (source.type == TYPE_DERIVED) {
        f2c_diagnostic(context, line, 1,
                       "FINDLOC does not accept a derived-type ARRAY without defined equality");
        f2c_transform_free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        f2c_transform_free_array(&mask_array);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_transform_materialize_array(context, unit, &source, "findloc_source", depth + 1) ||
        (mask_value != NULL && mask_value->rank != 0U &&
         !f2c_transform_materialize_array(context, unit, &mask_array, "findloc_mask", depth + 1))) {
        f2c_diagnostic(context, line, 1, "FINDLOC ARRAY expression could not be materialized");
        f2c_transform_free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        f2c_transform_free_array(&mask_array);
        return 1;
    }
    if (mask_value == NULL) {
        condition = f2c_strdup("true");
    } else if (mask_value->rank == 0U) {
        condition = f2c_transform_mask_test(unit, mask, "index");
    } else {
        Buffer expression = {0};
        f2c_buffer_printf(&expression, "%s[index]", mask_array.pointer);
        condition = f2c_buffer_take(&expression);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if ((size_t)(%s) != (size_t)(%s)) abort();\n",
                          mask_array.count, source.count);
    }
    if (condition == NULL) {
        f2c_diagnostic(context, line, 1, "FINDLOC MASK expression could not be lowered");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&mask_array);
        free(value);
        free(back);
        free(dimension_code);
        return 1;
    }
    if (source.type == TYPE_CHARACTER) {
        char *value_length = f2c_character_length_expression(unit, value_expression);
        Buffer expression = {0};
        f2c_buffer_printf(&expression,
                          "f2c_character_compare(%s + index * (size_t)(%s), (size_t)(%s), "
                          "%s, (size_t)(%s)) == 0",
                          source.pointer,
                          source.element_length != NULL ? source.element_length : "0U",
                          source.element_length != NULL ? source.element_length : "0U", value,
                          value_length != NULL ? value_length : "0U");
        comparison = f2c_buffer_take(&expression);
        free(value_length);
    } else {
        Buffer expression = {0};
        f2c_buffer_printf(&expression, "%s[index] == (%s)", source.pointer, value);
        comparison = f2c_buffer_take(&expression);
    }
    if (comparison == NULL) {
        f2c_diagnostic(context, line, 1, "FINDLOC comparison could not be lowered");
        f2c_transform_free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        free(condition);
        f2c_transform_free_array(&mask_array);
        return 1;
    }
    f2c_transform_emit_source_extents(context, &source, depth + 1);
    if (dim_expression != NULL) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const int32_t f2c_transform_dimension = (int32_t)(%s); "
                          "if (f2c_transform_dimension < 1 || "
                          "f2c_transform_dimension > %zu) abort();\n",
                          dimension_code, source.rank);
        for (dimension = 0U; dimension < target->rank; ++dimension) {
            f2c_transform_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transform_result_extent_%zu = "
                              "f2c_transform_dimension > %zu ? "
                              "f2c_transform_source_extent_%zu : "
                              "f2c_transform_source_extent_%zu;\n",
                              dimension + 1U, dimension + 1U, dimension + 1U, dimension + 2U);
        }
        f2c_transform_emit_result_count(context, target->rank, depth + 1);
        f2c_transform_emit_result_allocation(context, unit, target, NULL, depth + 1);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_extents[%zu] = {",
                          source.rank);
        for (dimension = 0U; dimension < source.rank; ++dimension)
            f2c_buffer_printf(&context->output, "%sf2c_transform_source_extent_%zu",
                              dimension == 0U ? "" : ", ", dimension + 1U);
        f2c_buffer_append(&context->output, "};\n");
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(
            &context->output,
            "for (size_t output = 0U; output < f2c_transform_result_count; ++output) { "
            "size_t base = 0U, source_stride = 1U, result_stride = 1U, "
            "selected_stride = 1U; for (size_t d = 0U; d < %zuU; ++d) { "
            "if (f2c_transform_dimension == (int32_t)(d + 1U)) "
            "selected_stride = source_stride; else { size_t coordinate = "
            "(output / result_stride) %% f2c_transform_extents[d]; "
            "base += coordinate * source_stride; result_stride *= "
            "f2c_transform_extents[d]; } source_stride *= f2c_transform_extents[d]; } "
            "f2c_transform_result[output] = 0; size_t selected_extent = "
            "f2c_transform_extents[(size_t)f2c_transform_dimension - 1U]; "
            "for (size_t step = 0U; step < selected_extent; ++step) { "
            "size_t coordinate = (%s) ? selected_extent - 1U - step : step; "
            "size_t index = base + coordinate * selected_stride; "
            "if ((%s) && (%s)) { "
            "f2c_transform_result[output] = (int32_t)(coordinate + 1U); break; } } }\n",
            source.rank, back, condition, comparison);
        f2c_transform_emit_array_cleanup(context, &source, depth + 1);
        f2c_transform_emit_array_cleanup(context, &mask_array, depth + 1);
        f2c_transform_emit_result_commit(context, unit, target, target->rank, depth + 1);
        f2c_transform_free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        free(condition);
        free(comparison);
        f2c_transform_free_array(&mask_array);
        return 1;
    }
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_result_extent_1 = %zuU;\n",
                      source.rank);
    f2c_transform_emit_result_count(context, 1U, depth + 1);
    f2c_transform_emit_result_allocation(context, unit, target, NULL, depth + 1);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "for (size_t d = 0U; d < f2c_transform_result_count; ++d) "
                                        "f2c_transform_result[d] = 0;\n");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t step = 0U; step < f2c_transform_source_count; ++step) { "
                      "size_t index = (%s) ? f2c_transform_source_count - 1U - step : step; "
                      "if ((%s) && (%s)) { size_t stride = 1U; ",
                      back, condition, comparison);
    for (dimension = 0U; dimension < source.rank; ++dimension)
        f2c_buffer_printf(&context->output,
                          "f2c_transform_result[%zu] = (int32_t)((index / stride) %% "
                          "f2c_transform_source_extent_%zu) + 1; stride *= "
                          "f2c_transform_source_extent_%zu; ",
                          dimension, dimension + 1U, dimension + 1U);
    f2c_buffer_append(&context->output, "break; } }\n");
    f2c_transform_emit_array_cleanup(context, &source, depth + 1);
    f2c_transform_emit_array_cleanup(context, &mask_array, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, 1U, depth + 1);
    f2c_transform_free_array(&source);
    free(value);
    free(back);
    free(dimension_code);
    free(condition);
    free(comparison);
    f2c_transform_free_array(&mask_array);
    return 1;
}
