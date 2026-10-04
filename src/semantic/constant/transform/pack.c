#include "semantic/constant/transform/private.h"

static int selected(const F2cConstantArray *mask, size_t index) {
    return mask->values[mask->shape.rank == 0U ? 0U : index].payload.integer != 0;
}

int f2c_constant_transform_pack(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                F2cConstantArray *result, size_t depth) {
    const int unpack = call->intrinsic == F2C_INTRINSIC_UNPACK;
    F2cConstantArray input = {0}, mask = {0}, extra = {0}, output = {0};
    const F2cExpr *extra_expression = f2c_constant_argument(call, unpack ? "field" : "vector", 2U);
    size_t selected_count = 0U, count, used = 0U;
    int success = 0;
    if (!f2c_constant_evaluate_array(evaluation,
                                     f2c_constant_argument(call, unpack ? "vector" : "array", 0U),
                                     &input, depth + 1U) ||
        !f2c_constant_evaluate_array(evaluation, f2c_constant_argument(call, "mask", 1U), &mask,
                                     depth + 1U) ||
        input.shape.rank == 0U || mask.type.type != TYPE_LOGICAL ||
        (unpack ? (input.shape.rank != 1U || mask.shape.rank == 0U)
                : (mask.shape.rank != 0U && !f2c_constant_conformable(&input.shape, &mask.shape))))
        goto cleanup;
    const size_t candidates = unpack ? mask.count : input.count;
    for (size_t index = 0U; index < candidates; ++index)
        if (selected(&mask, index))
            ++selected_count;
    if (extra_expression != NULL &&
        (!f2c_constant_evaluate_array(evaluation, extra_expression, &extra, depth + 1U) ||
         !f2c_constant_same_element(&input, &extra)))
        goto cleanup;
    if (unpack) {
        if (extra_expression == NULL || input.count < selected_count ||
            (extra.shape.rank != 0U && !f2c_constant_conformable(&extra.shape, &mask.shape)))
            goto cleanup;
        output.shape = mask.shape;
        count = mask.count;
    } else {
        if (extra_expression != NULL && (extra.shape.rank != 1U || extra.count < selected_count))
            goto cleanup;
        count = extra_expression != NULL ? extra.count : selected_count;
        output.shape.rank = 1U;
        f2c_constant_shape_dimension(&output.shape, 0U, count);
    }
    f2c_constant_result_model(&output, &input);
    for (size_t dimension = 0U; dimension < output.shape.rank; ++dimension)
        f2c_constant_shape_dimension(&output.shape, dimension,
                                     output.shape.dimensions[dimension].extent);
    if (!f2c_constant_array_allocate(evaluation, &output, count))
        goto cleanup;
    for (size_t index = 0U; index < candidates; ++index) {
        const int take = selected(&mask, index);
        if (!unpack && !take)
            continue;
        const F2cConstantValue *value =
            unpack ? (take ? &input.values[used++]
                           : &extra.values[extra.shape.rank == 0U ? 0U : index])
                   : &input.values[index];
        const size_t target = unpack ? index : used++;
        if (!f2c_constant_value_copy(&output.values[target], value))
            goto cleanup;
    }
    if (!unpack)
        for (size_t index = used; index < count; ++index)
            if (!f2c_constant_value_copy(&output.values[index], &extra.values[index]))
                goto cleanup;
    success = 1;
cleanup:
    f2c_constant_array_free(&input);
    f2c_constant_array_free(&mask);
    f2c_constant_array_free(&extra);
    if (!success)
        f2c_constant_array_free(&output);
    else
        *result = output;
    return success;
}
