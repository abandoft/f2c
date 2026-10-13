#include "intrinsic-result-model.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const int64_t wide = INT64_C(17);
    const int8_t narrow[4] = {1, 2, 3, 4};
    const int64_t mask[4] = {1, 1, 1, 1};
    const int32_t expected_models[14] = {4, 4, 4, 8, 1, 1, 8, 8, 4, 4, 8, 4, 8, 2};
    const int32_t expected_choices[6] = {103, 144, 21, 108, 101, 24};
    int32_t models[14] = {0}, choices[6] = {0};
    intrinsic_result_probe(&wide, narrow, mask, models, choices);
    for (size_t index = 0U; index < 14U; ++index)
        if (models[index] != expected_models[index])
            return EXIT_FAILURE;
    for (size_t index = 0U; index < 6U; ++index)
        if (choices[index] != expected_choices[index])
            return EXIT_FAILURE;
    puts("intrinsic result kind, generic selection and independent C ABI contracts passed");
    return EXIT_SUCCESS;
}
