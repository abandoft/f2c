#include "codegen/literal/integer.h"

#include "internal/base.h"

#include <limits.h>

static const char *integer_prefix(int kind) {
    switch (kind) {
    case 1:
        return "INT8_C";
    case 2:
        return "INT16_C";
    case 4:
        return "INT32_C";
    case 8:
        return "INT64_C";
    default:
        return NULL;
    }
}

char *f2c_integer_constant_literal(int64_t value, int kind) {
    const char *prefix = integer_prefix(kind);
    Buffer result = {0};
    if (prefix == NULL)
        return NULL;
    if ((kind == 1 && value == INT8_MIN) || (kind == 2 && value == INT16_MIN) ||
        (kind == 4 && value == INT32_MIN) || (kind == 8 && value == INT64_MIN)) {
        f2c_buffer_printf(&result, "INT%d_MIN", kind * 8);
    } else if (value < 0) {
        f2c_buffer_printf(&result, "-%s(%lld)", prefix, (long long)-value);
    } else {
        f2c_buffer_printf(&result, "%s(%lld)", prefix, (long long)value);
    }
    return f2c_buffer_take(&result);
}
