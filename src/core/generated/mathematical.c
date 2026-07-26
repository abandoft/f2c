#include "core/generated/private.h"

static void emit_maximum_support(Buffer *output) {
    f2c_buffer_append(
        output, "static inline F2C_UNUSED float f2c_fortran_smax(float a, float b) { return "
                "isnan(a) || isnan(b) ? a + b : (a > b ? a : b); }\n"
                "static inline F2C_UNUSED double f2c_fortran_dmax(double a, double b) { return "
                "isnan(a) || isnan(b) ? a + b : (a > b ? a : b); }\n"
                "#define F2C_DEFINE_INTEGER_MAX(s, t) static inline F2C_UNUSED t "
                "f2c_fortran_##s##max(t a, t b) { return a > b ? a : b; }\n"
                "F2C_DEFINE_INTEGER_MAX(i8, int8_t)\n"
                "F2C_DEFINE_INTEGER_MAX(i16, int16_t)\n"
                "F2C_DEFINE_INTEGER_MAX(i32, int32_t)\n"
                "F2C_DEFINE_INTEGER_MAX(i64, int64_t)\n"
                "#undef F2C_DEFINE_INTEGER_MAX\n"
                "#define F2C_FORTRAN_MAX(a, b) _Generic((a), int8_t: f2c_fortran_i8max, "
                "int16_t: f2c_fortran_i16max, int32_t: f2c_fortran_i32max, int64_t: "
                "f2c_fortran_i64max, float: f2c_fortran_smax, double: "
                "f2c_fortran_dmax)((a), (b))\n");
}

static void emit_minimum_support(Buffer *output) {
    f2c_buffer_append(
        output, "static inline F2C_UNUSED float f2c_fortran_smin(float a, float b) { return "
                "isnan(a) || isnan(b) ? a + b : (a < b ? a : b); }\n"
                "static inline F2C_UNUSED double f2c_fortran_dmin(double a, double b) { return "
                "isnan(a) || isnan(b) ? a + b : (a < b ? a : b); }\n"
                "#define F2C_DEFINE_INTEGER_MIN(s, t) static inline F2C_UNUSED t "
                "f2c_fortran_##s##min(t a, t b) { return a < b ? a : b; }\n"
                "F2C_DEFINE_INTEGER_MIN(i8, int8_t)\n"
                "F2C_DEFINE_INTEGER_MIN(i16, int16_t)\n"
                "F2C_DEFINE_INTEGER_MIN(i32, int32_t)\n"
                "F2C_DEFINE_INTEGER_MIN(i64, int64_t)\n"
                "#undef F2C_DEFINE_INTEGER_MIN\n"
                "#define F2C_FORTRAN_MIN(a, b) _Generic((a), int8_t: f2c_fortran_i8min, "
                "int16_t: f2c_fortran_i16min, int32_t: f2c_fortran_i32min, int64_t: "
                "f2c_fortran_i64min, float: f2c_fortran_smin, double: "
                "f2c_fortran_dmin)((a), (b))\n");
}

void f2c_emit_extremum_support(Buffer *output, int needs_minimum, int needs_maximum) {
    if (needs_maximum)
        emit_maximum_support(output);
    if (needs_minimum)
        emit_minimum_support(output);
}
