#include "core/numeric/power.h"
#include "core/generated/private.h"

#define F2C_STRINGIFY_INNER(value) #value
#define F2C_STRINGIFY(value) F2C_STRINGIFY_INNER(value)

void f2c_emit_power_support(Buffer *output, int needs_complex) {
    f2c_buffer_append(
        output, "static inline F2C_UNUSED long double f2c_square_l(long double value) { return "
                "value * value; }\n"
                "static inline F2C_UNUSED int f2c_integer_power(int64_t base, int64_t exponent, "
                "int64_t minimum, int64_t maximum, int64_t *value) ");
    f2c_buffer_append(output, F2C_STRINGIFY(F2C_INTEGER_POWER_BODY));
    f2c_buffer_append(
        output, "\n#define F2C_DEFINE_INTEGER_POWER(s, t, minimum, maximum) "
                "static inline F2C_UNUSED t f2c_pow_##s(t base, int64_t exponent) { int64_t value; "
                "if (!f2c_integer_power(base, exponent, minimum, maximum, &value)) abort(); return "
                "(t)value; }\n"
                "F2C_DEFINE_INTEGER_POWER(i8, int8_t, INT8_MIN, INT8_MAX)\n"
                "F2C_DEFINE_INTEGER_POWER(i16, int16_t, INT16_MIN, INT16_MAX)\n"
                "F2C_DEFINE_INTEGER_POWER(i32, int32_t, INT32_MIN, INT32_MAX)\n"
                "F2C_DEFINE_INTEGER_POWER(i64, int64_t, INT64_MIN, INT64_MAX)\n"
                "#undef F2C_DEFINE_INTEGER_POWER\n"
                "static inline F2C_UNUSED float f2c_pow_fi(float base, int64_t exponent) ");
    f2c_buffer_append(output, F2C_STRINGIFY(F2C_REAL_INTEGER_POWER_BODY(float, 1.0f)));
    f2c_buffer_append(
        output, "\nstatic inline F2C_UNUSED double f2c_pow_di(double base, int64_t exponent) ");
    f2c_buffer_append(output, F2C_STRINGIFY(F2C_REAL_INTEGER_POWER_BODY(double, 1.0)));
    f2c_buffer_append(
        output,
        "\nstatic inline F2C_UNUSED long double f2c_pow_li(long double base, int64_t exponent) ");
    f2c_buffer_append(output, F2C_STRINGIFY(F2C_REAL_INTEGER_POWER_BODY(long double, 1.0L)));
    f2c_buffer_append(output, "\n");
    if (!needs_complex)
        return;
    f2c_buffer_append(
        output,
        "#define F2C_DEFINE_COMPLEX_INTEGER_POWER(s, t, make, multiply, divide, one, zero) "
        "static inline F2C_UNUSED t f2c_pow_##s##i(t base, int64_t exponent) { "
        "uint64_t remaining = exponent < 0 ? (uint64_t)0 - (uint64_t)exponent : "
        "(uint64_t)exponent; "
        "t result = make(one, zero); if (exponent < 0) base = divide(result, base); "
        "while (remaining != 0U) { if ((remaining & 1U) != 0U) result = multiply(result, base); "
        "remaining >>= 1U; if (remaining != 0U) base = multiply(base, base); } return result; }\n"
        "F2C_DEFINE_COMPLEX_INTEGER_POWER(c, f2c_complex_float, f2c_make_c, f2c_cmul, f2c_cdiv, "
        "1.0f, 0.0f)\n"
        "F2C_DEFINE_COMPLEX_INTEGER_POWER(z, f2c_complex_double, f2c_make_z, f2c_zmul, f2c_zdiv, "
        "1.0, 0.0)\n"
        "F2C_DEFINE_COMPLEX_INTEGER_POWER(q, f2c_complex_long_double, f2c_make_q, f2c_qmul, "
        "f2c_qdiv, 1.0L, 0.0L)\n"
        "#undef F2C_DEFINE_COMPLEX_INTEGER_POWER\n");
}
