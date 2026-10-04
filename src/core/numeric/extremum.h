#ifndef F2C_CORE_NUMERIC_EXTREMUM_H
#define F2C_CORE_NUMERIC_EXTREMUM_H

#include <math.h>

/* Processor policy, not a claim that Fortran specifies NaN or signed-zero ties.
 * Any NaN propagates, as required by the pinned LAPACK installation test.
 * Mixed zeros select +0 for
 * maximum and -0 for minimum. The bodies are also emitted into generated C:
 * callers supply predicate names so host compiler math macros cannot leak into
 * target output. Arguments are function locals, never source expressions.
 */
#define F2C_REAL_MAXIMUM_BODY(is_nan, sign_bit, zero)                                              \
    {                                                                                              \
        if (a > b)                                                                                 \
            return a;                                                                              \
        if (a < b)                                                                                 \
            return b;                                                                              \
        if (is_nan(a) || is_nan(b))                                                                \
            return a + b;                                                                          \
        if (a == (zero) && b == (zero))                                                            \
            return sign_bit(a) && sign_bit(b) ? -(zero) : (zero);                                  \
        return a;                                                                                  \
    }

#define F2C_REAL_MINIMUM_BODY(is_nan, sign_bit, zero)                                              \
    {                                                                                              \
        if (a < b)                                                                                 \
            return a;                                                                              \
        if (a > b)                                                                                 \
            return b;                                                                              \
        if (is_nan(a) || is_nan(b))                                                                \
            return a + b;                                                                          \
        if (a == (zero) && b == (zero))                                                            \
            return sign_bit(a) || sign_bit(b) ? -(zero) : (zero);                                  \
        return a;                                                                                  \
    }

/* Location ties are numerical ties: signed zero does not change the location.
 * A NaN wins over a number, consistently with value propagation. BACK chooses
 * the first/last NaN when any selected value is NaN.
 */
#define F2C_REAL_SELECTION_BODY(is_nan, comparison)                                                \
    {                                                                                              \
        if (is_nan(a))                                                                             \
            return !is_nan(b) || back;                                                             \
        if (is_nan(b))                                                                             \
            return 0;                                                                              \
        return a comparison b || (back && a == b);                                                 \
    }

#define F2C_DEFINE_REAL_MAXIMUM(s, type, zero)                                                     \
    static inline type f2c_real_maximum_##s(type a, type b)                                        \
        F2C_REAL_MAXIMUM_BODY(isnan, signbit, zero)
#define F2C_DEFINE_REAL_MINIMUM(s, type, zero)                                                     \
    static inline type f2c_real_minimum_##s(type a, type b)                                        \
        F2C_REAL_MINIMUM_BODY(isnan, signbit, zero)

F2C_DEFINE_REAL_MAXIMUM(f, float, 0.0f)
F2C_DEFINE_REAL_MAXIMUM(d, double, 0.0)
F2C_DEFINE_REAL_MINIMUM(f, float, 0.0f)
F2C_DEFINE_REAL_MINIMUM(d, double, 0.0)
#undef F2C_DEFINE_REAL_MAXIMUM
#undef F2C_DEFINE_REAL_MINIMUM

#endif
