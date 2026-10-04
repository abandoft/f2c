#include "semantic/constant/transform/private.h"

static int reshape(F2cConstantEvaluation *evaluation, const F2cExpr *call, F2cConstantArray *source,
                   F2cConstantArray *result, size_t depth) {
    F2cConstantArray shape = {0}, pad = {0}, order = {0};
    const F2cExpr *pad_expression = f2c_constant_argument(call, "pad", 2U);
    const F2cExpr *order_expression = f2c_constant_argument(call, "order", 3U);
    size_t axes[F2C_MAX_RANK], strides[F2C_MAX_RANK];
    size_t count, stride = 1U;
    int success = 0;
    if (source->shape.rank == 0U ||
        !f2c_constant_evaluate_array(evaluation, f2c_constant_argument(call, "shape", 1U), &shape,
                                     depth + 1U) ||
        shape.type.type != TYPE_INTEGER || shape.shape.rank != 1U || shape.count == 0U ||
        shape.count > F2C_MAX_RANK)
        goto cleanup;
    result->shape.rank = shape.count;
    for (size_t dimension = 0U; dimension < shape.count; ++dimension) {
        const int64_t extent = shape.values[dimension].payload.integer;
        if (extent < 0)
            goto cleanup;
        f2c_constant_shape_dimension(&result->shape, dimension, (uint64_t)extent);
        axes[dimension] = dimension;
    }
    if (order_expression != NULL) {
        unsigned int seen = 0U;
        if (!f2c_constant_evaluate_array(evaluation, order_expression, &order, depth + 1U) ||
            order.type.type != TYPE_INTEGER ||
            !f2c_constant_conformable(&shape.shape, &order.shape))
            goto cleanup;
        for (size_t dimension = 0U; dimension < shape.count; ++dimension) {
            const int64_t axis = order.values[dimension].payload.integer;
            if (axis < 1 || (uint64_t)axis > shape.count ||
                (seen & (1U << ((unsigned int)axis - 1U))) != 0U)
                goto cleanup;
            axes[dimension] = (size_t)axis - 1U;
            seen |= 1U << ((unsigned int)axis - 1U);
        }
    }
    if (pad_expression != NULL &&
        (!f2c_constant_evaluate_array(evaluation, pad_expression, &pad, depth + 1U) ||
         pad.shape.rank == 0U || !f2c_constant_same_element(source, &pad)))
        goto cleanup;
    if (!f2c_constant_shape_count(&result->shape, &count) ||
        (count > source->count && pad.count == 0U) ||
        !f2c_constant_array_allocate(evaluation, result, count))
        goto cleanup;
    if (count == 0U) {
        success = 1;
        goto cleanup;
    }
    for (size_t dimension = 0U; dimension < result->shape.rank; ++dimension) {
        strides[dimension] = stride;
        stride *= (size_t)result->shape.dimensions[dimension].extent;
    }
    for (size_t sequence = 0U; sequence < count; ++sequence) {
        size_t remaining = sequence, offset = 0U;
        for (size_t dimension = 0U; dimension < result->shape.rank; ++dimension) {
            const size_t axis = axes[dimension];
            const size_t extent = (size_t)result->shape.dimensions[axis].extent;
            offset += (remaining % extent) * strides[axis];
            remaining /= extent;
        }
        const F2cConstantValue *value = sequence < source->count
                                            ? &source->values[sequence]
                                            : &pad.values[(sequence - source->count) % pad.count];
        if (!f2c_constant_value_copy(&result->values[offset], value))
            goto cleanup;
    }
    success = 1;
cleanup:
    f2c_constant_array_free(&shape);
    f2c_constant_array_free(&pad);
    f2c_constant_array_free(&order);
    return success;
}

static int spread(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                  const F2cConstantArray *source, F2cConstantArray *result, size_t depth) {
    int64_t dimension, copies;
    size_t count;
    if (source->shape.rank >= F2C_MAX_RANK ||
        !f2c_constant_integer_argument(evaluation, f2c_constant_argument(call, "dim", 1U),
                                       &dimension, depth + 1U) ||
        dimension < 1 || (uint64_t)dimension > source->shape.rank + 1U ||
        !f2c_constant_integer_argument(evaluation, f2c_constant_argument(call, "ncopies", 2U),
                                       &copies, depth + 1U))
        return 0;
    const size_t axis = (size_t)dimension - 1U;
    result->shape.rank = source->shape.rank + 1U;
    for (size_t target = 0U, input = 0U; target < result->shape.rank; ++target)
        f2c_constant_shape_dimension(&result->shape, target,
                                     target == axis ? (copies > 0 ? (uint64_t)copies : 0U)
                                                    : source->shape.dimensions[input++].extent);
    if (!f2c_constant_shape_count(&result->shape, &count) ||
        !f2c_constant_array_allocate(evaluation, result, count))
        return 0;
    for (size_t index = 0U; index < count; ++index) {
        size_t offset = 0U, stride = 1U, remaining = index;
        for (size_t target = 0U; target < result->shape.rank; ++target) {
            const size_t extent = (size_t)result->shape.dimensions[target].extent;
            const size_t coordinate = remaining % extent;
            remaining /= extent;
            if (target != axis) {
                offset += coordinate * stride;
                stride *= extent;
            }
        }
        if (!f2c_constant_value_copy(&result->values[index], &source->values[offset]))
            return 0;
    }
    return 1;
}

int f2c_constant_transform_reorder(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                   F2cConstantArray *result, size_t depth) {
    F2cConstantArray source = {0}, output = {0};
    const char *argument = call->intrinsic == F2C_INTRINSIC_TRANSPOSE ? "matrix" : "source";
    int success = 0;
    if (!f2c_constant_evaluate_array(evaluation, f2c_constant_argument(call, argument, 0U), &source,
                                     depth + 1U))
        goto cleanup;
    f2c_constant_result_model(&output, &source);
    if (call->intrinsic == F2C_INTRINSIC_RESHAPE)
        success = reshape(evaluation, call, &source, &output, depth + 1U);
    else if (call->intrinsic == F2C_INTRINSIC_SPREAD)
        success = spread(evaluation, call, &source, &output, depth + 1U);
    else if (source.shape.rank == 2U) {
        output.shape.rank = 2U;
        f2c_constant_shape_dimension(&output.shape, 0U, source.shape.dimensions[1].extent);
        f2c_constant_shape_dimension(&output.shape, 1U, source.shape.dimensions[0].extent);
        success = f2c_constant_array_allocate(evaluation, &output, source.count);
        const size_t rows = (size_t)source.shape.dimensions[0].extent;
        const size_t columns = (size_t)source.shape.dimensions[1].extent;
        for (size_t index = 0U; success && index < source.count; ++index)
            success = f2c_constant_value_copy(
                &output.values[index], &source.values[index / columns + (index % columns) * rows]);
    }
cleanup:
    f2c_constant_array_free(&source);
    if (!success)
        f2c_constant_array_free(&output);
    else
        *result = output;
    return success;
}
