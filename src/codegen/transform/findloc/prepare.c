#include "codegen/transform/findloc/private.h"

#include "codegen/names.h"
#include "codegen/operator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int materialize(F2cFindloc *lowering, TransformArray *array, const char *preferred,
                       int depth) {
    static const char *const fixed[] = {"values", "count",   "bytes",  "element_length",
                                        "index",  "ordinal", "source", "destination"};
    const char *suffixes[8U + 2U * F2C_MAX_RANK];
    char dimensions[2U * F2C_MAX_RANK][48];
    size_t count = 0U;
    for (size_t index = 0U; index < 8U; ++index)
        suffixes[count++] = fixed[index];
    for (size_t index = 0U; index < array->rank; ++index) {
        (void)snprintf(dimensions[2U * index], sizeof(dimensions[0]), "extent_%zu", index + 1U);
        (void)snprintf(dimensions[2U * index + 1U], sizeof(dimensions[0]), "ordinal_%zu",
                       index + 1U);
        suffixes[count++] = dimensions[2U * index];
        suffixes[count++] = dimensions[2U * index + 1U];
    }
    char *family = f2c_codegen_local_family(lowering->unit, preferred, suffixes, count);
    if (family == NULL)
        return 0;
    /* A stored pointer can be noncontiguous, or backed by unaligned storage.
     * Elementize those views before the dense scan instead of ignoring strides. */
    if ((array->expression->storage_qualifiers & F2C_STORAGE_VOLATILE) != 0U ||
        (array->symbol != NULL &&
         (array->symbol->pointer || array->symbol->equivalence_unaligned))) {
        free(array->pointer);
        array->pointer = NULL;
    }
    const int result = f2c_transform_materialize_array(lowering->context, lowering->unit, array,
                                                       family + strlen("f2c_transform_"), depth);
    free(family);
    return result;
}

int f2c_findloc_prepare(F2cFindloc *lowering, int depth) {
    static const char *const roles[] = {"value",
                                        "value_length",
                                        "source_length",
                                        "back",
                                        "mask",
                                        "dimension",
                                        "extents",
                                        "source_count",
                                        "result_extents",
                                        "result_count",
                                        "result",
                                        "output",
                                        "axis",
                                        "base",
                                        "source_stride",
                                        "result_stride",
                                        "selected_stride",
                                        "selected_extent",
                                        "step",
                                        "coordinate",
                                        "index",
                                        "stride",
                                        "position",
                                        "conforms",
                                        "scalar_storage"};
    Context *context = lowering->context;
    Unit *unit = lowering->unit;
    const F2cExpr *source = f2c_transform_argument(lowering->call, "array", 0U);
    const F2cExpr *value = f2c_transform_argument(lowering->call, "value", 1U);
    const F2cExpr *dimension = f2c_transform_argument(lowering->call, "dim", 2U);
    const F2cExpr *mask = f2c_transform_argument(lowering->call, "mask", 3U);
    const F2cExpr *back = f2c_transform_argument(lowering->call, "back", 5U);
    F2cOperatorTyping typing;
    const F2cOperator operation = f2c_value_equality_operator(
        source != NULL ? source->type : TYPE_UNKNOWN, value != NULL ? value->type : TYPE_UNKNOWN);
    if (!f2c_transform_array_view(unit, source, &lowering->source) || value == NULL ||
        value->rank != 0U ||
        f2c_operator_typing(operation, 0, f2c_expression_scalar_type(source),
                            f2c_expression_scalar_type(value), &typing) != F2C_OPERATOR_VALID ||
        (mask != NULL && mask->rank != 0U &&
         (!f2c_transform_array_view(unit, mask, &lowering->mask) ||
          lowering->mask.rank != lowering->source.rank)))
        return 0;
    lowering->prefix =
        f2c_codegen_local_family(unit, "f2c_findloc", roles, sizeof(roles) / sizeof(roles[0]));
    if (lowering->prefix == NULL)
        return 0;
    const char *name = lowering->prefix;
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    ++depth;
    if (!materialize(lowering, &lowering->source, "f2c_transform_findloc_source", depth) ||
        (mask != NULL && mask->rank != 0U &&
         !materialize(lowering, &lowering->mask, "f2c_transform_findloc_mask", depth)))
        return 0;
    char *value_code = f2c_transform_emit_expression(unit, value);
    if (value->type == TYPE_CHARACTER && value_code != NULL) {
        char *pointer = f2c_character_source_pointer(unit, value, value_code);
        free(value_code);
        value_code = pointer;
    }
    char *back_code =
        back != NULL ? f2c_transform_emit_expression(unit, back) : f2c_strdup("false");
    char *dimension_code =
        dimension != NULL ? f2c_transform_emit_expression(unit, dimension) : NULL;
    char *mask_code =
        mask != NULL && mask->rank == 0U ? f2c_transform_emit_expression(unit, mask) : NULL;
    char *value_length =
        value->type == TYPE_CHARACTER ? f2c_character_length_expression(unit, value) : NULL;
    const int supported = value_code != NULL && back_code != NULL &&
                          (dimension == NULL || dimension_code != NULL) &&
                          (mask == NULL || mask->rank != 0U || mask_code != NULL) &&
                          (value->type != TYPE_CHARACTER ||
                           (value_length != NULL && lowering->source.element_length != NULL));
    if (supported) {
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const %s %s_value = (%s);\n",
                          value->type == TYPE_CHARACTER
                              ? "char *const"
                              : f2c_c_type_kind(value->type, value->type_kind),
                          name, value_code);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const bool %s_back = !!(%s);\n", name, back_code);
        if (dimension != NULL) {
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "const int64_t %s_dimension = (int64_t)(%s); "
                              "if (%s_dimension < 1 || %s_dimension > %zu) abort();\n",
                              name, dimension_code, name, name, lowering->source.rank);
        }
        if (mask_code != NULL) {
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "const bool %s_mask = !!(%s);\n", name, mask_code);
        }
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const size_t %s_extents[%zu] = {", name,
                          lowering->source.rank);
        for (size_t axis = 0U; axis < lowering->source.rank; ++axis)
            f2c_buffer_printf(&context->output, "%s(size_t)(%s)", axis == 0U ? "" : ", ",
                              lowering->source.extents[axis]);
        f2c_buffer_append(&context->output, "};\n");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "const size_t %s_source_count = f2c_inquiry_size(%zuU, %s_extents);\n",
                          name, lowering->source.rank, name);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "(void)%s_source_count;\n", name);
        if (mask != NULL && mask->rank != 0U)
            for (size_t axis = 0U; axis < lowering->source.rank; ++axis) {
                f2c_transform_indent(&context->output, depth);
                f2c_buffer_printf(&context->output,
                                  "if ((size_t)(%s) != %s_extents[%zu]) abort();\n",
                                  lowering->mask.extents[axis], name, axis);
            }
        if (value_length != NULL) {
            f2c_transform_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "const size_t %s_value_length = (size_t)(%s); "
                              "const size_t %s_source_length = (size_t)(%s);\n",
                              name, value_length, name, lowering->source.element_length);
        }
    }
    free(value_code);
    free(back_code);
    free(dimension_code);
    free(mask_code);
    free(value_length);
    if (!supported)
        return 0;
    Buffer condition = {0}, element = {0}, cached_value = {0}, comparison = {0};
    if (mask == NULL)
        f2c_buffer_append(&condition, "true");
    else if (mask->rank == 0U)
        f2c_buffer_printf(&condition, "%s_mask", name);
    else
        f2c_buffer_printf(&condition, "(%s[%s_index] != 0)", lowering->mask.pointer, name);
    f2c_buffer_printf(&cached_value, "%s_value", name);
    if (source->type == TYPE_CHARACTER) {
        f2c_buffer_printf(&comparison,
                          "(f2c_character_compare(%s + %s_index * %s_source_length, "
                          "%s_source_length, %s_value, %s_value_length) == 0)",
                          lowering->source.pointer, name, name, name, name, name);
        lowering->comparison = f2c_buffer_take(&comparison);
    } else {
        f2c_buffer_printf(&element, "%s[%s_index]", lowering->source.pointer, name);
        lowering->comparison = f2c_emit_scalar_operator(
            operation, 0, (F2cScalarOperand){element.data, f2c_expression_scalar_type(source)},
            (F2cScalarOperand){cached_value.data, f2c_expression_scalar_type(value)},
            typing.result);
    }
    lowering->condition = f2c_buffer_take(&condition);
    free(element.data);
    free(cached_value.data);
    return lowering->condition != NULL && lowering->comparison != NULL;
}
