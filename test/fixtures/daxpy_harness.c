#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#define F2C_RESTRICT __restrict
#else
#define F2C_RESTRICT restrict
#endif

void daxpy(const int32_t *n, const double *alpha, const double *F2C_RESTRICT x, const int32_t *incx,
           double *F2C_RESTRICT y, const int32_t *incy);

int main(void) {
    const int32_t n = 4;
    const int32_t stride = 1;
    const double alpha = 2.0;
    const double x[] = {1.0, 2.0, 3.0, 4.0};
    double y[] = {10.0, 20.0, 30.0, 40.0};
    const double expected[] = {12.0, 24.0, 36.0, 48.0};
    int32_t i;
    daxpy(&n, &alpha, x, &stride, y, &stride);
    for (i = 0; i < n; ++i) {
        if (fabs(y[i] - expected[i]) > 1.0e-12)
            return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
