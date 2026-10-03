#include "codegen/storage/private.h"

#include <stdlib.h>

int f2c_storage_emit_contiguous_dimension(Buffer *output, Unit *unit,
                                          const F2cStorageReference *reference, size_t dimension,
                                          const char *physical_binding, const char *lower,
                                          const char *extent, int depth) {
    if (!f2c_storage_emit_store(output, unit, reference, F2C_OBJECT_LOWER, dimension,
                                physical_binding, lower, depth) ||
        !f2c_storage_emit_store(output, unit, reference, F2C_OBJECT_EXTENT, dimension,
                                physical_binding, extent, depth))
        return 0;
    if (!reference->symbol->pointer && reference->state_source != F2C_OBJECT_STATE_DESCRIPTOR)
        return 1;
    if (dimension == 0U)
        return f2c_storage_emit_store(output, unit, reference, F2C_OBJECT_STRIDE, dimension,
                                      physical_binding, "1", depth);
    char *prior_stride = f2c_storage_bound_property(unit, reference, F2C_OBJECT_STRIDE,
                                                    dimension - 1U, physical_binding, 0);
    char *prior_extent = f2c_storage_bound_property(unit, reference, F2C_OBJECT_EXTENT,
                                                    dimension - 1U, physical_binding, 0);
    Buffer stride = {0};
    if (prior_stride != NULL && prior_extent != NULL)
        f2c_buffer_printf(&stride, "f2c_descriptor_stride_extent((ptrdiff_t)(%s), (size_t)(%s))",
                          prior_stride, prior_extent);
    const int emitted = f2c_storage_emit_store(output, unit, reference, F2C_OBJECT_STRIDE,
                                               dimension, physical_binding, stride.data, depth);
    free(prior_stride);
    free(prior_extent);
    free(stride.data);
    return emitted;
}
