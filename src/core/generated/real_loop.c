#include "core/numeric/real_loop.h"
#include "core/generated/private.h"

#define F2C_STRINGIFY_INNER(value) #value
#define F2C_STRINGIFY(value) F2C_STRINGIFY_INNER(value)

void f2c_emit_real_loop_support(Buffer *output) {
    f2c_buffer_append(output,
                      "static inline F2C_UNUSED int F2C_REAL_LOOP_FINITE(long double value) "
                      "{ return isfinite(value); }\n"
                      "#define F2C_DEFINE_REAL_LOOP(s, t) "
                      "static inline F2C_UNUSED int F2C_REAL_LOOP_##s(t first, t last, t step, "
                      "uint64_t *remaining) ");
    f2c_buffer_append(output, F2C_STRINGIFY(F2C_REAL_LOOP_BEGIN_BODY(t, F2C_REAL_LOOP_FINITE)));
    f2c_buffer_append(output, "\nF2C_DEFINE_REAL_LOOP(R4, float)\n"
                              "F2C_DEFINE_REAL_LOOP(R8, double)\n"
                              "F2C_DEFINE_REAL_LOOP(EXTENDED, long double)\n"
                              "#undef F2C_DEFINE_REAL_LOOP\n");
}
