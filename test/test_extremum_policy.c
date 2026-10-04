#include "core/numeric/extremum.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

/* Independent oracle for the processor contract, including bit-sign ties. */
static int matches(double result, double a, double b, int maximum) {
    double expected;
    if (a != a || b != b)
        return result != result;
    if (a == 0.0 && b == 0.0) {
        const int negative = maximum ? (signbit(a) && signbit(b)) : (signbit(a) || signbit(b));
        return result == 0.0 && (signbit(result) != 0) == negative;
    } else
        expected = maximum ? (a > b ? a : b) : (a < b ? a : b);
    return result == expected && (signbit(result) != 0) == (signbit(expected) != 0);
}

int main(void) {
    static const float single[] = {-INFINITY, -FLT_MAX, -1.0f,    -0.0f, 0.0f,
                                   1.0f,      FLT_MAX,  INFINITY, NAN};
    static const double wide[] = {-INFINITY, -DBL_MAX, -1.0,     -0.0, 0.0,
                                  1.0,       DBL_MAX,  INFINITY, NAN};
    for (size_t i = 0U; i < sizeof(single) / sizeof(single[0]); ++i) {
        for (size_t j = 0U; j < sizeof(single) / sizeof(single[0]); ++j) {
            const float a = single[i], b = single[j];
            expect(matches(f2c_real_maximum_f(a, b), a, b, 1), "kind-4 maximum policy");
            expect(matches(f2c_real_minimum_f(a, b), a, b, 0), "kind-4 minimum policy");
        }
    }
    for (size_t i = 0U; i < sizeof(wide) / sizeof(wide[0]); ++i) {
        for (size_t j = 0U; j < sizeof(wide) / sizeof(wide[0]); ++j) {
            const double a = wide[i], b = wide[j];
            expect(matches(f2c_real_maximum_d(a, b), a, b, 1), "kind-8 maximum policy");
            expect(matches(f2c_real_minimum_d(a, b), a, b, 0), "kind-8 minimum policy");
        }
    }
    if (failures != 0)
        return 1;
    puts("all real extremum policy pairs passed");
    return 0;
}
