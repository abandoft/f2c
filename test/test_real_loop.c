#include "core/numeric/real_loop.h"

#include <float.h>
#include <stdio.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition && failures++ < 12)
        fprintf(stderr, "FAIL: %s\n", message);
}

static void test_exact_models(void) {
    for (int first = -24; first <= 24; ++first)
        for (int last = -24; last <= 24; ++last)
            for (int step = -8; step <= 8; ++step) {
                const int active = step == 0 ? -1 : step > 0 ? first <= last : first >= last;
                const uint64_t expected = active > 0 ? (uint64_t)((last - first) / step) : 0U;
                uint64_t remaining = UINT64_MAX;
                expect(f2c_real_loop_begin_r4((float)first / 8.0F, (float)last / 8.0F,
                                              (float)step / 8.0F, &remaining) == active &&
                           remaining == expected,
                       "binary32 exact binary-fraction controls match independent integer counts");
                expect(f2c_real_loop_begin_r8((double)first / 8.0, (double)last / 8.0,
                                              (double)step / 8.0, &remaining) == active &&
                           remaining == expected,
                       "binary64 exact binary-fraction controls match independent integer counts");
                expect(f2c_real_loop_begin_extended(
                           (long double)first / 8.0L, (long double)last / 8.0L,
                           (long double)step / 8.0L, &remaining) == active &&
                           remaining == expected,
                       "the C long-double model preserves exact binary-fraction counts");
            }
}

static void test_checked_domain(void) {
    uint64_t remaining;
    expect(f2c_real_loop_begin_r4(0.0F, 1.0F, 0.1F, &remaining) == 1 && remaining == 10U,
           "the selected binary32 pre-count includes eleven decimal-step iterations");
    expect(f2c_real_loop_begin_r8(0.0, 1.0, 0.1, &remaining) == 1 && remaining == 10U,
           "the selected binary64 pre-count includes eleven decimal-step iterations");
    expect(f2c_real_loop_begin_r4(FLT_MAX, FLT_MAX, 1.0F, &remaining) == 1 && remaining == 0U,
           "a stagnating real induction value does not create an infinite comparison loop");
    expect(f2c_real_loop_begin_r4(0.0F, 0x1p64F, 1.0F, &remaining) == -1 && remaining == 0U,
           "binary32 counts at the unsigned conversion boundary are rejected before casting");
    expect(f2c_real_loop_begin_r8(0.0, 0x1p64, 1.0, &remaining) == -1 && remaining == 0U,
           "binary64 counts at the unsigned conversion boundary are rejected before casting");
    const double below = nextafter(0x1p64, 0.0);
    expect(f2c_real_loop_begin_r8(0.0, below, 1.0, &remaining) == 1 && remaining == (uint64_t)below,
           "the largest binary64-representable valid counter is preserved exactly");
    if (LDBL_MANT_DIG >= 64)
        expect(f2c_real_loop_begin_extended(0.0L, (long double)UINT64_MAX, 1.0L, &remaining) == 1 &&
                   remaining == UINT64_MAX,
               "a sufficiently precise C long-double model represents all 2^64 trips");
    const double bad[] = {NAN, INFINITY, -INFINITY};
    for (size_t index = 0U; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        expect(f2c_real_loop_begin_r8(bad[index], 1.0, 1.0, &remaining) == -1 && remaining == 0U,
               "nonfinite initial controls are diagnosed");
        expect(f2c_real_loop_begin_r8(0.0, bad[index], 1.0, &remaining) == -1 && remaining == 0U,
               "nonfinite final controls are diagnosed");
        expect(f2c_real_loop_begin_r8(0.0, 1.0, bad[index], &remaining) == -1 && remaining == 0U,
               "nonfinite increments are diagnosed");
    }
    expect(f2c_real_loop_begin_r8(0.0, 1.0, -0.0, &remaining) == -1 && remaining == 0U,
           "negative zero is an invalid step");
    expect(f2c_real_loop_begin_r8(-DBL_MAX, DBL_MAX, 1.0, &remaining) == -1 && remaining == 0U,
           "an unrepresentable selected-model difference cannot trigger an undefined cast");
}

int main(void) {
    test_exact_models();
    test_checked_domain();
    if (failures != 0)
        return 1;
    puts("legacy real loop selected-model counts and checked domains passed");
    return 0;
}
