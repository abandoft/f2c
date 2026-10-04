#include "loop-contract.h"

#include <assert.h>
#include <float.h>
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

static void test_default_integer_unit_strides(void) {
    const int32_t first = INT32_MIN + 2, last = INT32_MIN;
    int32_t cap = 4, values[4] = {0}, trips = -1, final_value = 0;
    loop32_descending(&first, &last, &cap, values, &trips, &final_value);
    assert(trips == 3 && values[0] == first && values[1] == INT32_MIN + 1 && values[2] == last &&
           final_value == INT32_MAX);
    cap = 3;
    loop32_descending(&first, &last, &cap, values, &trips, &final_value);
    assert(trips == 3 && final_value == last);
    loop32_descending(&last, &first, &cap, values, &trips, &final_value);
    assert(trips == 0 && final_value == last);
    const int32_t rounded_first = 3, rounded_last = 1;
    loop32_rounded_step(&rounded_first, &rounded_last, values, &trips, &final_value);
    assert(trips == 3 && values[0] == 3 && values[1] == 2 && values[2] == 1 && final_value == 0);
}

static void test_default_integer_constant_strides(void) {
    const int32_t first = INT32_MAX - 10, last = INT32_MAX;
    int32_t values[4] = {0}, trips = -1, final_value = 0;
    loop32_stride5(&first, &last, values, &trips, &final_value);
    assert(trips == 3 && values[0] == first && values[1] == INT32_MAX - 5 && values[2] == last &&
           final_value == INT32_MIN + 4);
    const int32_t negative_first = INT32_MIN + 10, negative_last = INT32_MIN;
    loop32_stride_minus5(&negative_first, &negative_last, values, &trips, &final_value);
    assert(trips == 3 && values[0] == negative_first && values[1] == INT32_MIN + 5 &&
           values[2] == negative_last && final_value == INT32_MAX - 4);
    loop32_stride5(&last, &first, values, &trips, &final_value);
    assert(trips == 0 && final_value == last);
    loop32_stride_minus5(&negative_last, &negative_first, values, &trips, &final_value);
    assert(trips == 0 && final_value == negative_last);
}

static void test_real_counts(void) {
    static const struct {
        double first, last, step;
        int32_t cap, trips4, trips8;
    } cases[] = {
        {0.0, 1.0, 0.1, 16, 11, 11}, {1.0, 0.0, -0.1, 16, 11, 11},
        {1.0, -1.0, -0.3, 16, 7, 7}, {-0.3, 0.3, 0.2, 16, 4, 3},
        {5.0, 1.0, 0.1, 16, 0, 0},   {1.0, 5.0, -0.1, 16, 0, 0},
        {0.0, 1.0, 0.1, 3, 3, 3},    {(double)FLT_MAX, (double)FLT_MAX, 1.0, 16, 1, 1},
        {0.0, 0x1p63, 1.0, 3, 3, 3},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        float first4 = (float)cases[index].first, last4 = (float)cases[index].last;
        float step4 = (float)cases[index].step, values4[16] = {0}, final4 = 0;
        double values8[16] = {0}, final8 = 0;
        int32_t trips4 = -1, trips8 = -1;
        loop_real4(&first4, &last4, &step4, &cases[index].cap, values4, &trips4, &final4);
        loop_real8(&cases[index].first, &cases[index].last, &cases[index].step, &cases[index].cap,
                   values8, &trips8, &final8);
        assert(trips4 == cases[index].trips4 && trips8 == cases[index].trips8);
        float expected4 = first4;
        for (int32_t trip = 0; trip < trips4; ++trip) {
            assert(values4[trip] == expected4);
            if (trip + 1 < cases[index].cap)
                expected4 = (float)(expected4 + step4);
        }
        double expected8 = cases[index].first;
        for (int32_t trip = 0; trip < trips8; ++trip) {
            assert(values8[trip] == expected8);
            if (trip + 1 < cases[index].cap)
                expected8 += cases[index].step;
        }
        assert(final4 == expected4 && final8 == expected8);
    }
}

static int test_real_execution_error(const char *name) {
    const int32_t cap = 16;
    int32_t trips;
    const double first = strstr(name, "-nan") != NULL ? NAN : 0.0;
    const double last = strstr(name, "-inf") != NULL        ? INFINITY
                        : strstr(name, "-overflow") != NULL ? 0x1p64
                                                            : 1.0;
    const double step = strstr(name, "-zero") != NULL ? -0.0 : 1.0;
    if (strncmp(name, "real4-do-", 9) == 0) {
        const float first4 = (float)first, last4 = (float)last, step4 = (float)step;
        float values[16], final_value;
        loop_real4(&first4, &last4, &step4, &cap, values, &trips, &final_value);
    } else {
        double values[16], final_value;
        loop_real8(&first, &last, &step, &cap, values, &trips, &final_value);
    }
    return 2;
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

static void test_default_integer_range(void) {
    static const struct {
        int32_t first, last, step, cap, trips;
        int32_t values[3];
        int32_t final_value;
    } cases[] = {
        {INT32_MIN, INT32_MAX, 1, 3, 3, {INT32_MIN, INT32_MIN + 1, INT32_MIN + 2}, INT32_MIN + 2},
        {INT32_MAX, INT32_MIN, -1, 3, 3, {INT32_MAX, INT32_MAX - 1, INT32_MAX - 2}, INT32_MAX - 2},
        {INT32_MAX, INT32_MIN, INT32_MIN, 4, 2, {INT32_MAX, -1, 0}, INT32_MAX},
        {5, 1, 1, 4, 0, {0, 0, 0}, 5},
        {1, 5, -1, 4, 0, {0, 0, 0}, 1},
        {INT32_MAX - 2, INT32_MAX, 1, 4, 3, {INT32_MAX - 2, INT32_MAX - 1, INT32_MAX}, INT32_MIN},
        {INT32_MIN + 2, INT32_MIN, -1, 4, 3, {INT32_MIN + 2, INT32_MIN + 1, INT32_MIN}, INT32_MAX},
        {INT32_MAX - 2, INT32_MAX, 1, 3, 3, {INT32_MAX - 2, INT32_MAX - 1, INT32_MAX}, INT32_MAX},
        {INT32_MIN + 2, INT32_MIN, -1, 3, 3, {INT32_MIN + 2, INT32_MIN + 1, INT32_MIN}, INT32_MIN},
        {INT32_MIN, INT32_MAX, INT32_MAX, 4, 3, {INT32_MIN, -1, INT32_MAX - 1}, -3},
        {INT32_MAX, INT32_MIN, -INT32_MAX, 4, 3, {INT32_MAX, 0, -INT32_MAX}, 2},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        int32_t values[4] = {0}, trips = -1, final_value = 0;
        loop32(&cases[index].first, &cases[index].last, &cases[index].step, &cases[index].cap,
               values, &trips, &final_value);
        if (trips != cases[index].trips || final_value != cases[index].final_value)
            fprintf(stderr, "default integer case %zu: trips=%d, final=%d; expected %d, %d\n",
                    index, trips, final_value, cases[index].trips, cases[index].final_value);
        assert(trips == cases[index].trips && final_value == cases[index].final_value);
        for (int32_t trip = 0; trip < trips; ++trip)
            assert(values[trip] == cases[index].values[trip]);
    }
}

static int test_execution_error(const char *name) {
    if (strncmp(name, "real4-do-", 9) == 0 || strncmp(name, "real8-do-", 9) == 0)
        return test_real_execution_error(name);
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
    test_default_integer_range();
    test_default_integer_unit_strides();
    test_default_integer_constant_strides();
    test_real_counts();
    CHECK_NARROW(8, int8_t, INT8_MAX, INT8_MIN);
    CHECK_NARROW(16, int16_t, INT16_MAX, INT16_MIN);
    CHECK_NARROW(32, int32_t, INT32_MAX, INT32_MIN);
    puts("independent integer and legacy real loop ABI and processor contracts passed");
    return 0;
}
