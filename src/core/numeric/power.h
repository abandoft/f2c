#ifndef F2C_CORE_NUMERIC_POWER_H
#define F2C_CORE_NUMERIC_POWER_H

#include <float.h>
#include <math.h>
#include <stdint.h>

/* The same bounded unsigned algorithm is compiled into the constant evaluator
 * and emitted into standalone C. It never evaluates ABS(INT64_MIN), performs
 * overflowing signed multiplication, or narrows through floating point.
 */
#define F2C_INTEGER_POWER_BODY                                                                     \
    {                                                                                              \
        uint64_t magnitude;                                                                        \
        uint64_t remaining;                                                                        \
        uint64_t result = 1U;                                                                      \
        uint64_t limit;                                                                            \
        int negative;                                                                              \
        if (base < minimum || base > maximum)                                                      \
            return 0;                                                                              \
        if (exponent < 0) {                                                                        \
            if (base == 0)                                                                         \
                return 0;                                                                          \
            *value = base == 1 ? 1 : base == -1 ? ((uint64_t)exponent & 1U) != 0U ? -1 : 1 : 0;    \
            return 1;                                                                              \
        }                                                                                          \
        remaining = (uint64_t)exponent;                                                            \
        negative = base < 0 && (remaining & 1U) != 0U;                                             \
        magnitude = base < 0 ? (uint64_t)0 - (uint64_t)base : (uint64_t)base;                      \
        limit = negative ? (uint64_t)0 - (uint64_t)minimum : (uint64_t)maximum;                    \
        while (remaining != 0U) {                                                                  \
            if ((remaining & 1U) != 0U) {                                                          \
                if (magnitude != 0U && result > limit / magnitude)                                 \
                    return 0;                                                                      \
                result *= magnitude;                                                               \
            }                                                                                      \
            remaining >>= 1U;                                                                      \
            if (remaining != 0U) {                                                                 \
                if (magnitude != 0U && magnitude > limit / magnitude)                              \
                    return 0;                                                                      \
                magnitude *= magnitude;                                                            \
            }                                                                                      \
        }                                                                                          \
        *value = negative ? result == (uint64_t)0 - (uint64_t)minimum ? minimum : -(int64_t)result \
                          : (int64_t)result;                                                       \
        return 1;                                                                                  \
    }

static inline int f2c_numeric_integer_power(int64_t base, int64_t exponent, int64_t minimum,
                                            int64_t maximum, int64_t *value){F2C_INTEGER_POWER_BODY}

/* Split the exact integer into at most three floating-representable exponent
 * parts. Each libm power receives an exact integer, including parity. This
 * avoids repeated-squaring rounding amplification and computes negative powers
 * directly without overflowing a positive-exponent reciprocal. All nonzero
 * parts have the same sign, so an intermediate range failure cannot conceal a
 * representable final result by subsequent cancellation.
 */
#define F2C_REAL_INTEGER_POWER_BODY(type, one, power, precision)                                   \
    {                                                                                              \
        const int negative = exponent < 0;                                                         \
        uint64_t remaining = negative ? (uint64_t)0 - (uint64_t)exponent : (uint64_t)exponent;     \
        const unsigned int bits = (unsigned int)((precision) < 63 ? (precision) : 63);             \
        const uint64_t mask = ~(uint64_t)0 >> (64U - bits);                                        \
        unsigned int shift = 0U;                                                                   \
        type result = (one);                                                                       \
        while (remaining != 0U) {                                                                  \
            const uint64_t part = (remaining & mask) << shift;                                     \
            if (part != 0U) {                                                                      \
                const type part_exponent = (type)part;                                             \
                result *= power(base, negative ? -part_exponent : part_exponent);                  \
            }                                                                                      \
            remaining >>= bits;                                                                    \
            shift += bits;                                                                         \
        }                                                                                          \
        return result;                                                                             \
    }

static inline float f2c_numeric_real4_integer_power(float base, int64_t exponent) {
    F2C_REAL_INTEGER_POWER_BODY(float, 1.0f, powf, FLT_MANT_DIG)
}
static inline double f2c_numeric_real8_integer_power(double base, int64_t exponent) {
    F2C_REAL_INTEGER_POWER_BODY(double, 1.0, pow, DBL_MANT_DIG)
}

#endif
