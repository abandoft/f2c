#include "loop-contract.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_wide(void) {
    static const struct {
        int64_t first;
        int64_t last;
        int64_t step;
        int32_t cap;
        int32_t trips;
        int64_t values[3];
        int64_t final_value;
    } cases[] = {
        {4294967296LL,
         4294967300LL,
         2,
         4,
         3,
         {4294967296LL, 4294967298LL, 4294967300LL},
         4294967302LL},
        {INT64_MIN, INT64_MAX, 1, 3, 3, {INT64_MIN, INT64_MIN + 1, INT64_MIN + 2}, INT64_MIN + 2},
        {INT64_MAX, INT64_MIN, -1, 3, 3, {INT64_MAX, INT64_MAX - 1, INT64_MAX - 2}, INT64_MAX - 2},
        {INT64_MAX, INT64_MIN, INT64_MIN, 4, 2, {INT64_MAX, -1, 0}, INT64_MAX},
        {INT64_MAX, INT64_MAX, 1, 4, 1, {INT64_MAX, 0, 0}, INT64_MIN},
        {INT64_MIN, INT64_MIN, -1, 4, 1, {INT64_MIN, 0, 0}, INT64_MAX},
        {5, 1, 1, 4, 0, {0, 0, 0}, 5},
        {1, 5, -1, 4, 0, {0, 0, 0}, 1},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        int64_t values[4] = {0}, final_value = 0;
        int32_t trips = -1;
        loop64(&cases[index].first, &cases[index].last, &cases[index].step, &cases[index].cap,
               values, &trips, &final_value);
        assert(trips == cases[index].trips && final_value == cases[index].final_value);
        for (int32_t trip = 0; trip < trips; ++trip)
            assert(values[trip] == cases[index].values[trip]);
    }
}

#define CHECK_NARROW(suffix, type, maximum, minimum)                                               \
    do {                                                                                           \
        const type first = maximum, last = maximum, step = 1;                                      \
        const int32_t cap = 4;                                                                     \
        type values[4] = {0}, final_value = 0;                                                     \
        int32_t trips = -1;                                                                        \
        loop##suffix(&first, &last, &step, &cap, values, &trips, &final_value);                    \
        assert(trips == 1 && values[0] == maximum && final_value == minimum);                      \
    } while (0)

static void test_conversion(void) {
    const int64_t integer_first = 125, integer_last = 126, integer_step = 1;
    const int32_t cap = 4;
    int32_t trips = -1;
    int8_t values[4] = {0}, final_value = 0;
    loop8_from64(&integer_first, &integer_last, &integer_step, &cap, values, &trips, &final_value);
    assert(trips == 2 && values[0] == 125 && values[1] == 126 && final_value == 127);
    const double real_first = 1.9, real_last = 3.9, real_step = 1.9;
    loop8_from_real(&real_first, &real_last, &real_step, &cap, values, &trips, &final_value);
    assert(trips == 3 && values[0] == 1 && values[1] == 2 && values[2] == 3 && final_value == 4);
    const double fractional_first = 127.9, fractional_last = 127.9, fractional_step = 1.0;
    loop8_from_real(&fractional_first, &fractional_last, &fractional_step, &cap, values, &trips,
                    &final_value);
    assert(trips == 1 && values[0] == 127 && final_value == INT8_MIN);
}

static int test_execution_error(const char *name) {
    const int32_t cap = 4;
    int32_t trips;
    if (strcmp(name, "zero") == 0) {
        const int64_t first = 1, last = 4, step = 0;
        int64_t values[4], final_value;
        loop64(&first, &last, &step, &cap, values, &trips, &final_value);
        return 2;
    }
    int8_t values[4], final_value;
    if (strcmp(name, "integer-range") == 0) {
        const int64_t first = 128, last = 128, step = 1;
        loop8_from64(&first, &last, &step, &cap, values, &trips, &final_value);
        return 2;
    }
    double first;
    const double last = 1.0, step = 1.0;
    if (strcmp(name, "real-range") == 0)
        first = 128.0;
    else if (strcmp(name, "real-nan") == 0)
        first = NAN;
    else if (strcmp(name, "real-inf") == 0)
        first = INFINITY;
    else
        return 3;
    loop8_from_real(&first, &last, &step, &cap, values, &trips, &final_value);
    return 2;
}

int main(int argc, char **argv) {
    if (argc > 1)
        return test_execution_error(argv[1]);
    test_wide();
    test_conversion();
    CHECK_NARROW(8, int8_t, INT8_MAX, INT8_MIN);
    CHECK_NARROW(16, int16_t, INT16_MAX, INT16_MIN);
    CHECK_NARROW(32, int32_t, INT32_MAX, INT32_MIN);
    puts("independent integer loop ABI and processor contracts passed");
    return 0;
}
