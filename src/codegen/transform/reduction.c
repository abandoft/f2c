#include "codegen/transform/private.h"

#include <stdlib.h>

static int is_location(F2cIntrinsicId intrinsic) {
    return intrinsic == F2C_INTRINSIC_MAXLOC || intrinsic == F2C_INTRINSIC_MINLOC;
}

static int is_logical_reduction(F2cIntrinsicId intrinsic) {
    return intrinsic == F2C_INTRINSIC_ALL || intrinsic == F2C_INTRINSIC_ANY ||
           intrinsic == F2C_INTRINSIC_COUNT;
}

static const char *source_argument_name(F2cIntrinsicId intrinsic) {
    return is_logical_reduction(intrinsic) ? "mask" : "array";
}

static char *numeric_identity(const F2cExpr *call, int product, int maximum, int minimum) {
    const int kind = call->type_kind != 0 ? call->type_kind : f2c_default_kind(call->type);
    if (call->type == TYPE_COMPLEX)
        return f2c_strdup(product ? "f2c_make_c(1.0f, 0.0f)" : "f2c_make_c(0.0f, 0.0f)");
    if (call->type == TYPE_DOUBLE_COMPLEX)
        return f2c_strdup(product ? "f2c_make_z(1.0, 0.0)" : "f2c_make_z(0.0, 0.0)");
    if (call->type == TYPE_REAL)
        return f2c_strdup(maximum   ? "-HUGE_VALF"
                          : minimum ? "HUGE_VALF"
                          : product ? "1.0f"
                                    : "0.0f");
    if (call->type == TYPE_DOUBLE)
        return f2c_strdup(maximum ? "-HUGE_VAL" : minimum ? "HUGE_VAL" : product ? "1.0" : "0.0");
    if (call->type == TYPE_INTEGER) {
        if (maximum)
            return f2c_strdup(kind == 1   ? "INT8_MIN"
                              : kind == 2 ? "INT16_MIN"
                              : kind == 4 ? "INT32_MIN"
                                          : "INT64_MIN");
        if (minimum)
            return f2c_strdup(kind == 1   ? "INT8_MAX"
                              : kind == 2 ? "INT16_MAX"
                              : kind == 4 ? "INT32_MAX"
                                          : "INT64_MAX");
        return f2c_strdup(product ? "1" : "0");
    }
    return NULL;
}

static char *mask_condition(const TransformArray *mask, int scalar_mask) {
    Buffer result = {0};
    if (scalar_mask)
        return f2c_strdup("f2c_transform_mask_scalar");
    if (mask->expression == NULL)
        return f2c_strdup("true");
    f2c_buffer_printf(&result,
                      "f2c_reduction_logical_at((const void *)(%s), sizeof(*(%s)), "
                      "(ptrdiff_t)f2c_reduction_index)",
                      mask->pointer, mask->pointer);
    return f2c_buffer_take(&result);
}

static char *result_value(const F2cExpr *call, const Symbol *target, const char *value) {
    char *converted;
    Buffer result = {0};
    if (call->type == TYPE_INTEGER) {
        const int kind = call->type_kind != 0 ? call->type_kind : f2c_default_kind(TYPE_INTEGER);
        f2c_buffer_printf(&result, "((%s)f2c_reduction_integer_result((int64_t)(%s), %d))",
                          f2c_symbol_c_type(target), value, kind);
        return f2c_buffer_take(&result);
    }
    if (call->type == TYPE_LOGICAL) {
        f2c_buffer_printf(&result, "((%s)(%s))", f2c_symbol_c_type(target), value);
        return f2c_buffer_take(&result);
    }
    converted = f2c_emit_numeric_conversion(value, call->type, target->type);
    if (converted == NULL)
        return NULL;
    f2c_buffer_printf(&result, "((%s)(%s))", f2c_symbol_c_type(target), converted);
    free(converted);
    return f2c_buffer_take(&result);
}

static int emit_result_extents(Context *context, size_t source_rank, size_t result_rank,
                               int depth) {
    size_t dimension;
    for (dimension = 0U; dimension < result_rank; ++dimension) {
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transform_result_extent_%zu = "
                          "f2c_transform_dimension > %zu ? "
                          "f2c_transform_source_extent_%zu : "
                          "f2c_transform_source_extent_%zu;\n",
                          dimension + 1U, dimension + 1U, dimension + 1U, dimension + 2U);
    }
    return result_rank + 1U == source_rank;
}

static void emit_extent_vector(Context *context, size_t rank, int depth) {
    size_t dimension;
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_extents[%zu] = {", rank);
    for (dimension = 0U; dimension < rank; ++dimension)
        f2c_buffer_printf(&context->output, "%sf2c_transform_source_extent_%zu",
                          dimension == 0U ? "" : ", ", dimension + 1U);
    f2c_buffer_append(&context->output, "};\n");
}

static void emit_accumulator_update(Buffer *output, const F2cExpr *call,
                                    const char *source_pointer) {
    switch (call->intrinsic) {
    case F2C_INTRINSIC_SUM:
        if (call->type == TYPE_COMPLEX)
            f2c_buffer_printf(output,
                              "f2c_reduction_accumulator = "
                              "f2c_cadd(f2c_reduction_accumulator, "
                              "%s[f2c_reduction_index]); ",
                              source_pointer);
        else if (call->type == TYPE_DOUBLE_COMPLEX)
            f2c_buffer_printf(output,
                              "f2c_reduction_accumulator = "
                              "f2c_zadd(f2c_reduction_accumulator, "
                              "%s[f2c_reduction_index]); ",
                              source_pointer);
        else
            f2c_buffer_printf(output,
                              "f2c_reduction_accumulator = (%s)(f2c_reduction_accumulator + "
                              "%s[f2c_reduction_index]); ",
                              f2c_expression_c_type(call), source_pointer);
        break;
    case F2C_INTRINSIC_PRODUCT:
        if (call->type == TYPE_COMPLEX)
            f2c_buffer_printf(output,
                              "f2c_reduction_accumulator = "
                              "f2c_cmul(f2c_reduction_accumulator, "
                              "%s[f2c_reduction_index]); ",
                              source_pointer);
        else if (call->type == TYPE_DOUBLE_COMPLEX)
            f2c_buffer_printf(output,
                              "f2c_reduction_accumulator = "
                              "f2c_zmul(f2c_reduction_accumulator, "
                              "%s[f2c_reduction_index]); ",
                              source_pointer);
        else
            f2c_buffer_printf(output,
                              "f2c_reduction_accumulator = (%s)(f2c_reduction_accumulator * "
                              "%s[f2c_reduction_index]); ",
                              f2c_expression_c_type(call), source_pointer);
        break;
    case F2C_INTRINSIC_MAXVAL:
        f2c_buffer_printf(output,
                          "if (%s[f2c_reduction_index] > f2c_reduction_accumulator) "
                          "f2c_reduction_accumulator = %s[f2c_reduction_index]; ",
                          source_pointer, source_pointer);
        break;
    case F2C_INTRINSIC_MINVAL:
        f2c_buffer_printf(output,
                          "if (%s[f2c_reduction_index] < f2c_reduction_accumulator) "
                          "f2c_reduction_accumulator = %s[f2c_reduction_index]; ",
                          source_pointer, source_pointer);
        break;
    case F2C_INTRINSIC_ALL:
        f2c_buffer_printf(output,
                          "f2c_reduction_accumulator = f2c_reduction_accumulator && "
                          "f2c_reduction_logical_at((const void *)(%s), sizeof(*(%s)), "
                          "(ptrdiff_t)f2c_reduction_index); ",
                          source_pointer, source_pointer);
        break;
    case F2C_INTRINSIC_ANY:
        f2c_buffer_printf(output,
                          "f2c_reduction_accumulator = f2c_reduction_accumulator || "
                          "f2c_reduction_logical_at((const void *)(%s), sizeof(*(%s)), "
                          "(ptrdiff_t)f2c_reduction_index); ",
                          source_pointer, source_pointer);
        break;
    case F2C_INTRINSIC_COUNT:
        f2c_buffer_printf(output,
                          "if (f2c_reduction_logical_at((const void *)(%s), sizeof(*(%s)), "
                          "(ptrdiff_t)f2c_reduction_index)) ++f2c_reduction_accumulator; ",
                          source_pointer, source_pointer);
        break;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_DOT_PRODUCT:
    case F2C_INTRINSIC_MAXLOC:
    case F2C_INTRINSIC_MINLOC:
    default:
        break;
    }
}

static int emit_dimensional_reduction(Context *context, Unit *unit, Symbol *target,
                                      const F2cExpr *call, const TransformArray *source,
                                      const TransformArray *mask, const char *condition,
                                      int depth) {
    const int location = is_location(call->intrinsic);
    const int maximum = call->intrinsic == F2C_INTRINSIC_MAXVAL;
    const int minimum = call->intrinsic == F2C_INTRINSIC_MINVAL;
    const int product = call->intrinsic == F2C_INTRINSIC_PRODUCT;
    const char *accumulator_type =
        call->type == TYPE_LOGICAL ? "bool"
        : call->type == TYPE_INTEGER && call->intrinsic == F2C_INTRINSIC_COUNT
            ? "int64_t"
            : f2c_expression_c_type(call);
    char *identity = call->type == TYPE_LOGICAL
                         ? f2c_strdup(call->intrinsic == F2C_INTRINSIC_ALL ? "true" : "false")
                     : call->intrinsic == F2C_INTRINSIC_COUNT
                         ? f2c_strdup("INT64_C(0)")
                         : numeric_identity(call, product, maximum, minimum);
    char *store = result_value(call, target,
                               location ? "f2c_reduction_location" : "f2c_reduction_accumulator");
    if (identity == NULL || store == NULL) {
        free(identity);
        free(store);
        return 0;
    }
    emit_extent_vector(context, source->rank, depth);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(
        &context->output,
        "for (size_t f2c_reduction_output = 0U; "
        "f2c_reduction_output < f2c_transform_result_count; ++f2c_reduction_output) { "
        "size_t f2c_reduction_base = 0U, f2c_reduction_source_stride = 1U, "
        "f2c_reduction_result_stride = 1U, f2c_reduction_selected_stride = 1U; "
        "for (size_t f2c_reduction_axis = 0U; f2c_reduction_axis < %zuU; "
        "++f2c_reduction_axis) { if (f2c_transform_dimension == "
        "(int32_t)(f2c_reduction_axis + 1U)) "
        "f2c_reduction_selected_stride = f2c_reduction_source_stride; else { "
        "size_t f2c_reduction_coordinate = "
        "(f2c_reduction_output / f2c_reduction_result_stride) %% "
        "f2c_transform_extents[f2c_reduction_axis]; "
        "f2c_reduction_base += f2c_reduction_coordinate * f2c_reduction_source_stride; "
        "f2c_reduction_result_stride *= f2c_transform_extents[f2c_reduction_axis]; } "
        "f2c_reduction_source_stride *= f2c_transform_extents[f2c_reduction_axis]; } ",
        source->rank);
    if (location) {
        f2c_buffer_append(&context->output, "int64_t f2c_reduction_location = INT64_C(0); "
                                            "size_t f2c_reduction_selected_index = 0U; "
                                            "bool f2c_reduction_found = false; ");
    } else {
        f2c_buffer_printf(&context->output, "%s f2c_reduction_accumulator = %s; ", accumulator_type,
                          identity);
    }
    f2c_buffer_append(&context->output,
                      "size_t f2c_reduction_selected_extent = "
                      "f2c_transform_extents[(size_t)"
                      "f2c_transform_dimension - 1U]; "
                      "for (size_t f2c_reduction_coordinate = 0U; "
                      "f2c_reduction_coordinate < f2c_reduction_selected_extent; "
                      "++f2c_reduction_coordinate) { "
                      "size_t f2c_reduction_index = f2c_reduction_base + "
                      "f2c_reduction_coordinate * f2c_reduction_selected_stride; ");
    f2c_buffer_printf(&context->output, "if (%s) { ", condition);
    if (location) {
        const char *comparison = call->intrinsic == F2C_INTRINSIC_MAXLOC ? ">" : "<";
        f2c_buffer_printf(&context->output,
                          "if (!f2c_reduction_found || %s[f2c_reduction_index] %s "
                          "%s[f2c_reduction_selected_index] || "
                          "(f2c_transform_back && %s[f2c_reduction_index] == "
                          "%s[f2c_reduction_selected_index])) { "
                          "f2c_reduction_selected_index = f2c_reduction_index; "
                          "f2c_reduction_location = "
                          "(int64_t)f2c_reduction_coordinate + INT64_C(1); "
                          "f2c_reduction_found = true; } ",
                          source->pointer, comparison, source->pointer, source->pointer,
                          source->pointer);
    } else {
        emit_accumulator_update(&context->output, call, source->pointer);
    }
    f2c_buffer_printf(&context->output, "} } f2c_transform_result[f2c_reduction_output] = %s; }\n",
                      store);
    (void)unit;
    (void)mask;
    free(identity);
    free(store);
    return 1;
}

static int emit_global_location(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                                const TransformArray *source, const char *condition, int depth) {
    const char *comparison = call->intrinsic == F2C_INTRINSIC_MAXLOC ? ">" : "<";
    char *store = result_value(call, target, "f2c_reduction_location");
    if (store == NULL)
        return 0;
    emit_extent_vector(context, source->rank, depth);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_result_extent_1 = %zuU;\n",
                      source->rank);
    f2c_transform_emit_result_count(context, 1U, depth);
    f2c_transform_emit_result_allocation(context, unit, target, NULL, depth);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_reduction_selected_index = 0U; "
                                        "bool f2c_reduction_found = false; "
                                        "for (size_t f2c_reduction_index = 0U; "
                                        "f2c_reduction_index < f2c_transform_source_count; "
                                        "++f2c_reduction_index) { ");
    f2c_buffer_printf(&context->output,
                      "if ((%s) && (!f2c_reduction_found || %s[f2c_reduction_index] %s "
                      "%s[f2c_reduction_selected_index] || "
                      "(f2c_transform_back && %s[f2c_reduction_index] == "
                      "%s[f2c_reduction_selected_index]))) { "
                      "f2c_reduction_selected_index = f2c_reduction_index; "
                      "f2c_reduction_found = true; } }\n",
                      condition, source->pointer, comparison, source->pointer, source->pointer,
                      source->pointer);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "size_t f2c_reduction_coordinate_stride = 1U; "
                      "for (size_t f2c_reduction_dimension = 0U; f2c_reduction_dimension < "
                      "f2c_transform_result_count; ++f2c_reduction_dimension) { "
                      "int64_t f2c_reduction_location = f2c_reduction_found ? "
                      "(int64_t)((f2c_reduction_selected_index / "
                      "f2c_reduction_coordinate_stride) "
                      "% f2c_transform_extents[f2c_reduction_dimension]) + INT64_C(1) : "
                      "INT64_C(0); ");
    f2c_buffer_printf(&context->output,
                      "f2c_transform_result[f2c_reduction_dimension] = %s; "
                      "f2c_reduction_coordinate_stride *= "
                      "f2c_transform_extents[f2c_reduction_dimension]; }\n",
                      store);
    free(store);
    return 1;
}

int f2c_transform_emit_reduction(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                                 size_t line, int depth) {
    TransformArray source = {0};
    TransformArray mask = {0};
    const F2cExpr *source_expression =
        f2c_transform_argument(call, source_argument_name(call->intrinsic), 0U);
    const F2cExpr *dimension_expression = f2c_transform_argument(call, "dim", 1U);
    const F2cExpr *mask_expression =
        is_logical_reduction(call->intrinsic) ? NULL : f2c_transform_argument(call, "mask", 2U);
    const F2cExpr *back_expression =
        is_location(call->intrinsic) ? f2c_transform_argument(call, "back", 4U) : NULL;
    const int scalar_mask = mask_expression != NULL && mask_expression->rank == 0U;
    char *dimension = dimension_expression != NULL
                          ? f2c_transform_emit_expression(unit, dimension_expression)
                          : NULL;
    char *mask_scalar =
        scalar_mask ? f2c_transform_emit_expression(unit, mask_expression) : f2c_strdup("true");
    char *back = back_expression != NULL ? f2c_transform_emit_expression(unit, back_expression)
                                         : f2c_strdup("false");
    char *condition = NULL;
    size_t source_dimension;
    if (!f2c_intrinsic_is_reduction(call->intrinsic) ||
        call->intrinsic == F2C_INTRINSIC_DOT_PRODUCT ||
        !f2c_transform_array_view(unit, source_expression, &source) || target->rank != call->rank ||
        call->rank == 0U || mask_scalar == NULL || back == NULL ||
        (dimension_expression != NULL && dimension == NULL) ||
        (mask_expression != NULL && !scalar_mask &&
         (!f2c_transform_array_view(unit, mask_expression, &mask) || mask.type != TYPE_LOGICAL ||
          mask.rank != source.rank)) ||
        (dimension_expression == NULL ? !is_location(call->intrinsic) || call->rank != 1U
                                      : call->rank + 1U != source.rank)) {
        f2c_diagnostic(context, line, 1,
                       "array-valued reduction requires a conforming source, MASK, DIM, and "
                       "result rank");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&mask);
        free(dimension);
        free(mask_scalar);
        free(back);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_transform_materialize_array(context, unit, &source, "reduction_source", depth + 1) ||
        (mask.expression != NULL &&
         !f2c_transform_materialize_array(context, unit, &mask, "reduction_mask", depth + 1))) {
        f2c_diagnostic(context, line, 1, "reduction array expression could not be materialized");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&mask);
        free(dimension);
        free(mask_scalar);
        free(back);
        return 1;
    }
    f2c_transform_emit_source_extents(context, &source, depth + 1);
    if (mask.expression != NULL) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if ((size_t)(%s) != "
                          "f2c_transform_source_count) abort();\n",
                          mask.count);
        for (source_dimension = 0U; source_dimension < source.rank; ++source_dimension) {
            f2c_transform_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if ((size_t)(%s) != f2c_transform_source_extent_%zu) abort();\n",
                              mask.extents[source_dimension], source_dimension + 1U);
        }
    }
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_transform_mask_scalar = (%s) != 0;\n",
                      mask_scalar);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_transform_back = (%s) != 0;\n", back);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "(void)f2c_transform_mask_scalar; (void)f2c_transform_back;\n");
    condition = mask_condition(&mask, scalar_mask);
    if (condition == NULL) {
        f2c_diagnostic(context, line, 1, "reduction MASK expression could not be lowered");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&mask);
        free(dimension);
        free(mask_scalar);
        free(back);
        return 1;
    }
    if (dimension_expression != NULL) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const int32_t f2c_transform_dimension = (int32_t)(%s); "
                          "if (f2c_transform_dimension < 1 || "
                          "f2c_transform_dimension > %zu) abort();\n",
                          dimension, source.rank);
        if (!emit_result_extents(context, source.rank, target->rank, depth + 1)) {
            f2c_diagnostic(context, line, 1, "reduction result rank is inconsistent with DIM");
        } else {
            f2c_transform_emit_result_count(context, target->rank, depth + 1);
            f2c_transform_emit_result_allocation(context, unit, target, NULL, depth + 1);
            (void)emit_dimensional_reduction(context, unit, target, call, &source, &mask, condition,
                                             depth + 1);
        }
    } else {
        (void)emit_global_location(context, unit, target, call, &source, condition, depth + 1);
    }
    f2c_transform_emit_array_cleanup(context, &source, depth + 1);
    f2c_transform_emit_array_cleanup(context, &mask, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, target->rank, depth + 1);
    f2c_transform_free_array(&source);
    f2c_transform_free_array(&mask);
    free(dimension);
    free(mask_scalar);
    free(back);
    free(condition);
    return 1;
}
