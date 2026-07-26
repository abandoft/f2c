#include "core/generated/private.h"

void f2c_emit_numeric_conversion_support(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED int64_t f2c_checked_integer_from_i64(int64_t value, int kind) "
        "{ int64_t minimum = kind == 1 ? INT8_MIN : kind == 2 ? INT16_MIN : kind == 4 ? "
        "INT32_MIN : kind == 8 ? INT64_MIN : INT64_C(1); int64_t maximum = kind == 1 ? "
        "INT8_MAX : kind == 2 ? INT16_MAX : kind == 4 ? INT32_MAX : kind == 8 ? INT64_MAX : "
        "INT64_C(0); if (minimum > maximum || value < minimum || value > maximum) abort(); "
        "return value; }\n"
        "static inline F2C_UNUSED int64_t f2c_int_integer(double value, int kind) { double "
        "truncated = trunc(value); double limit = kind == 1 ? 0x1p7 : kind == 2 ? 0x1p15 : "
        "kind == 4 ? 0x1p31 : kind == 8 ? 0x1p63 : 0.0; if (limit == 0.0 || "
        "!isfinite(truncated) || truncated < -limit || truncated >= limit) abort(); return "
        "(int64_t)truncated; }\n");
}
