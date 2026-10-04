#include "core/numeric/shift.h"
#include "semantic/constant/transform/private.h"

#include <stdlib.h>
#include <string.h>

static int slice_conformable(const F2cShape *array, const F2cShape *slice, size_t axis) {
    if (slice->rank == 0U)
        return 1;
    if (slice->rank + 1U != array->rank)
        return 0;
    for (size_t dimension = 0U, position = 0U; dimension < array->rank; ++dimension)
        if (dimension != axis &&
            (!slice->dimensions[position].extent_known ||
             slice->dimensions[position++].extent != array->dimensions[dimension].extent))
            return 0;
    return 1;
}

static int default_boundary(const F2cConstantArray *source, F2cConstantValue *value) {
    value->type = source->type;
    if (source->type.type == TYPE_DERIVED || source->type.type == TYPE_UNKNOWN)
        return 0;
    if (source->type.type == TYPE_CHARACTER) {
        if (source->character_length == SIZE_MAX)
            return 0;
        value->payload.character.bytes = (char *)malloc(source->character_length + 1U);
        if (value->payload.character.bytes == NULL)
            return 0;
        value->payload.character.length = source->character_length;
        memset(value->payload.character.bytes, ' ', source->character_length);
        value->payload.character.bytes[source->character_length] = '\0';
    }
    return 1;
}

int f2c_constant_transform_shift(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                 F2cConstantArray *result, size_t depth) {
    const int circular = call->intrinsic == F2C_INTRINSIC_CSHIFT;
    const F2cExpr *dimension_expression = f2c_constant_argument(call, "dim", circular ? 2U : 3U);
    const F2cExpr *boundary_expression =
        circular ? NULL : f2c_constant_argument(call, "boundary", 2U);
    F2cConstantArray source = {0}, shift = {0}, boundary = {0}, output = {0};
    F2cConstantValue fallback = {0};
    int64_t dimension = 1;
    size_t stride = 1U;
    int success = 0, fallback_ready = 0;
    if (!f2c_constant_evaluate_array(evaluation, f2c_constant_argument(call, "array", 0U), &source,
                                     depth + 1U) ||
        source.shape.rank == 0U ||
        (dimension_expression != NULL &&
         !f2c_constant_integer_argument(evaluation, dimension_expression, &dimension,
                                        depth + 1U)) ||
        dimension < 1 || (uint64_t)dimension > source.shape.rank)
        goto cleanup;
    const size_t axis = (size_t)dimension - 1U;
    if (!f2c_constant_evaluate_array(evaluation, f2c_constant_argument(call, "shift", 1U), &shift,
                                     depth + 1U) ||
        shift.type.type != TYPE_INTEGER || !slice_conformable(&source.shape, &shift.shape, axis))
        goto cleanup;
    if (boundary_expression != NULL &&
        (!f2c_constant_evaluate_array(evaluation, boundary_expression, &boundary, depth + 1U) ||
         !f2c_constant_same_element(&source, &boundary) ||
         !slice_conformable(&source.shape, &boundary.shape, axis)))
        goto cleanup;
    if (!circular && boundary_expression == NULL && source.type.type == TYPE_DERIVED)
        goto cleanup;
    f2c_constant_result_model(&output, &source);
    output.shape.rank = source.shape.rank;
    for (size_t index = 0U; index < source.shape.rank; ++index)
        f2c_constant_shape_dimension(&output.shape, index, source.shape.dimensions[index].extent);
    if (!f2c_constant_array_allocate(evaluation, &output, source.count))
        goto cleanup;
    if (source.count == 0U) {
        success = 1;
        goto cleanup;
    }
    for (size_t index = 0U; index < axis; ++index)
        stride *= (size_t)source.shape.dimensions[index].extent;
    const size_t extent = (size_t)source.shape.dimensions[axis].extent;
    const size_t span = stride * extent;
    for (size_t index = 0U; index < source.count; ++index) {
        const size_t slice = index % stride + (index / span) * stride;
        const size_t position = (index / stride) % extent;
        const int64_t amount = shift.values[shift.shape.rank == 0U ? 0U : slice].payload.integer;
        const F2cConstantValue *value;
        size_t selected;
        if (f2c_array_shift_position(position, extent, amount, circular, &selected)) {
            value = &source.values[index - position * stride + selected * stride];
        } else if (boundary_expression != NULL) {
            value = &boundary.values[boundary.shape.rank == 0U ? 0U : slice];
        } else {
            if (!fallback_ready) {
                if (!default_boundary(&source, &fallback))
                    goto cleanup;
                fallback_ready = 1;
            }
            value = &fallback;
        }
        if (!f2c_constant_value_copy(&output.values[index], value))
            goto cleanup;
    }
    success = 1;
cleanup:
    f2c_constant_value_free(&fallback);
    f2c_constant_array_free(&source);
    f2c_constant_array_free(&shift);
    f2c_constant_array_free(&boundary);
    if (!success)
        f2c_constant_array_free(&output);
    else
        *result = output;
    return success;
}
