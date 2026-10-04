#ifndef F2C_CORE_NUMERIC_SHIFT_H
#define F2C_CORE_NUMERIC_SHIFT_H

#include <stddef.h>
#include <stdint.h>

/* Shared by semantic folding and emitted C. No signed negation, index+shift
 * addition, or conversion of an out-of-range index is performed. */
#define F2C_ARRAY_SHIFT_POSITION_BODY                                                              \
    {                                                                                              \
        if (result == NULL)                                                                        \
            return 0;                                                                              \
        *result = 0U;                                                                              \
        if (extent == 0U || position >= extent)                                                    \
            return 0;                                                                              \
        uint64_t magnitude = amount < 0 ? (uint64_t)0 - (uint64_t)amount : (uint64_t)amount;       \
        if (circular)                                                                              \
            magnitude %= (uint64_t)extent;                                                         \
        if (amount >= 0) {                                                                         \
            if (magnitude < (uint64_t)(extent - position)) {                                       \
                *result = position + (size_t)magnitude;                                            \
                return 1;                                                                          \
            }                                                                                      \
            if (circular) {                                                                        \
                *result = (size_t)(magnitude - (uint64_t)(extent - position));                     \
                return 1;                                                                          \
            }                                                                                      \
        } else {                                                                                   \
            if (magnitude <= (uint64_t)position) {                                                 \
                *result = position - (size_t)magnitude;                                            \
                return 1;                                                                          \
            }                                                                                      \
            if (circular) {                                                                        \
                *result = extent - (size_t)(magnitude - (uint64_t)position);                       \
                return 1;                                                                          \
            }                                                                                      \
        }                                                                                          \
        return 0;                                                                                  \
    }

static inline int f2c_array_shift_position(size_t position, size_t extent, int64_t amount,
                                           int circular, size_t *result) {
    F2C_ARRAY_SHIFT_POSITION_BODY;
}

#endif
