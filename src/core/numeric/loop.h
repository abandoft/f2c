#ifndef F2C_CORE_NUMERIC_LOOP_H
#define F2C_CORE_NUMERIC_LOOP_H

#include <stdint.h>
#include <string.h>

/* Store trips minus one, with a separate active flag. This represents even the
 * complete 2^64-element interval without a counter overflow. A zero step is
 * invalid; callers choose a diagnostic or their execution-error policy. */
#define F2C_INTEGER_LOOP_BEGIN_BODY                                                                \
    {                                                                                              \
        uint64_t distance;                                                                         \
        uint64_t magnitude;                                                                        \
        *remaining = 0U;                                                                           \
        if (step == 0)                                                                             \
            return -1;                                                                             \
        if ((step > 0 && first > last) || (step < 0 && first < last))                              \
            return 0;                                                                              \
        if (step > 0) {                                                                            \
            distance = (uint64_t)last - (uint64_t)first;                                           \
            magnitude = (uint64_t)step;                                                            \
        } else {                                                                                   \
            distance = (uint64_t)first - (uint64_t)last;                                           \
            magnitude = (uint64_t)0 - (uint64_t)step;                                              \
        }                                                                                          \
        *remaining = distance / magnitude;                                                         \
        return 1;                                                                                  \
    }

/* Valid standard executions do not overflow their DO variable. Outside that
 * domain, use explicit two's-complement modular advancement, including the
 * final update: no C signed overflow or out-of-range unsigned-to-signed cast.
 * The supported integer storage models have minimum == -maximum - 1. */
#define F2C_INTEGER_LOOP_ADD_BODY                                                                  \
    {                                                                                              \
        const uint64_t mask = (uint64_t)maximum * (uint64_t)2 + (uint64_t)1;                       \
        const uint64_t bits = ((uint64_t)value + (uint64_t)step) & mask;                           \
        if (bits <= (uint64_t)maximum)                                                             \
            return (int64_t)bits;                                                                  \
        return minimum + (int64_t)(bits - (uint64_t)maximum - (uint64_t)1);                        \
    }

static inline int f2c_integer_loop_begin(int64_t first, int64_t last, int64_t step,
                                         uint64_t *remaining) {
    F2C_INTEGER_LOOP_BEGIN_BODY;
}

static inline int64_t f2c_integer_loop_add(int64_t value, int64_t step, int64_t minimum,
                                           int64_t maximum) {
    F2C_INTEGER_LOOP_ADD_BODY;
}

/* Exact-width intN_t has a two's-complement representation and no padding
 * (C17 7.20.1.1). Copy the unsigned sum's representation, not an out-of-range
 * unsigned-to-signed conversion. This also leaves a branch-free induction
 * update for optimizers instead of selecting a signed range on every trip. */
#define F2C_INTEGER_LOOP_TYPED_ADD_BODY(t, u)                                                      \
    {                                                                                              \
        const u bits = (u)((uint64_t)(u)value + (uint64_t)(u)step);                                \
        t result;                                                                                  \
        memcpy(&result, &bits, sizeof(result));                                                    \
        return result;                                                                             \
    }

static inline int8_t f2c_integer_loop_add_i8(int8_t value, int8_t step) {
    F2C_INTEGER_LOOP_TYPED_ADD_BODY(int8_t, uint8_t);
}

static inline int16_t f2c_integer_loop_add_i16(int16_t value, int16_t step) {
    F2C_INTEGER_LOOP_TYPED_ADD_BODY(int16_t, uint16_t);
}

static inline int32_t f2c_integer_loop_add_i32(int32_t value, int32_t step) {
    F2C_INTEGER_LOOP_TYPED_ADD_BODY(int32_t, uint32_t);
}

static inline int64_t f2c_integer_loop_add_i64(int64_t value, int64_t step) {
    F2C_INTEGER_LOOP_TYPED_ADD_BODY(int64_t, uint64_t);
}

#define F2C_INTEGER_LOOP_I32_VALUE_BODY                                                            \
    {                                                                                              \
        const uint32_t bits = (uint32_t)value;                                                     \
        int32_t result;                                                                            \
        memcpy(&result, &bits, sizeof(result));                                                    \
        return result;                                                                             \
    }

static inline int32_t f2c_integer_loop_value_i32(int64_t value) { F2C_INTEGER_LOOP_I32_VALUE_BODY; }

#endif
