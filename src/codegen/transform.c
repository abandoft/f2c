#include "codegen/transform/private.h"

#include "codegen/lowering/private.h"

#include <stdlib.h>
#include <string.h>

static char *vector_element(Unit *unit, const F2cExpr *vector, size_t index) {
    const F2cExpr *value = f2c_transform_argument_value(vector);
    const char *lowered_code;
    if (value == NULL)
        return NULL;
    lowered_code = f2c_lowering_code(unit, value);
    if (lowered_code != NULL && value->rank == 1U) {
        Buffer result = {0};
        f2c_buffer_printf(&result, "%s[%zuU]", lowered_code, index);
        return f2c_buffer_take(&result);
    }
    if (value->kind == F2C_EXPR_CALL && value->rank == 1U &&
        (value->intrinsic == F2C_INTRINSIC_SHAPE ||
         value->intrinsic == F2C_INTRINSIC_LBOUND ||
         value->intrinsic == F2C_INTRINSIC_UBOUND))
        return f2c_transform_inquiry_element(unit, value, index);
    if (value->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        if (index >= value->child_count)
            return NULL;
        return f2c_transform_emit_expression(unit, value->children[index]);
    }
    if (value->kind == F2C_EXPR_NAME && value->symbol != NULL && value->rank == 1U) {
        Buffer result = {0};
        f2c_buffer_printf(&result, "%s[%zuU]", f2c_symbol_c_name(unit, value->symbol), index);
        return f2c_buffer_take(&result);
    }
    return NULL;
}

static int emit_reshape(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                        size_t line, int depth) {
    const F2cExpr *source_expression = f2c_transform_argument(call, "source", 0U);
    const F2cExpr *shape = f2c_transform_argument(call, "shape", 1U);
    const F2cExpr *pad_expression = f2c_transform_argument(call, "pad", 2U);
    const F2cExpr *order = f2c_transform_argument(call, "order", 3U);
    TransformArray source = {0};
    TransformArray pad = {0};
    TransformArray shape_array = {0};
    TransformArray order_array = {0};
    size_t rank = call->rank;
    size_t dimension;
    char *shape_probe = vector_element(unit, shape, 0U);
    char *order_probe = order != NULL ? vector_element(unit, order, 0U) : NULL;
    const int materialize_shape = shape_probe == NULL;
    const int materialize_order = order != NULL && order_probe == NULL;
    free(shape_probe);
    free(order_probe);
    if (!f2c_transform_array_view(unit, source_expression, &source) ||
        !f2c_transform_compatible_array(target, &source) || rank == 0U || rank != target->rank ||
        (materialize_shape && (!f2c_transform_array_view(unit, shape, &shape_array) ||
                               shape_array.type != TYPE_INTEGER || shape_array.rank != 1U)) ||
        (materialize_order && (!f2c_transform_array_view(unit, order, &order_array) ||
                               order_array.type != TYPE_INTEGER || order_array.rank != 1U)) ||
        (pad_expression != NULL && (!f2c_transform_array_view(unit, pad_expression, &pad) ||
                                    !f2c_transform_compatible_array(target, &pad)))) {
        f2c_diagnostic(context, line, 1,
                       "RESHAPE requires array SOURCE/PAD, a known SHAPE rank, and a conforming "
                       "target");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&pad);
        f2c_transform_free_array(&shape_array);
        f2c_transform_free_array(&order_array);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_transform_materialize_array(context, unit, &source, "reshape_source", depth + 1) ||
        (materialize_shape && !f2c_transform_materialize_array(context, unit, &shape_array,
                                                               "reshape_shape", depth + 1)) ||
        (materialize_order && !f2c_transform_materialize_array(context, unit, &order_array,
                                                               "reshape_order", depth + 1)) ||
        (pad_expression != NULL &&
         !f2c_transform_materialize_array(context, unit, &pad, "reshape_pad", depth + 1))) {
        f2c_diagnostic(context, line, 1, "RESHAPE array expression could not be materialized");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&pad);
        f2c_transform_free_array(&shape_array);
        f2c_transform_free_array(&order_array);
        return 1;
    }
    if (materialize_shape) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if ((size_t)(%s) != %zuU) abort();\n",
                          shape_array.count, rank);
    }
    if (materialize_order) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if ((size_t)(%s) != %zuU) abort();\n",
                          order_array.count, rank);
    }
    for (dimension = 0U; dimension < rank; ++dimension) {
        char *extent;
        if (materialize_shape) {
            Buffer item = {0};
            f2c_buffer_printf(&item, "%s[%zuU]", shape_array.pointer, dimension);
            extent = f2c_buffer_take(&item);
        } else {
            extent = vector_element(unit, shape, dimension);
        }
        if (extent == NULL) {
            f2c_diagnostic(context, line, 1, "RESHAPE SHAPE must provide every result extent");
            f2c_transform_free_array(&source);
            f2c_transform_free_array(&pad);
            f2c_transform_free_array(&shape_array);
            f2c_transform_free_array(&order_array);
            return 1;
        }
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const int64_t f2c_transform_shape_%zu = (int64_t)(%s);\n",
                          dimension + 1U, extent);
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_transform_shape_%zu < 0) abort();\n"
                          "%*sconst size_t f2c_transform_result_extent_%zu = "
                          "(size_t)f2c_transform_shape_%zu;\n",
                          dimension + 1U, (depth + 1) * 4, "", dimension + 1U, dimension + 1U);
        free(extent);
    }
    f2c_transform_emit_source_extents(context, &source, depth + 1);
    if (pad_expression != NULL) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_pad_count = %s;\n",
                          pad.count);
    }
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_order[%zu] = {", rank);
    for (dimension = 0U; dimension < rank; ++dimension) {
        char *item = NULL;
        if (materialize_order) {
            Buffer value = {0};
            f2c_buffer_printf(&value, "%s[%zuU]", order_array.pointer, dimension);
            item = f2c_buffer_take(&value);
        } else if (order != NULL) {
            item = vector_element(unit, order, dimension);
        }
        if (item != NULL)
            f2c_buffer_printf(&context->output, "%s(size_t)(%s)", dimension == 0U ? "" : ", ",
                              item);
        else
            f2c_buffer_printf(&context->output, "%s%zuU", dimension == 0U ? "" : ", ",
                              dimension + 1U);
        free(item);
    }
    f2c_buffer_append(&context->output, "};\n");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t f2c_transform_index = 0U; f2c_transform_index < %zuU; "
                      "++f2c_transform_index) { if (f2c_transform_order[f2c_transform_index] "
                      "< 1U || f2c_transform_order[f2c_transform_index] > %zuU) abort(); "
                      "for (size_t f2c_transform_prior = 0U; "
                      "f2c_transform_prior < f2c_transform_index; ++f2c_transform_prior) "
                      "if (f2c_transform_order[f2c_transform_index] == "
                      "f2c_transform_order[f2c_transform_prior]) abort(); }\n",
                      rank, rank);
    f2c_transform_emit_result_count(context, rank, depth + 1);
    f2c_transform_emit_result_allocation(context, unit, target, source_expression, depth + 1);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_transform_source_count < f2c_transform_result_count%s) abort();\n",
                      pad_expression != NULL ? " && f2c_transform_pad_count == 0U" : "");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t output = 0U; output < f2c_transform_result_count; ++output) { "
                      "size_t coordinates[%zu] = {0}; size_t stride = 1U; size_t sequence = 0U; "
                      "size_t multiplier = 1U; ",
                      rank);
    for (dimension = 0U; dimension < rank; ++dimension)
        f2c_buffer_printf(&context->output,
                          "coordinates[%zu] = (output / stride) %% "
                          "f2c_transform_result_extent_%zu; stride *= "
                          "f2c_transform_result_extent_%zu; ",
                          dimension, dimension + 1U, dimension + 1U);
    f2c_buffer_printf(&context->output,
                      "for (size_t k = 0U; k < %zuU; ++k) { size_t d = "
                      "f2c_transform_order[k] - 1U; sequence += coordinates[d] * multiplier; "
                      "multiplier *= ((const size_t[%zu]){",
                      rank, rank);
    for (dimension = 0U; dimension < rank; ++dimension)
        f2c_buffer_printf(&context->output, "%sf2c_transform_result_extent_%zu",
                          dimension == 0U ? "" : ", ", dimension + 1U);
    f2c_buffer_append(&context->output, "})[d]; } ");
    f2c_buffer_append(&context->output, "if (sequence < f2c_transform_source_count) { ");
    f2c_transform_append_array_store(&context->output, target, "output", &source, "sequence");
    f2c_buffer_append(&context->output, "} ");
    if (pad_expression != NULL) {
        f2c_buffer_append(&context->output, "else { ");
        f2c_transform_append_array_store(&context->output, target, "output", &pad,
                                         "(sequence - f2c_transform_source_count) % "
                                         "f2c_transform_pad_count");
        f2c_buffer_append(&context->output, "} ");
    }
    f2c_buffer_append(&context->output, "}\n");
    f2c_transform_emit_array_cleanup(context, &source, depth + 1);
    f2c_transform_emit_array_cleanup(context, &pad, depth + 1);
    f2c_transform_emit_array_cleanup(context, &shape_array, depth + 1);
    f2c_transform_emit_array_cleanup(context, &order_array, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, rank, depth + 1);
    f2c_transform_free_array(&source);
    f2c_transform_free_array(&pad);
    f2c_transform_free_array(&shape_array);
    f2c_transform_free_array(&order_array);
    return 1;
}

char *f2c_transform_mask_test(Unit *unit, const F2cExpr *mask, const char *index) {
    Buffer result = {0};
    const F2cExpr *value = f2c_transform_argument_value(mask);
    char *scalar;
    if (value == NULL || value->type != TYPE_LOGICAL)
        return NULL;
    if (value->rank == 0U) {
        scalar = f2c_transform_emit_expression(unit, value);
        if (scalar != NULL) {
            f2c_buffer_printf(&result, "(%s)", scalar);
            free(scalar);
        }
    } else if (value->kind == F2C_EXPR_NAME && value->symbol != NULL) {
        f2c_buffer_printf(&result, "%s[%s]", f2c_symbol_c_name(unit, value->symbol), index);
    }
    return f2c_buffer_take(&result);
}

static int emit_pack(Context *context, Unit *unit, Symbol *target, const F2cExpr *call, size_t line,
                     int depth) {
    TransformArray source = {0};
    TransformArray vector = {0};
    TransformArray mask_array = {0};
    const F2cExpr *mask = f2c_transform_argument(call, "mask", 1U);
    const F2cExpr *mask_value = f2c_transform_argument_value(mask);
    const F2cExpr *vector_expression = f2c_transform_argument(call, "vector", 2U);
    char *condition = NULL;
    if (target->rank != 1U ||
        !f2c_transform_array_view(unit, f2c_transform_argument(call, "array", 0U), &source) ||
        !f2c_transform_compatible_array(target, &source) || mask_value == NULL ||
        mask_value->type != TYPE_LOGICAL ||
        (mask_value->rank != 0U && (!f2c_transform_array_view(unit, mask_value, &mask_array) ||
                                    mask_array.rank != source.rank)) ||
        (vector_expression != NULL &&
         (!f2c_transform_array_view(unit, vector_expression, &vector) ||
          !f2c_transform_compatible_array(target, &vector)))) {
        f2c_diagnostic(context, line, 1, "PACK requires ARRAY/MASK and a rank-one result/VECTOR");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&vector);
        f2c_transform_free_array(&mask_array);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_transform_materialize_array(context, unit, &source, "pack_source", depth + 1) ||
        (mask_value->rank != 0U &&
         !f2c_transform_materialize_array(context, unit, &mask_array, "pack_mask", depth + 1)) ||
        (vector_expression != NULL &&
         !f2c_transform_materialize_array(context, unit, &vector, "pack_vector", depth + 1))) {
        f2c_diagnostic(context, line, 1, "PACK array expression could not be materialized");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&vector);
        f2c_transform_free_array(&mask_array);
        return 1;
    }
    if (mask_value->rank == 0U) {
        condition = f2c_transform_mask_test(unit, mask, "f2c_transform_index");
    } else {
        Buffer expression = {0};
        f2c_buffer_printf(&expression, "%s[f2c_transform_index]", mask_array.pointer);
        condition = f2c_buffer_take(&expression);
    }
    if (condition == NULL) {
        f2c_diagnostic(context, line, 1, "PACK MASK expression could not be lowered");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&vector);
        f2c_transform_free_array(&mask_array);
        return 1;
    }
    f2c_transform_emit_source_extents(context, &source, depth + 1);
    if (mask_value->rank != 0U) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if ((size_t)(%s) != f2c_transform_source_count) "
                          "abort();\n",
                          mask_array.count);
    }
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_transform_selected = 0U;\n");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t f2c_transform_index = 0U; "
                      "f2c_transform_index < f2c_transform_source_count; "
                      "++f2c_transform_index) if (%s) "
                      "++f2c_transform_selected;\n",
                      condition);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_result_extent_1 = %s;\n",
                      vector_expression != NULL ? vector.count : "f2c_transform_selected");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_transform_selected > f2c_transform_result_extent_1) abort();\n");
    f2c_transform_emit_result_count(context, 1U, depth + 1);
    f2c_transform_emit_result_allocation(context, unit, target,
                                         f2c_transform_argument(call, "array", 0U), depth + 1);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_transform_output = 0U;\n");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t f2c_transform_index = 0U; "
                      "f2c_transform_index < f2c_transform_source_count; "
                      "++f2c_transform_index) if (%s) { ",
                      condition);
    f2c_transform_append_array_store(&context->output, target, "f2c_transform_output", &source,
                                     "f2c_transform_index");
    f2c_buffer_append(&context->output, "++f2c_transform_output; }\n");
    if (vector_expression != NULL) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "while (f2c_transform_output < f2c_transform_result_count) { ");
        f2c_transform_append_array_store(&context->output, target, "f2c_transform_output", &vector,
                                         "f2c_transform_output");
        f2c_buffer_append(&context->output, "++f2c_transform_output; }\n");
    }
    f2c_transform_emit_array_cleanup(context, &source, depth + 1);
    f2c_transform_emit_array_cleanup(context, &vector, depth + 1);
    f2c_transform_emit_array_cleanup(context, &mask_array, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, 1U, depth + 1);
    free(condition);
    f2c_transform_free_array(&source);
    f2c_transform_free_array(&vector);
    f2c_transform_free_array(&mask_array);
    return 1;
}

static int emit_unpack(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                       size_t line, int depth) {
    TransformArray vector = {0};
    TransformArray mask = {0};
    TransformArray field_array = {0};
    const F2cExpr *field = f2c_transform_argument(call, "field", 2U);
    char *field_scalar = NULL;
    char *field_length = NULL;
    size_t dimension;
    if (!f2c_transform_array_view(unit, f2c_transform_argument(call, "vector", 0U), &vector) ||
        !f2c_transform_compatible_array(target, &vector) ||
        !f2c_transform_array_view(unit, f2c_transform_argument(call, "mask", 1U), &mask) ||
        mask.rank != target->rank) {
        f2c_diagnostic(context, line, 1, "UNPACK requires rank-one VECTOR and conforming MASK");
        f2c_transform_free_array(&vector);
        f2c_transform_free_array(&mask);
        return 1;
    }
    if (field != NULL && field->rank == 0U && field->type == target->type &&
        (target->type != TYPE_DERIVED || field->derived_type == target->derived_type))
        field_scalar = f2c_transform_emit_expression(unit, field);
    else if (!f2c_transform_array_view(unit, field, &field_array) ||
             !f2c_transform_compatible_array(target, &field_array) ||
             field_array.rank != mask.rank) {
        f2c_diagnostic(context, line, 1, "UNPACK FIELD must be scalar or conform with MASK");
        f2c_transform_free_array(&vector);
        f2c_transform_free_array(&mask);
        f2c_transform_free_array(&field_array);
        return 1;
    }
    if (field_scalar != NULL && target->type == TYPE_CHARACTER)
        field_length = f2c_character_length_expression(unit, field);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_transform_materialize_array(context, unit, &vector, "unpack_vector", depth + 1) ||
        !f2c_transform_materialize_array(context, unit, &mask, "unpack_mask", depth + 1) ||
        (field_scalar == NULL && !f2c_transform_materialize_array(context, unit, &field_array,
                                                                  "unpack_field", depth + 1))) {
        f2c_diagnostic(context, line, 1, "UNPACK array expression could not be materialized");
        free(field_scalar);
        free(field_length);
        f2c_transform_free_array(&vector);
        f2c_transform_free_array(&mask);
        f2c_transform_free_array(&field_array);
        return 1;
    }
    for (dimension = 0U; dimension < mask.rank; ++dimension) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_result_extent_%zu = %s;\n",
                          dimension + 1U, mask.extents[dimension]);
    }
    f2c_transform_emit_result_count(context, mask.rank, depth + 1);
    f2c_transform_emit_result_allocation(context, unit, target,
                                         f2c_transform_argument(call, "vector", 0U), depth + 1);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_transform_vector_index = 0U;\n");
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t f2c_transform_index = 0U; "
                      "f2c_transform_index < f2c_transform_result_count; "
                      "++f2c_transform_index) { if (%s[f2c_transform_index]) { "
                      "if (f2c_transform_vector_index >= (size_t)(%s)) abort(); ",
                      mask.pointer, vector.count);
    f2c_transform_append_array_store(&context->output, target, "f2c_transform_index", &vector,
                                     "f2c_transform_vector_index");
    f2c_buffer_append(&context->output, "++f2c_transform_vector_index; } else { ");
    if (field_scalar != NULL)
        f2c_transform_append_scalar_store(&context->output, target, "f2c_transform_index",
                                          field_scalar, field_length);
    else
        f2c_transform_append_array_store(&context->output, target, "f2c_transform_index",
                                         &field_array, "f2c_transform_index");
    f2c_buffer_append(&context->output, "} }\n");
    f2c_transform_emit_array_cleanup(context, &vector, depth + 1);
    f2c_transform_emit_array_cleanup(context, &mask, depth + 1);
    f2c_transform_emit_array_cleanup(context, &field_array, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, mask.rank, depth + 1);
    free(field_scalar);
    free(field_length);
    f2c_transform_free_array(&vector);
    f2c_transform_free_array(&mask);
    f2c_transform_free_array(&field_array);
    return 1;
}

static int emit_spread(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                       size_t line, int depth) {
    const F2cExpr *source_expression = f2c_transform_argument(call, "source", 0U);
    char *source_scalar = NULL;
    TransformArray source = {0};
    char *dimension_code =
        f2c_transform_emit_expression(unit, f2c_transform_argument(call, "dim", 1U));
    char *copies_code =
        f2c_transform_emit_expression(unit, f2c_transform_argument(call, "ncopies", 2U));
    size_t source_rank = source_expression != NULL ? source_expression->rank : 0U;
    size_t result_rank = source_rank + 1U;
    size_t dimension;
    char *source_length = NULL;
    if (source_rank == 0U)
        source_scalar = f2c_transform_emit_expression(unit, source_expression);
    else
        (void)f2c_transform_array_view(unit, source_expression, &source);
    if (result_rank != target->rank || source_expression == NULL ||
        source_expression->type != target->type ||
        (target->type == TYPE_DERIVED && source_expression->derived_type != target->derived_type) ||
        dimension_code == NULL || copies_code == NULL ||
        (source_rank == 0U ? source_scalar == NULL : source.count == NULL)) {
        f2c_diagnostic(context, line, 1,
                       "SPREAD requires SOURCE, scalar DIM/NCOPIES, and rank+1 target");
        free(source_scalar);
        free(dimension_code);
        free(copies_code);
        f2c_transform_free_array(&source);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (source_rank != 0U &&
        !f2c_transform_materialize_array(context, unit, &source, "spread_source", depth + 1)) {
        f2c_diagnostic(context, line, 1, "SPREAD array expression could not be materialized");
        free(source_scalar);
        free(dimension_code);
        free(copies_code);
        f2c_transform_free_array(&source);
        return 1;
    }
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const int32_t f2c_transform_dimension = (int32_t)(%s); "
                      "const int64_t f2c_transform_copies_value = (int64_t)(%s); "
                      "if (f2c_transform_dimension < 1 || f2c_transform_dimension > %zu || "
                      "f2c_transform_copies_value < 0) abort();\n",
                      dimension_code, copies_code, result_rank);
    if (source_rank != 0U)
        f2c_transform_emit_source_extents(context, &source, depth + 1);
    for (dimension = 0U; dimension < result_rank; ++dimension) {
        f2c_transform_indent(&context->output, depth + 1);
        if (source_rank == 0U) {
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transform_result_extent_%zu = "
                              "(size_t)f2c_transform_copies_value;\n",
                              dimension + 1U);
        } else if (dimension == 0U) {
            f2c_buffer_append(&context->output, "const size_t f2c_transform_result_extent_1 = "
                                                "f2c_transform_dimension == 1 ? "
                                                "(size_t)f2c_transform_copies_value : "
                                                "f2c_transform_source_extent_1;\n");
        } else if (dimension == source_rank) {
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transform_result_extent_%zu = "
                              "f2c_transform_dimension == %zu ? "
                              "(size_t)f2c_transform_copies_value : "
                              "f2c_transform_source_extent_%zu;\n",
                              dimension + 1U, dimension + 1U, dimension);
        } else {
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transform_result_extent_%zu = "
                              "f2c_transform_dimension == %zu ? "
                              "(size_t)f2c_transform_copies_value : "
                              "(f2c_transform_dimension > %zu ? "
                              "f2c_transform_source_extent_%zu : "
                              "f2c_transform_source_extent_%zu);\n",
                              dimension + 1U, dimension + 1U, dimension + 1U, dimension,
                              dimension + 1U);
        }
    }
    f2c_transform_emit_result_count(context, result_rank, depth + 1);
    if (source_rank == 0U && target->type == TYPE_CHARACTER)
        source_length = f2c_character_length_expression(unit, source_expression);
    f2c_transform_emit_result_allocation(context, unit, target, source_expression, depth + 1);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t output = 0U; output < f2c_transform_result_count; ++output) { "
                      "size_t source_index = 0U, source_stride = 1U, result_stride = 1U; ");
    for (dimension = 0U; dimension < result_rank; ++dimension) {
        f2c_buffer_printf(&context->output,
                          "{ size_t coordinate = (output / result_stride) %% "
                          "(f2c_transform_result_extent_%zu == 0U ? 1U : "
                          "f2c_transform_result_extent_%zu); "
                          "if (f2c_transform_dimension != %zu) "
                          "{ source_index += coordinate * source_stride; source_stride *= "
                          "f2c_transform_result_extent_%zu; } result_stride *= "
                          "f2c_transform_result_extent_%zu; } ",
                          dimension + 1U, dimension + 1U, dimension + 1U, dimension + 1U,
                          dimension + 1U);
    }
    if (source_rank == 0U)
        f2c_transform_append_scalar_store(&context->output, target, "output", source_scalar,
                                          source_length);
    else
        f2c_transform_append_array_store(&context->output, target, "output", &source,
                                         "source_index");
    f2c_buffer_append(&context->output, "}\n");
    f2c_transform_emit_array_cleanup(context, &source, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, result_rank, depth + 1);
    free(source_scalar);
    free(dimension_code);
    free(copies_code);
    free(source_length);
    f2c_transform_free_array(&source);
    return 1;
}

static char *slice_value(Unit *unit, const F2cExpr *expression, const TransformArray *array,
                         const char *slice, const char *fallback) {
    const F2cExpr *value = f2c_transform_argument_value(expression);
    Buffer result = {0};
    char *scalar;
    if (value == NULL)
        return f2c_strdup(fallback);
    if (value->rank == 0U) {
        scalar = f2c_transform_emit_expression(unit, value);
        return scalar;
    }
    if (array != NULL && array->pointer != NULL) {
        if (value->type == TYPE_CHARACTER)
            f2c_buffer_printf(&result, "%s + (%s) * (size_t)(%s)", array->pointer, slice,
                              array->element_length != NULL ? array->element_length : "0U");
        else
            f2c_buffer_printf(&result, "%s[%s]", array->pointer, slice);
        return f2c_buffer_take(&result);
    }
    return NULL;
}

static int emit_shift(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                      size_t line, int depth, int end_off) {
    TransformArray source = {0};
    TransformArray shift_array = {0};
    TransformArray boundary_array = {0};
    const F2cExpr *shift = f2c_transform_argument(call, "shift", 1U);
    const F2cExpr *shift_value_expression = f2c_transform_argument_value(shift);
    const F2cExpr *boundary = end_off ? f2c_transform_argument(call, "boundary", 2U) : NULL;
    const F2cExpr *boundary_value_expression = f2c_transform_argument_value(boundary);
    const F2cExpr *dim_argument = f2c_transform_argument(call, "dim", end_off ? 3U : 2U);
    char *dimension_code =
        dim_argument != NULL ? f2c_transform_emit_expression(unit, dim_argument) : f2c_strdup("1");
    const int need_slice = (shift != NULL && f2c_transform_argument_value(shift) != NULL &&
                            f2c_transform_argument_value(shift)->rank != 0U) ||
                           (boundary != NULL && f2c_transform_argument_value(boundary) != NULL &&
                            f2c_transform_argument_value(boundary)->rank != 0U);
    size_t dimension;
    if (!f2c_transform_array_view(unit, f2c_transform_argument(call, "array", 0U), &source) ||
        !f2c_transform_compatible_array(target, &source) || source.rank != target->rank ||
        dimension_code == NULL || shift_value_expression == NULL ||
        (shift_value_expression->rank != 0U &&
         (shift_value_expression->type != TYPE_INTEGER ||
          !f2c_transform_array_view(unit, shift_value_expression, &shift_array) ||
          shift_array.rank + 1U != source.rank)) ||
        (boundary_value_expression != NULL && boundary_value_expression->rank != 0U &&
         (!f2c_transform_array_view(unit, boundary_value_expression, &boundary_array) ||
          !f2c_transform_compatible_array(target, &boundary_array) ||
          boundary_array.rank + 1U != source.rank)) ||
        (end_off && target->type == TYPE_DERIVED && boundary == NULL)) {
        f2c_diagnostic(context, line, 1, "%s requires conforming ARRAY/result and scalar DIM",
                       end_off ? "EOSHIFT" : "CSHIFT");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&shift_array);
        f2c_transform_free_array(&boundary_array);
        free(dimension_code);
        return 1;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    if (!f2c_transform_materialize_array(context, unit, &source, "shift_source", depth + 1) ||
        (shift_value_expression->rank != 0U &&
         !f2c_transform_materialize_array(context, unit, &shift_array, "shift_values",
                                          depth + 1)) ||
        (boundary_value_expression != NULL && boundary_value_expression->rank != 0U &&
         !f2c_transform_materialize_array(context, unit, &boundary_array, "shift_boundary",
                                          depth + 1))) {
        f2c_diagnostic(context, line, 1, "%s ARRAY expression could not be materialized",
                       end_off ? "EOSHIFT" : "CSHIFT");
        f2c_transform_free_array(&source);
        f2c_transform_free_array(&shift_array);
        f2c_transform_free_array(&boundary_array);
        free(dimension_code);
        return 1;
    }
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(
        &context->output,
        "const int32_t f2c_transform_dimension = (int32_t)(%s); "
        "if (f2c_transform_dimension < 1 || f2c_transform_dimension > %zu) abort();\n",
        dimension_code, source.rank);
    f2c_transform_emit_source_extents(context, &source, depth + 1);
    if (shift_value_expression->rank != 0U) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_shift_extents[%zu] = {",
                          shift_array.rank);
        for (dimension = 0U; dimension < shift_array.rank; ++dimension)
            f2c_buffer_printf(&context->output, "%s%s", dimension == 0U ? "" : ", ",
                              shift_array.extents[dimension]);
        f2c_buffer_append(&context->output, "};\n");
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "for (size_t d = 0U, k = 0U; d < "
                                            "sizeof(f2c_transform_shift_extents) / "
                                            "sizeof(f2c_transform_shift_extents[0]) + 1U; ++d) "
                                            "if (d + 1U != (size_t)f2c_transform_dimension && "
                                            "f2c_transform_shift_extents[k++] != "
                                            "((const size_t[]){");
        for (dimension = 0U; dimension < source.rank; ++dimension)
            f2c_buffer_printf(&context->output, "%sf2c_transform_source_extent_%zu",
                              dimension == 0U ? "" : ", ", dimension + 1U);
        f2c_buffer_append(&context->output, "})[d]) abort();\n");
    }
    if (boundary_value_expression != NULL && boundary_value_expression->rank != 0U) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_boundary_extents[%zu] = {",
                          boundary_array.rank);
        for (dimension = 0U; dimension < boundary_array.rank; ++dimension)
            f2c_buffer_printf(&context->output, "%s%s", dimension == 0U ? "" : ", ",
                              boundary_array.extents[dimension]);
        f2c_buffer_append(&context->output, "};\n");
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "for (size_t d = 0U, k = 0U; d < "
                                            "sizeof(f2c_transform_boundary_extents) / "
                                            "sizeof(f2c_transform_boundary_extents[0]) + 1U; ++d) "
                                            "if (d + 1U != (size_t)f2c_transform_dimension && "
                                            "f2c_transform_boundary_extents[k++] != "
                                            "((const size_t[]){");
        for (dimension = 0U; dimension < source.rank; ++dimension)
            f2c_buffer_printf(&context->output, "%sf2c_transform_source_extent_%zu",
                              dimension == 0U ? "" : ", ", dimension + 1U);
        f2c_buffer_append(&context->output, "})[d]) abort();\n");
    }
    for (dimension = 0U; dimension < source.rank; ++dimension) {
        f2c_transform_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transform_result_extent_%zu = "
                          "f2c_transform_source_extent_%zu;\n",
                          dimension + 1U, dimension + 1U);
    }
    f2c_transform_emit_result_count(context, source.rank, depth + 1);
    f2c_transform_emit_result_allocation(context, unit, target,
                                         f2c_transform_argument(call, "array", 0U), depth + 1);
    f2c_transform_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t output = 0U; output < f2c_transform_result_count; ++output) { "
                      "size_t coordinates[%zu] = {0}; size_t stride = 1U; ",
                      source.rank);
    if (need_slice)
        f2c_buffer_append(&context->output, "size_t slice = 0U; size_t slice_stride = 1U; ");
    for (dimension = 0U; dimension < source.rank; ++dimension) {
        f2c_buffer_printf(&context->output,
                          "coordinates[%zu] = (output / stride) %% "
                          "f2c_transform_source_extent_%zu; stride *= "
                          "f2c_transform_source_extent_%zu; ",
                          dimension, dimension + 1U, dimension + 1U);
        if (need_slice)
            f2c_buffer_printf(&context->output,
                              "if (f2c_transform_dimension != %zu) "
                              "{ slice += coordinates[%zu] * slice_stride; slice_stride *= "
                              "f2c_transform_source_extent_%zu; } ",
                              dimension + 1U, dimension, dimension + 1U);
    }
    {
        char *shift_value = slice_value(unit, shift, &shift_array, "slice", "0");
        const char *boundary_fallback = target->type == TYPE_CHARACTER ? "\" \"" : "0";
        char *boundary_value =
            slice_value(unit, boundary, &boundary_array, "slice", boundary_fallback);
        char *boundary_length =
            target->type == TYPE_CHARACTER
                ? (boundary != NULL ? f2c_character_length_expression(
                                          unit, f2c_transform_argument_value(boundary))
                                    : f2c_strdup("1U"))
                : NULL;
        f2c_buffer_printf(&context->output,
                          "int64_t amount = (int64_t)(%s); size_t d = "
                          "(size_t)(f2c_transform_dimension - 1); int64_t extent = "
                          "(int64_t)((const size_t[%zu]){",
                          shift_value != NULL ? shift_value : "0", source.rank);
        for (dimension = 0U; dimension < source.rank; ++dimension)
            f2c_buffer_printf(&context->output, "%sf2c_transform_source_extent_%zu",
                              dimension == 0U ? "" : ", ", dimension + 1U);
        f2c_buffer_append(&context->output, "})[d]; int64_t f2c_shifted_index = "
                                            "(int64_t)coordinates[d] + amount; ");
        if (!end_off)
            f2c_buffer_append(&context->output,
                              "if (extent != 0) f2c_shifted_index = "
                              "((f2c_shifted_index % extent) + extent) % extent; ");
        f2c_buffer_append(&context->output,
                          "size_t source_index = 0U; size_t source_stride = 1U; ");
        for (dimension = 0U; dimension < source.rank; ++dimension)
            f2c_buffer_printf(&context->output,
                              "source_index += (d == %zuU ? "
                              "(size_t)(f2c_shifted_index < 0 ? 0 : f2c_shifted_index) : "
                              "coordinates[%zu]) * source_stride; source_stride *= "
                              "f2c_transform_source_extent_%zu; ",
                              dimension, dimension, dimension + 1U);
        if (end_off) {
            f2c_buffer_append(&context->output,
                              "if (f2c_shifted_index < 0 || f2c_shifted_index >= extent) { ");
            f2c_transform_append_scalar_store(
                &context->output, target, "output",
                boundary_value != NULL ? boundary_value : boundary_fallback, boundary_length);
            f2c_buffer_append(&context->output, "} else { ");
            f2c_transform_append_array_store(&context->output, target, "output", &source,
                                             "source_index");
            f2c_buffer_append(&context->output, "} ");
        } else {
            f2c_transform_append_array_store(&context->output, target, "output", &source,
                                             "source_index");
        }
        free(shift_value);
        free(boundary_value);
        free(boundary_length);
    }
    f2c_buffer_append(&context->output, "}\n");
    f2c_transform_emit_array_cleanup(context, &source, depth + 1);
    f2c_transform_emit_array_cleanup(context, &shift_array, depth + 1);
    f2c_transform_emit_array_cleanup(context, &boundary_array, depth + 1);
    f2c_transform_emit_result_commit(context, unit, target, source.rank, depth + 1);
    f2c_transform_free_array(&source);
    f2c_transform_free_array(&shift_array);
    f2c_transform_free_array(&boundary_array);
    free(dimension_code);
    return 1;
}

int f2c_emit_transform_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                  const F2cExpr *right, size_t line, int depth) {
    Symbol *target = left != NULL ? left->symbol : NULL;
    const char *name = right != NULL ? right->text : NULL;
    if (context == NULL || unit == NULL || target == NULL || right == NULL ||
        right->kind != F2C_EXPR_CALL)
        return 0;
    if (f2c_intrinsic_is_reduction(right->intrinsic) &&
        right->intrinsic != F2C_INTRINSIC_DOT_PRODUCT && right->rank != 0U)
        return f2c_transform_emit_reduction(context, unit, target, right, line, depth);
    if (right->intrinsic == F2C_INTRINSIC_SHAPE ||
        right->intrinsic == F2C_INTRINSIC_LBOUND ||
        right->intrinsic == F2C_INTRINSIC_UBOUND)
        return f2c_transform_emit_inquiry(context, unit, left, right, line, depth);
    if (!f2c_intrinsic_is_transformational(right->intrinsic))
        return 0;
    if (!f2c_transform_supported_element_type(target)) {
        f2c_diagnostic(context, line, 1, "%s result has an unsupported element type",
                       name != NULL ? name : "transformational intrinsic");
        return 1;
    }
    if (right->intrinsic == F2C_INTRINSIC_TRANSPOSE || right->intrinsic == F2C_INTRINSIC_MATMUL)
        return f2c_transform_emit_matrix(context, unit, target, right, line, depth);
    if (right->intrinsic == F2C_INTRINSIC_RESHAPE)
        return emit_reshape(context, unit, target, right, line, depth);
    if (right->intrinsic == F2C_INTRINSIC_PACK)
        return emit_pack(context, unit, target, right, line, depth);
    if (right->intrinsic == F2C_INTRINSIC_UNPACK)
        return emit_unpack(context, unit, target, right, line, depth);
    if (right->intrinsic == F2C_INTRINSIC_SPREAD)
        return emit_spread(context, unit, target, right, line, depth);
    if (right->intrinsic == F2C_INTRINSIC_CSHIFT)
        return emit_shift(context, unit, target, right, line, depth, 0);
    if (right->intrinsic == F2C_INTRINSIC_EOSHIFT)
        return emit_shift(context, unit, target, right, line, depth, 1);
    if (right->intrinsic == F2C_INTRINSIC_FINDLOC)
        return f2c_transform_emit_findloc(context, unit, target, right, line, depth);
    return 0;
}
