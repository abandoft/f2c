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
