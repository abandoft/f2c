#include "core/numeric/shift.h"
#include "core/generated/private.h"

#define F2C_STRINGIFY_INNER(value) #value
#define F2C_STRINGIFY(value) F2C_STRINGIFY_INNER(value)

void f2c_emit_array_shift_support(Buffer *output) {
    f2c_buffer_append(output,
                      "static inline F2C_UNUSED int f2c_array_shift_position(size_t position, "
                      "size_t extent, int64_t amount, int circular, size_t *result) ");
    f2c_buffer_append(output, F2C_STRINGIFY(F2C_ARRAY_SHIFT_POSITION_BODY));
    f2c_buffer_append(output, "\n");
}
