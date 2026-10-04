#ifndef F2C_CORE_NUMERIC_POWER_H
#define F2C_CORE_NUMERIC_POWER_H

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

// clang-format off
static inline int f2c_numeric_integer_power(int64_t base, int64_t exponent, int64_t minimum,
                                           int64_t maximum, int64_t *value)
    F2C_INTEGER_POWER_BODY
// clang-format on

/* Invert before squaring for negative exponents, so a representable subnormal
 * result is not lost by overflowing its positive-exponent reciprocal. */
#define F2C_REAL_INTEGER_POWER_BODY(type, one)                                                     \
    {                                                                                              \
        const int negative = exponent < 0;                                                         \
        uint64_t remaining = negative ? (uint64_t)0 - (uint64_t)exponent : (uint64_t)exponent;     \
        type result = (one);                                                                       \
        if (negative)                                                                              \
            base = (one) / base;                                                                   \
        while (remaining != 0U) {                                                                  \
            if ((remaining & 1U) != 0U)                                                            \
                result *= base;                                                                    \
            remaining >>= 1U;                                                                      \
            if (remaining != 0U)                                                                   \
                base *= base;                                                                      \
        }                                                                                          \
        return result;                                                                             \
    }

    // clang-format off
static inline float f2c_numeric_real4_integer_power(float base, int64_t exponent)
    F2C_REAL_INTEGER_POWER_BODY(float, 1.0f)
static inline double f2c_numeric_real8_integer_power(double base, int64_t exponent)
    F2C_REAL_INTEGER_POWER_BODY(double, 1.0)
// clang-format on

#endif
