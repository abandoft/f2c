#include "findloc-contract.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const int32_t values[] = {1, 2, 2};
    int64_t position = -1;
    int64_t dimension = 1;
    if (argc == 2) {
        if (strcmp(argv[1], "dimension-wide") == 0 || strcmp(argv[1], "dimension-zero") == 0 ||
            strcmp(argv[1], "dimension-negative") == 0) {
            dimension = strcmp(argv[1], "dimension-wide") == 0   ? INT64_C(4294967297)
                        : strcmp(argv[1], "dimension-zero") == 0 ? 0
                                                                 : -1;
            findloc_dimension_model(values, &dimension, &position);
        } else if (strcmp(argv[1], "mask-shape") == 0) {
            const int32_t n = 2, m = 3, p = 3, q = 2;
            const int32_t matrix[] = {1, 2, 2, 4, 2, 6};
            const int32_t mask[] = {1, 1, 1, 1, 1, 1};
            int64_t coordinates[2] = {-1, -1};
            findloc_mask_shape(&n, &m, &p, &q, matrix, mask, coordinates);
        } else if (strcmp(argv[1], "result-kind") == 0) {
            int32_t large[128] = {0};
            const double searched = 2.0;
            large[127] = 2;
            findloc_narrow_position(large, &searched, &position);
        } else {
            return EXIT_FAILURE;
        }
        return 42; /* Every error profile must terminate through checked abort. */
    }
    {
        const int64_t logical_values[] = {-2, 0, 5}, back = -9;
        int64_t searched = 7, first = -1, last = -1;
        int8_t narrow = -1;
        int16_t short_value = -1;
        int32_t normal = -1;
        findloc_logical_models(logical_values, &searched, &back, &first, &last, &narrow,
                               &short_value, &normal);
        if (first != 1 || last != 3 || narrow != 3 || short_value != 3 || normal != 3)
            return EXIT_FAILURE;
        searched = 0;
        findloc_logical_models(logical_values, &searched, &back, &first, &last, &narrow,
                               &short_value, &normal);
        if (first != 2 || last != 2 || narrow != 2 || short_value != 2 || normal != 2)
            return EXIT_FAILURE;
    }
    {
        const double real_values[] = {NAN, -0.0, INFINITY, 0.0};
        double searched = 0.0;
        int64_t first = -1, last = -1;
        findloc_real_models(real_values, &searched, &first, &last);
        if (first != 2 || last != 4)
            return EXIT_FAILURE;
        searched = NAN;
        findloc_real_models(real_values, &searched, &first, &last);
        if (first != 0 || last != 0)
            return EXIT_FAILURE;
        searched = INFINITY;
        findloc_real_models(real_values, &searched, &first, &last);
        if (first != 3 || last != 3)
            return EXIT_FAILURE;
    }
    {
        const int64_t integers[] = {INT64_C(4611686293305294849), 1};
        const float searched = 0x1.000002p62f;
        findloc_integer_real_model(integers, &searched, &position);
        if (position != 1)
            return EXIT_FAILURE; /* Direct INTEGER64 -> REAL32 model conversion. */
    }
    findloc_dimension_model(values, &dimension, &position);
    if (position != 2)
        return EXIT_FAILURE;
    {
        int32_t large[128] = {0};
        const double searched = 2.0;
        large[126] = 2;
        findloc_narrow_position(large, &searched, &position);
        if (position != 127)
            return EXIT_FAILURE;
    }
    {
        const int32_t n = 2, m = 3;
        const int32_t matrix[] = {1, 2, 2, 4, 2, 6};
        int32_t mask[] = {-2, 0, 7, 0, 0, 0};
        int64_t coordinates[2] = {-1, -1};
        findloc_mask_shape(&n, &m, &n, &m, matrix, mask, coordinates);
        if (coordinates[0] != 1 || coordinates[1] != 2)
            return EXIT_FAILURE;
        memset(mask, 0, sizeof(mask));
        findloc_mask_shape(&n, &m, &n, &m, matrix, mask, coordinates);
        if (coordinates[0] != 0 || coordinates[1] != 0)
            return EXIT_FAILURE;
        const int32_t empty = 0;
        findloc_mask_shape(&empty, &m, &empty, &m, matrix, mask, coordinates);
        if (coordinates[0] != 0 || coordinates[1] != 0)
            return EXIT_FAILURE;
    }
    puts("FINDLOC independent runtime ABI and model contracts passed");
    return EXIT_SUCCESS;
}
