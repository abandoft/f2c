#ifndef F2C_CORE_NUMERIC_REAL_LOOP_H
#define F2C_CORE_NUMERIC_REAL_LOOP_H

#include <math.h>
#include <stdint.h>

static inline int f2c_real_loop_finite(long double value) { return isfinite(value); }

/* Legacy REAL DO uses a count established before the first iteration, not a
 * comparison of the accumulating induction variable with its limit.
 * Keep the subtraction and division in the selected real model. The count
 * minus one representation matches the common native processor policy and
 * permits 2^64 trips without wrapping a counter. The floating value is cast
 * only after proving that its truncated nonnegative value fits uint64_t.
 * finite is an ordinary function identifier: host math macros must never be
 * expanded into the stringified generated C implementation. */
#define F2C_REAL_LOOP_BEGIN_BODY(t, finite)                                                        \
    {                                                                                              \
        *remaining = 0U;                                                                           \
        if (!finite(first) || !finite(last) || !finite(step) || step == (t)0)                      \
            return -1;                                                                             \
        if ((step > (t)0 && first > last) || (step < (t)0 && first < last))                        \
            return 0;                                                                              \
        const t distance = (t)(last - first);                                                      \
        const t quotient = (t)(distance / step);                                                   \
        if (!finite(quotient) || quotient < (t)0 || (long double)quotient >= 0x1p64L)              \
            return -1;                                                                             \
        *remaining = (uint64_t)quotient;                                                           \
        return 1;                                                                                  \
    }

static inline int f2c_real_loop_begin_r4(float first, float last, float step, uint64_t *remaining) {
    F2C_REAL_LOOP_BEGIN_BODY(float, f2c_real_loop_finite);
}

static inline int f2c_real_loop_begin_r8(double first, double last, double step,
                                         uint64_t *remaining) {
    F2C_REAL_LOOP_BEGIN_BODY(double, f2c_real_loop_finite);
}

static inline int f2c_real_loop_begin_extended(long double first, long double last,
                                               long double step, uint64_t *remaining) {
    F2C_REAL_LOOP_BEGIN_BODY(long double, f2c_real_loop_finite);
}

#endif
