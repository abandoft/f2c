#include "core/numeric/loop.h"
#include "core/generated/private.h"

#define F2C_STRINGIFY_INNER(value) #value
#define F2C_STRINGIFY(value) F2C_STRINGIFY_INNER(value)

void f2c_emit_integer_loop_support(Buffer *output) {
    /* Fortran source identifiers are emitted in canonical lowercase. These
     * private uppercase helper identifiers cannot be shadowed by source names. */
    f2c_buffer_append(output,
                      "static inline F2C_UNUSED int F2C_LOOP_BEGIN(int64_t first, int64_t last, "
                      "int64_t step, uint64_t *remaining) ");
    f2c_buffer_append(output, F2C_STRINGIFY(F2C_INTEGER_LOOP_BEGIN_BODY));
    f2c_buffer_append(
        output, "\nstatic inline F2C_UNUSED int64_t F2C_LOOP_ADD(int64_t value, int64_t step, "
                "int64_t minimum, int64_t maximum) ");
    f2c_buffer_append(output, F2C_STRINGIFY(F2C_INTEGER_LOOP_ADD_BODY));
    f2c_buffer_append(output, "\n#define F2C_DEFINE_LOOP_ADD(s, t, minimum, maximum) "
                              "static inline F2C_UNUSED t F2C_LOOP_##s(t value, t step) { "
                              "return (t)F2C_LOOP_ADD(value, step, minimum, maximum); }\n"
                              "F2C_DEFINE_LOOP_ADD(I8, int8_t, INT8_MIN, INT8_MAX)\n"
                              "F2C_DEFINE_LOOP_ADD(I16, int16_t, INT16_MIN, INT16_MAX)\n"
                              "F2C_DEFINE_LOOP_ADD(I32, int32_t, INT32_MIN, INT32_MAX)\n"
                              "F2C_DEFINE_LOOP_ADD(I64, int64_t, INT64_MIN, INT64_MAX)\n"
                              "#undef F2C_DEFINE_LOOP_ADD\n");
    f2c_buffer_append(output,
                      "#define F2C_DEFINE_LOOP_PARAMETER(s, t, minimum, maximum) "
                      "static inline F2C_UNUSED t F2C_LOOP_INTEGER_##s(int64_t value) { "
                      "if (value < minimum || value > maximum) abort(); return (t)value; }\n"
                      "F2C_DEFINE_LOOP_PARAMETER(I8, int8_t, INT8_MIN, INT8_MAX)\n"
                      "F2C_DEFINE_LOOP_PARAMETER(I16, int16_t, INT16_MIN, INT16_MAX)\n"
                      "F2C_DEFINE_LOOP_PARAMETER(I32, int32_t, INT32_MIN, INT32_MAX)\n"
                      "#undef F2C_DEFINE_LOOP_PARAMETER\n"
                      "static inline F2C_UNUSED int64_t F2C_LOOP_INTEGER_I64(int64_t value) "
                      "{ return value; }\n"
                      "#define F2C_DEFINE_LOOP_REAL(s, t, width) "
                      "static inline F2C_UNUSED t F2C_LOOP_REAL_##s(long double value) { "
                      "const long double rounded = truncl(value); "
                      "const long double limit = ldexpl(1.0L, width - 1); "
                      "if (!isfinite(rounded) || rounded < -limit || rounded >= limit) abort(); "
                      "return (t)rounded; }\n"
                      "F2C_DEFINE_LOOP_REAL(I8, int8_t, 8)\n"
                      "F2C_DEFINE_LOOP_REAL(I16, int16_t, 16)\n"
                      "F2C_DEFINE_LOOP_REAL(I32, int32_t, 32)\n"
                      "F2C_DEFINE_LOOP_REAL(I64, int64_t, 64)\n"
                      "#undef F2C_DEFINE_LOOP_REAL\n");
}
