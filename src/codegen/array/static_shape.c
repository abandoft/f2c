#include "codegen/array/static_shape.h"

#include "semantic/semantic.h"

int f2c_static_array_element_count(Unit *unit, const Symbol *symbol, size_t *count) {
    F2cShape shape;
    size_t result = 1U;
    size_t dimension;
    if (symbol == NULL || count == NULL)
        return 0;
    f2c_shape_from_symbol(unit, &shape, symbol);
    for (dimension = 0U; dimension < shape.rank; ++dimension)
        if (!shape.dimensions[dimension].extent_known ||
            shape.dimensions[dimension].extent > SIZE_MAX)
            return 0;
    for (dimension = 0U; dimension < shape.rank; ++dimension)
        if (shape.dimensions[dimension].extent == 0U) {
            *count = 0U;
            return 1;
        }
    for (dimension = 0U; dimension < shape.rank; ++dimension) {
        const size_t extent = (size_t)shape.dimensions[dimension].extent;
        if (result > SIZE_MAX / extent)
            return 0;
        result *= extent;
    }
    *count = result;
    return 1;
}
