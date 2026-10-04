#include "semantic/constant/transform/private.h"

static int equal_value(const F2cConstantValue *left, const F2cConstantValue *right) {
    switch (left->type.type) {
    case TYPE_INTEGER:
    case TYPE_LOGICAL:
        return left->payload.integer == right->payload.integer;
    case TYPE_REAL:
    case TYPE_DOUBLE:
        return left->payload.number.real == right->payload.number.real;
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        return left->payload.number.real == right->payload.number.real &&
               left->payload.number.imaginary == right->payload.number.imaginary;
    case TYPE_CHARACTER: {
        const size_t left_length = left->payload.character.length;
        const size_t right_length = right->payload.character.length;
        const size_t length = left_length > right_length ? left_length : right_length;
        for (size_t index = 0U; index < length; ++index) {
            const unsigned char a = index < left_length
                                        ? (unsigned char)left->payload.character.bytes[index]
                                        : (unsigned char)' ';
            const unsigned char b = index < right_length
                                        ? (unsigned char)right->payload.character.bytes[index]
                                        : (unsigned char)' ';
            if (a != b)
                return 0;
        }
        return 1;
    }
    default:
        return 0;
    }
}

static int location(F2cConstantValue *value, F2cScalarType type, size_t position) {
    if (position > (size_t)INT64_MAX)
        return 0;
    value->type = (F2cScalarType){TYPE_INTEGER, 8};
    value->payload.integer = (int64_t)position;
    return f2c_constant_value_convert(value, type, NULL, 0U);
}

int f2c_constant_transform_findloc(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                   F2cConstantArray *result, size_t depth) {
    const F2cExpr *dimension_expression = f2c_constant_argument(call, "dim", 2U);
    const F2cExpr *mask_expression = f2c_constant_argument(call, "mask", 3U);
    const F2cExpr *kind_expression = f2c_constant_argument(call, "kind", 4U);
    const F2cExpr *back_expression = f2c_constant_argument(call, "back", 5U);
    F2cConstantArray source = {0}, value = {0}, mask = {0}, output = {0};
    int64_t dimension = 0, kind = 4, back = 0;
    size_t count, stride = 1U;
    int success = 0;
    if (!f2c_constant_evaluate_array(evaluation, f2c_constant_argument(call, "array", 0U), &source,
                                     depth + 1U) ||
        source.shape.rank == 0U || source.type.type == TYPE_DERIVED ||
        !f2c_constant_evaluate_array(evaluation, f2c_constant_argument(call, "value", 1U), &value,
                                     depth + 1U) ||
        value.shape.rank != 0U)
        goto cleanup;
    /* CHARACTER equality pads operands to the longer length. Unlike storage
     * transforms, FINDLOC therefore does not require equal character lengths. */
    const size_t value_length = value.character_length;
    value.character_length = source.character_length;
    const int same_type = f2c_constant_same_element(&source, &value);
    value.character_length = value_length;
    if (!same_type ||
        (dimension_expression != NULL &&
         (!f2c_constant_integer_argument(evaluation, dimension_expression, &dimension,
                                         depth + 1U) ||
          dimension < 1 || (uint64_t)dimension > source.shape.rank)) ||
        (kind_expression != NULL &&
         !f2c_constant_integer_argument(evaluation, kind_expression, &kind, depth + 1U)) ||
        (kind != 1 && kind != 2 && kind != 4 && kind != 8) ||
        (back_expression != NULL &&
         (back_expression->type != TYPE_LOGICAL || back_expression->rank != 0U ||
          !f2c_constant_evaluate_integer(evaluation, back_expression, &back, depth + 1U))))
        goto cleanup;
    if (mask_expression != NULL &&
        (!f2c_constant_evaluate_array(evaluation, mask_expression, &mask, depth + 1U) ||
         mask.type.type != TYPE_LOGICAL ||
         (mask.shape.rank != 0U && !f2c_constant_conformable(&source.shape, &mask.shape))))
        goto cleanup;
    output.type = (F2cScalarType){TYPE_INTEGER, (int)kind};
    if (dimension_expression == NULL) {
        output.shape.rank = 1U;
        f2c_constant_shape_dimension(&output.shape, 0U, source.shape.rank);
    } else {
        output.shape.rank = source.shape.rank - 1U;
        output.shape.kind = output.shape.rank == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_EXPRESSION;
        for (size_t axis = 0U, target = 0U; axis < source.shape.rank; ++axis)
            if (axis != (size_t)dimension - 1U)
                f2c_constant_shape_dimension(&output.shape, target++,
                                             source.shape.dimensions[axis].extent);
    }
    if (!f2c_constant_shape_count(&output.shape, &count) ||
        !f2c_constant_array_allocate(evaluation, &output, count))
        goto cleanup;
    for (size_t index = 0U; index < count; ++index)
        if (!location(&output.values[index], output.type, 0U))
            goto cleanup;
    if (dimension_expression == NULL) {
        size_t found = 0U;
        int matched = 0;
        for (size_t index = 0U; index < source.count; ++index) {
            if (mask_expression != NULL &&
                mask.values[mask.shape.rank == 0U ? 0U : index].payload.integer == 0)
                continue;
            if (equal_value(&source.values[index], &value.values[0])) {
                found = index;
                matched = 1;
                if (!back)
                    break;
            }
        }
        if (matched)
            for (size_t axis = 0U; axis < source.shape.rank; ++axis) {
                const size_t extent = (size_t)source.shape.dimensions[axis].extent;
                if (!location(&output.values[axis], output.type, found % extent + 1U))
                    goto cleanup;
                found /= extent;
            }
    } else if (source.count != 0U) {
        const size_t axis = (size_t)dimension - 1U;
        for (size_t index = 0U; index < axis; ++index)
            stride *= (size_t)source.shape.dimensions[index].extent;
        const size_t extent = (size_t)source.shape.dimensions[axis].extent;
        const size_t span = stride * extent;
        for (size_t index = 0U; index < source.count; ++index) {
            if (mask_expression != NULL &&
                mask.values[mask.shape.rank == 0U ? 0U : index].payload.integer == 0)
                continue;
            const size_t slice = index % stride + (index / span) * stride;
            if ((!back && output.values[slice].payload.integer != 0) ||
                !equal_value(&source.values[index], &value.values[0]))
                continue;
            if (!location(&output.values[slice], output.type, (index / stride) % extent + 1U))
                goto cleanup;
        }
    }
    success = 1;
cleanup:
    f2c_constant_array_free(&source);
    f2c_constant_array_free(&value);
    f2c_constant_array_free(&mask);
    if (!success)
        f2c_constant_array_free(&output);
    else
        *result = output;
    return success;
}
