#include "ir/constant.h"

#include <stdlib.h>
#include <string.h>

void f2c_constant_value_free(F2cConstantValue *value) {
    if (value == NULL)
        return;
    if (value->type.type == TYPE_CHARACTER)
        free(value->payload.character.bytes);
    else if (value->type.type == TYPE_DERIVED)
        f2c_expr_free(value->payload.derived);
    memset(value, 0, sizeof(*value));
}

int f2c_constant_value_copy(F2cConstantValue *target, const F2cConstantValue *source) {
    F2cConstantValue copy = *source;
    if (source->type.type == TYPE_CHARACTER) {
        const size_t length = source->payload.character.length;
        if (length == SIZE_MAX)
            return 0;
        copy.payload.character.bytes = (char *)malloc(length + 1U);
        if (copy.payload.character.bytes == NULL)
            return 0;
        if (length != 0U)
            memcpy(copy.payload.character.bytes, source->payload.character.bytes, length);
        copy.payload.character.bytes[length] = '\0';
    } else if (source->type.type == TYPE_DERIVED) {
        copy.payload.derived =
            f2c_expr_clone_substitute_integers(source->payload.derived, NULL, 0U);
        if (copy.payload.derived == NULL)
            return 0;
    }
    *target = copy;
    return 1;
}

void f2c_constant_array_free(F2cConstantArray *array) {
    if (array == NULL)
        return;
    for (size_t index = 0U; index < array->count; ++index)
        f2c_constant_value_free(&array->values[index]);
    free(array->values);
    memset(array, 0, sizeof(*array));
}

int f2c_constant_array_reserve(F2cConstantArray *array, size_t count) {
    if (count <= array->capacity)
        return 1;
    if (count > SIZE_MAX / sizeof(*array->values))
        return 0;
    F2cConstantValue *values = (F2cConstantValue *)realloc(array->values, count * sizeof(*values));
    if (values == NULL)
        return 0;
    array->values = values;
    array->capacity = count;
    return 1;
}

int f2c_constant_array_copy(F2cConstantArray *target, const F2cConstantArray *source) {
    F2cConstantArray copy = *source;
    copy.values = NULL;
    copy.count = copy.capacity = 0U;
    if (!f2c_constant_array_reserve(&copy, source->count))
        return 0;
    for (; copy.count < source->count; ++copy.count)
        if (!f2c_constant_value_copy(&copy.values[copy.count], &source->values[copy.count])) {
            f2c_constant_array_free(&copy);
            return 0;
        }
    *target = copy;
    return 1;
}

int f2c_constant_shape_count(const F2cShape *shape, size_t *count) {
    size_t result = 1U;
    if (shape == NULL || count == NULL || shape->rank > F2C_MAX_RANK)
        return 0;
    for (size_t dimension = 0U; dimension < shape->rank; ++dimension)
        if (!shape->dimensions[dimension].extent_known ||
            shape->dimensions[dimension].extent > SIZE_MAX)
            return 0;
    for (size_t dimension = 0U; dimension < shape->rank; ++dimension)
        if (shape->dimensions[dimension].extent == 0U) {
            *count = 0U;
            return 1;
        }
    for (size_t dimension = 0U; dimension < shape->rank; ++dimension) {
        const size_t extent = (size_t)shape->dimensions[dimension].extent;
        if (result > SIZE_MAX / extent)
            return 0;
        result *= extent;
    }
    *count = result;
    return 1;
}
