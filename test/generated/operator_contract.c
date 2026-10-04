#include "operator-contract.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int64_t base = 3;
    int64_t exponent = 39;
    int64_t value = 0;
    int8_t narrow_base = 2;
    int8_t narrow_exponent = 7;
    int8_t narrow_value = 0;
    if (argc == 2) {
        if (strcmp(argv[1], "overflow") == 0) {
            base = 2;
            exponent = 63;
            power_wide(&base, &exponent, &value);
        } else if (strcmp(argv[1], "narrow") == 0) {
            power_narrow(&narrow_base, &narrow_exponent, &narrow_value);
        } else if (strcmp(argv[1], "zero") == 0) {
            base = 0;
            exponent = -1;
            power_wide(&base, &exponent, &value);
        } else {
            return EXIT_FAILURE;
        }
        /* A guard must abort; exiting normally, even with a failure status,
         * must not be mistaken for successful runtime rejection. */
        return 42;
    }
    power_wide(&base, &exponent, &value);
    if (value != INT64_C(4052555153018976267))
        return EXIT_FAILURE;
    base = -2;
    exponent = 63;
    power_wide(&base, &exponent, &value);
    if (value != INT64_MIN)
        return EXIT_FAILURE;
    base = -1;
    exponent = INT64_MIN;
    power_wide(&base, &exponent, &value);
    if (value != 1)
        return EXIT_FAILURE;
    narrow_base = -2;
    power_narrow(&narrow_base, &narrow_exponent, &narrow_value);
    if (narrow_value != INT8_MIN)
        return EXIT_FAILURE;
    {
        float real_base = 10.0f;
        float real_value = 0.0f;
        double double_base = 2.0;
        double double_value = 0.0;
        exponent = -20;
        power_real4(&real_base, &exponent, &real_value);
        if (real_value != 1.0e-20f)
            return EXIT_FAILURE;
        real_base = 2.0f;
        exponent = -149;
        power_real4(&real_base, &exponent, &real_value);
        if (real_value != 0x1p-149f)
            return EXIT_FAILURE;
        exponent = -1074;
        power_real8(&double_base, &exponent, &double_value);
        if (double_value != 0x1p-1074)
            return EXIT_FAILURE;
        real_base = -1.0f;
        double_base = -1.0;
        exponent = INT64_MAX;
        power_real4(&real_base, &exponent, &real_value);
        power_real8(&double_base, &exponent, &double_value);
        if (real_value != -1.0f || double_value != -1.0)
            return EXIT_FAILURE;
        exponent = INT64_MIN;
        power_real4(&real_base, &exponent, &real_value);
        power_real8(&double_base, &exponent, &double_value);
        if (real_value != 1.0f || double_value != 1.0)
            return EXIT_FAILURE;
    }
    {
        const int64_t left[] = {2, 0, -7};
        const int64_t right[] = {4, 0, 3};
        int64_t scalar_equal = 0;
        int64_t array_equal = 0;
        int32_t different_count = -1;
        logical_truth(left, right, &scalar_equal, &array_equal, &different_count);
        if (scalar_equal != 1 || array_equal != 1 || different_count != 0)
            return EXIT_FAILURE;
    }
    puts("independent operator ABI and processor contracts passed");
    return EXIT_SUCCESS;
}
