#include "core/generated/private.h"
#include "core/numeric/extremum.h"

#define F2C_STRINGIFY_INNER(value) #value
#define F2C_STRINGIFY(value) F2C_STRINGIFY_INNER(value)

static void emit_maximum_support(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED float f2c_fortran_smax(float a, float b) " F2C_STRINGIFY(F2C_REAL_MAXIMUM_BODY(F2C_NUMBER_IS_NAN, F2C_NUMBER_SIGNBIT, 0.0f)) "\nstatic inline F2C_UNUSED double f2c_fortran_dmax(double a, double b) " F2C_STRINGIFY(
            F2C_REAL_MAXIMUM_BODY(
                F2C_NUMBER_IS_NAN, F2C_NUMBER_SIGNBIT,
                0.0)) "\nstatic inline F2C_UNUSED bool f2c_fortran_smax_select(float a, float b, "
                      "bool back) " F2C_STRINGIFY(F2C_REAL_SELECTION_BODY(F2C_NUMBER_IS_NAN,
                                                                          >)) "\nstatic inline "
                                                                              "F2C_UNUSED bool "
                                                                              "f2c_fortran_dmax_"
                                                                              "select(double a, "
                                                                              "double b, bool "
                                                                              "back)"
                                                                              " " F2C_STRINGIFY(F2C_REAL_SELECTION_BODY(F2C_NUMBER_IS_NAN,
                                                                                                                        >)) "\n"
                                                                                                                            "#define F2C_DEFINE_INTEGER_MAX(s, t) static inline F2C_UNUSED t "
                                                                                                                            "f2c_fortran_##s##max(t a, t b) { return a > b ? a : b; } "
                                                                                                                            "static inline F2C_UNUSED bool f2c_fortran_##s##max_select(t a, t b, bool back) "
                                                                                                                            "{ return a > b || (back && a == b); }\n"
                                                                                                                            "F2C_DEFINE_INTEGER_MAX(i8, int8_t)\n"
                                                                                                                            "F2C_DEFINE_INTEGER_MAX(i16, int16_t)\n"
                                                                                                                            "F2C_DEFINE_INTEGER_MAX(i32, int32_t)\n"
                                                                                                                            "F2C_DEFINE_INTEGER_MAX(i64, int64_t)\n"
                                                                                                                            "#undef F2C_DEFINE_INTEGER_MAX\n"
                                                                                                                            "#define F2C_FORTRAN_MAX(a, b) _Generic((a), int8_t: f2c_fortran_i8max, "
                                                                                                                            "int16_t: f2c_fortran_i16max, int32_t: f2c_fortran_i32max, int64_t: "
                                                                                                                            "f2c_fortran_i64max, float: f2c_fortran_smax, double: "
                                                                                                                            "f2c_fortran_dmax)((a), (b))\n"
                                                                                                                            "#define F2C_MAXIMUM_SELECT(a, b, back) _Generic((a), "
                                                                                                                            "int8_t: f2c_fortran_i8max_select, int16_t: f2c_fortran_i16max_select, "
                                                                                                                            "int32_t: f2c_fortran_i32max_select, int64_t: f2c_fortran_i64max_select, "
                                                                                                                            "float: f2c_fortran_smax_select, double: f2c_fortran_dmax_select)((a), (b), (back))\n");
}

static void emit_minimum_support(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED float f2c_fortran_smin(float a, float b) " F2C_STRINGIFY(F2C_REAL_MINIMUM_BODY(F2C_NUMBER_IS_NAN, F2C_NUMBER_SIGNBIT, 0.0f)) "\nstatic inline F2C_UNUSED double f2c_fortran_dmin(double a, double b) " F2C_STRINGIFY(
            F2C_REAL_MINIMUM_BODY(
                F2C_NUMBER_IS_NAN, F2C_NUMBER_SIGNBIT,
                0.0)) "\nstatic inline F2C_UNUSED bool f2c_fortran_smin_select(float a, float b, "
                      "bool back) " F2C_STRINGIFY(F2C_REAL_SELECTION_BODY(F2C_NUMBER_IS_NAN,
                                                                          <)) "\nstatic inline "
                                                                              "F2C_UNUSED bool "
                                                                              "f2c_fortran_dmin_"
                                                                              "select(double a, "
                                                                              "double b, bool "
                                                                              "back)"
                                                                              " " F2C_STRINGIFY(F2C_REAL_SELECTION_BODY(F2C_NUMBER_IS_NAN,
                                                                                                                        <)) "\n"
                                                                                                                            "#define F2C_DEFINE_INTEGER_MIN(s, t) static inline F2C_UNUSED t "
                                                                                                                            "f2c_fortran_##s##min(t a, t b) { return a < b ? a : b; } "
                                                                                                                            "static inline F2C_UNUSED bool f2c_fortran_##s##min_select(t a, t b, bool back) "
                                                                                                                            "{ return a < b || (back && a == b); }\n"
                                                                                                                            "F2C_DEFINE_INTEGER_MIN(i8, int8_t)\n"
                                                                                                                            "F2C_DEFINE_INTEGER_MIN(i16, int16_t)\n"
                                                                                                                            "F2C_DEFINE_INTEGER_MIN(i32, int32_t)\n"
                                                                                                                            "F2C_DEFINE_INTEGER_MIN(i64, int64_t)\n"
                                                                                                                            "#undef F2C_DEFINE_INTEGER_MIN\n"
                                                                                                                            "#define F2C_FORTRAN_MIN(a, b) _Generic((a), int8_t: f2c_fortran_i8min, "
                                                                                                                            "int16_t: f2c_fortran_i16min, int32_t: f2c_fortran_i32min, int64_t: "
                                                                                                                            "f2c_fortran_i64min, float: f2c_fortran_smin, double: "
                                                                                                                            "f2c_fortran_dmin)((a), (b))\n"
                                                                                                                            "#define F2C_MINIMUM_SELECT(a, b, back) _Generic((a), "
                                                                                                                            "int8_t: f2c_fortran_i8min_select, int16_t: f2c_fortran_i16min_select, "
                                                                                                                            "int32_t: f2c_fortran_i32min_select, int64_t: f2c_fortran_i64min_select, "
                                                                                                                            "float: f2c_fortran_smin_select, double: f2c_fortran_dmin_select)((a), (b), (back))\n");
}

void f2c_emit_extremum_support(Buffer *output, int needs_minimum, int needs_maximum) {
    if (!needs_minimum && !needs_maximum)
        return;
    f2c_buffer_append(output, "#define F2C_NUMBER_IS_NAN(value) isnan(value)\n"
                              "#define F2C_NUMBER_SIGNBIT(value) signbit(value)\n");
    if (needs_maximum)
        emit_maximum_support(output);
    if (needs_minimum)
        emit_minimum_support(output);
    f2c_buffer_append(output, "#undef F2C_NUMBER_IS_NAN\n#undef F2C_NUMBER_SIGNBIT\n");
}
