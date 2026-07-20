#include "codegen/literal/real.h"

#include "internal/base.h"

#include <math.h>
#include <stddef.h>

static int append_fraction(Buffer *output, double fraction, size_t digit_count) {
    static const char hexadecimal[] = "0123456789abcdef";
    char digits[13];
    size_t digit;
    for (digit = 0U; digit < digit_count; ++digit) {
        unsigned int value;
        fraction *= 16.0;
        value = (unsigned int)fraction;
        if (value > 15U)
            return 0;
        digits[digit] = hexadecimal[value];
        fraction -= (double)value;
    }
    while (digit_count != 0U && digits[digit_count - 1U] == '0')
        --digit_count;
    if (digit_count != 0U) {
        f2c_buffer_append(output, ".");
        f2c_buffer_append_n(output, digits, digit_count);
    }
    return 1;
}

char *f2c_real_constant_literal(double value, int kind) {
    Buffer result = {0};
    double magnitude;
    double significand;
    size_t digit_count;
    int exponent = 0;
    if (kind == 4) {
        value = (double)(float)value;
        digit_count = 6U;
    } else if (kind == 8) {
        digit_count = 13U;
    } else {
        return NULL;
    }
    if (!isfinite(value))
        return NULL;
    if (signbit(value))
        f2c_buffer_append(&result, "-");
    if (value == 0.0) {
        f2c_buffer_append(&result, "0x0p+0");
    } else {
        magnitude = fabs(value);
        significand = frexp(magnitude, &exponent) * 2.0;
        --exponent;
        f2c_buffer_append(&result, "0x1");
        if (!append_fraction(&result, significand - 1.0, digit_count)) {
            result.failed = 1;
            return f2c_buffer_take(&result);
        }
        f2c_buffer_printf(&result, "p%+d", exponent);
    }
    if (kind == 4)
        f2c_buffer_append(&result, "f");
    return f2c_buffer_take(&result);
}
