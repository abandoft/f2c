#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "benchmark_statistics.h"

typedef void (*dgemv_function)(char *, int32_t *, int32_t *, double *, double *, int32_t *,
                               double *, int32_t *, double *, double *, int32_t *);

typedef struct DgemvCase {
    int32_t n;
    char transpose;
    int repetitions;
} DgemvCase;

void dgemv(char *, int32_t *, int32_t *, double *, double *, int32_t *, double *, int32_t *,
           double *, double *, int32_t *, size_t);
void dgemv_(char *, int32_t *, int32_t *, double *, double *, int32_t *, double *, int32_t *,
            double *, double *, int32_t *, size_t);

static void dgemv_fortran(char *transpose, int32_t *m, int32_t *n, double *alpha, double *a,
                          int32_t *lda, double *x, int32_t *incx, double *beta, double *y,
                          int32_t *incy) {
    dgemv_(transpose, m, n, alpha, a, lda, x, incx, beta, y, incy, 1U);
}

static void dgemv_generated(char *transpose, int32_t *m, int32_t *n, double *alpha, double *a,
                            int32_t *lda, double *x, int32_t *incx, double *beta, double *y,
                            int32_t *incy) {
    dgemv(transpose, m, n, alpha, a, lda, x, incx, beta, y, incy, 1U);
}

static void initialize(double *values, size_t count, int offset) {
    size_t i;
    for (i = 0U; i < count; ++i)
        values[i] = (double)(((int)(i % 127U) + offset) % 127 - 63) / 127.0;
}

static double measure(dgemv_function function, const DgemvCase *test, double *a, double *x,
                      double *y) {
    int32_t stride = 1;
    int32_t n = test->n;
    char transpose = test->transpose;
    double alpha = 0.75;
    double beta = 0.25;
    double begin = f2c_benchmark_seconds();
    int repeat;
    for (repeat = 0; repeat < test->repetitions; ++repeat)
        function(&transpose, &n, &n, &alpha, a, &n, x, &stride, &beta, y, &stride);
    return f2c_benchmark_seconds() - begin;
}

static int verify(const DgemvCase *test, double *a, double *x, double *c_y, double *fortran_y) {
    int32_t stride = 1;
    int32_t n = test->n;
    char transpose = test->transpose;
    double alpha = -1.25;
    double beta = 0.5;
    int32_t i;
    initialize(c_y, (size_t)n, 43);
    memcpy(fortran_y, c_y, (size_t)n * sizeof(*fortran_y));
    dgemv_generated(&transpose, &n, &n, &alpha, a, &n, x, &stride, &beta, c_y, &stride);
    dgemv_fortran(&transpose, &n, &n, &alpha, a, &n, x, &stride, &beta, fortran_y, &stride);
    for (i = 0; i < n; ++i) {
        const double scale = fmax(1.0, fabs(fortran_y[i]));
        if (fabs(c_y[i] - fortran_y[i]) > 1.0e-12 * scale)
            return 0;
    }
    return 1;
}

static int run_case(const DgemvCase *test, double *a, double *x, double *y, double *reference) {
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    F2cBenchmarkSample result;
    size_t round;
    initialize(a, (size_t)test->n * (size_t)test->n, 7);
    initialize(x, (size_t)test->n, 31);
    if (!verify(test, a, x, y, reference)) {
        fputs("C and Fortran DGEMV results differ\n", stderr);
        return 0;
    }
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        double c_time;
        double fortran_time;
        if ((round & 1) == 0) {
            initialize(y, (size_t)test->n, 19);
            c_time = measure(dgemv_generated, test, a, x, y);
            initialize(y, (size_t)test->n, 19);
            fortran_time = measure(dgemv_fortran, test, a, x, y);
        } else {
            initialize(y, (size_t)test->n, 19);
            fortran_time = measure(dgemv_fortran, test, a, x, y);
            initialize(y, (size_t)test->n, 19);
            c_time = measure(dgemv_generated, test, a, x, y);
        }
        samples[round].generated_seconds = c_time;
        samples[round].fortran_seconds = fortran_time;
        samples[round].ratio = c_time / fortran_time;
    }
    result = f2c_benchmark_median(samples, sizeof(samples) / sizeof(samples[0]));
    printf("DGEMV n=%d trans=%c: generated C %.6fs, Fortran %.6fs, ratio %.3f\n", test->n,
           test->transpose, result.generated_seconds, result.fortran_seconds, result.ratio);
    printf("F2C_PERF,DGEMV,n=%d;trans=%c,%.9f,%.9f,%.6f\n", test->n, test->transpose,
           result.generated_seconds, result.fortran_seconds, result.ratio);
    return f2c_benchmark_sample_valid(&result);
}

int main(void) {
    static const DgemvCase cases[] = {
        {128, 'N', 10240}, {128, 'T', 10240}, {768, 'N', 320}, {768, 'T', 320}};
    const int32_t maximum_n = 768;
    const size_t matrix_count = (size_t)maximum_n * (size_t)maximum_n;
    double *a = (double *)malloc(matrix_count * sizeof(*a));
    double *x = (double *)malloc((size_t)maximum_n * sizeof(*x));
    double *y = (double *)malloc((size_t)maximum_n * sizeof(*y));
    double *reference = (double *)malloc((size_t)maximum_n * sizeof(*reference));
    size_t i;
    int passed = 1;
    if (a == NULL || x == NULL || y == NULL || reference == NULL)
        return EXIT_FAILURE;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        passed = run_case(&cases[i], a, x, y, reference) && passed;
    free(a);
    free(x);
    free(y);
    free(reference);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
