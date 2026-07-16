#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#define F2C_RESTRICT __restrict
#else
#define F2C_RESTRICT restrict
#endif

void daxpy(int32_t *n, double *alpha, double *F2C_RESTRICT x, int32_t *incx, double *F2C_RESTRICT y,
           int32_t *incy);

static int close_enough(double actual, double expected) {
    const double scale = fabs(expected) > 1.0 ? fabs(expected) : 1.0;
    return fabs(actual - expected) <= 1.0e-13 * scale;
}

static int unit_stride(void) {
    int32_t n = 5;
    int32_t stride = 1;
    double alpha = -1.5;
    double x[] = {1.0, -2.0, 3.0, -4.0, 5.0};
    double y[] = {3.0, 4.0, 5.0, 6.0, 7.0};
    const double expected[] = {1.5, 7.0, 0.5, 12.0, -0.5};
    int32_t i;
    daxpy(&n, &alpha, x, &stride, y, &stride);
    for (i = 0; i < n; ++i) {
        if (!close_enough(y[i], expected[i]))
            return 0;
    }
    return 1;
}

static int negative_input_stride(void) {
    int32_t n = 4;
    int32_t incx = -1;
    int32_t incy = 1;
    double alpha = 2.0;
    double x[] = {1.0, 2.0, 3.0, 4.0};
    double y[] = {10.0, 20.0, 30.0, 40.0};
    const double expected[] = {18.0, 26.0, 34.0, 42.0};
    int32_t i;
    daxpy(&n, &alpha, x, &incx, y, &incy);
    for (i = 0; i < n; ++i) {
        if (!close_enough(y[i], expected[i]))
            return 0;
    }
    return 1;
}

static int non_unit_strides(void) {
    int32_t n = 3;
    int32_t stride = 2;
    double alpha = 0.5;
    double x[] = {2.0, 99.0, 4.0, 99.0, 6.0};
    double y[] = {1.0, 77.0, 2.0, 77.0, 3.0};
    const double expected[] = {2.0, 77.0, 4.0, 77.0, 6.0};
    size_t i;
    daxpy(&n, &alpha, x, &stride, y, &stride);
    for (i = 0U; i < sizeof(y) / sizeof(y[0]); ++i) {
        if (!close_enough(y[i], expected[i]))
            return 0;
    }
    return 1;
}

int main(void) {
    if (!unit_stride() || !negative_input_stride() || !non_unit_strides()) {
        fputs("translated Reference BLAS DAXPY produced an incorrect result\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
