#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void dgesv(int32_t *n, int32_t *nrhs, double *a, int32_t *lda, int32_t *ipiv, double *b,
           int32_t *ldb, int32_t *info);

int main(void) {
    int32_t n = 3;
    int32_t nrhs = 1;
    int32_t lda = 3;
    int32_t ldb = 3;
    int32_t info = -1;
    int32_t ipiv[3] = {0, 0, 0};
    double a[9] = {
        3.0, 6.0, 3.0, 1.0, 3.0, 1.0, 2.0, 4.0, 5.0,
    };
    double b[3] = {11.0, 24.0, 20.0};
    const double expected[3] = {1.0, 2.0, 3.0};
    size_t i;

    dgesv(&n, &nrhs, a, &lda, ipiv, b, &ldb, &info);
    if (info != 0) {
        fprintf(stderr, "DGESV returned INFO=%d\n", (int)info);
        return EXIT_FAILURE;
    }
    for (i = 0U; i < 3U; ++i) {
        if (fabs(b[i] - expected[i]) > 1.0e-12) {
            fprintf(stderr, "DGESV solution mismatch at %zu: %.17g != %.17g\n", i, b[i],
                    expected[i]);
            return EXIT_FAILURE;
        }
    }
    puts("Reference LAPACK DGESV generated-C check passed");
    return EXIT_SUCCESS;
}
