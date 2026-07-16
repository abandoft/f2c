#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "benchmark_statistics.h"

typedef void (*daxpy_function)(int32_t *, double *, double *, int32_t *, double *, int32_t *);

typedef struct DaxpyCase {
    int32_t n;
    int32_t stride;
    int repetitions;
} DaxpyCase;

void daxpy(int32_t *n, double *alpha, double *x, int32_t *incx, double *y, int32_t *incy);
void daxpy_(int32_t *n, double *alpha, double *x, int32_t *incx, double *y, int32_t *incy);

static double seconds(void) {
    struct timespec now;
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1.0e-9;
}

static size_t storage_count(int32_t n, int32_t stride) {
    const int32_t magnitude = stride < 0 ? -stride : stride;
    return n <= 0 ? 0U : 1U + (size_t)(n - 1) * (size_t)magnitude;
}

static void initialize(double *x, double *y, size_t count) {
    size_t i;
    for (i = 0U; i < count; ++i) {
        x[i] = (double)((int32_t)(i % 97U) - 48) / 97.0;
        y[i] = (double)((int32_t)(i % 89U) - 44) / 89.0;
    }
}

static double measure(daxpy_function function, double *x, double *y, const DaxpyCase *test) {
    int32_t n = test->n;
    int32_t stride = test->stride;
    double alpha = 0.75;
    double begin = seconds();
    int repeat;
    for (repeat = 0; repeat < test->repetitions; ++repeat) {
        function(&n, &alpha, x, &stride, y, &stride);
    }
    return seconds() - begin;
}

static int run_case(const DaxpyCase *test, double *x, double *c_y, double *fortran_y) {
    const size_t count = storage_count(test->n, test->stride);
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    F2cBenchmarkSample result;
    size_t round;
    size_t i;
    initialize(x, c_y, count);
    memcpy(fortran_y, c_y, count * sizeof(*fortran_y));
    {
        double alpha = -1.25;
        int32_t n = test->n;
        int32_t stride = test->stride;
        daxpy(&n, &alpha, x, &stride, c_y, &stride);
        daxpy_(&n, &alpha, x, &stride, fortran_y, &stride);
    }
    for (i = 0U; i < count; ++i) {
        if (fabs(c_y[i] - fortran_y[i]) > 1.0e-13) {
            fputs("C and Fortran DAXPY results differ\n", stderr);
            return 0;
        }
    }
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        double c_time;
        double fortran_time;
        if ((round & 1) == 0) {
            initialize(x, c_y, count);
            c_time = measure(daxpy, x, c_y, test);
            initialize(x, c_y, count);
            fortran_time = measure(daxpy_, x, c_y, test);
        } else {
            initialize(x, c_y, count);
            fortran_time = measure(daxpy_, x, c_y, test);
            initialize(x, c_y, count);
            c_time = measure(daxpy, x, c_y, test);
        }
        samples[round].generated_seconds = c_time;
        samples[round].fortran_seconds = fortran_time;
        samples[round].ratio = c_time / fortran_time;
    }
    result = f2c_benchmark_median(samples, sizeof(samples) / sizeof(samples[0]));
    printf("DAXPY n=%d inc=%d: generated C %.6fs, Fortran %.6fs, ratio %.3f\n", test->n,
           test->stride, result.generated_seconds, result.fortran_seconds, result.ratio);
    printf("F2C_PERF,DAXPY,n=%d;inc=%d,%.9f,%.9f,%.6f\n", test->n, test->stride,
           result.generated_seconds, result.fortran_seconds, result.ratio);
    return result.ratio <= 1.05;
}

int main(void) {
    static const DaxpyCase cases[] = {{4096, 1, 40960}, {4096, 2, 40960}, {4096, -1, 40960},
                                      {262144, 1, 960}, {262144, 2, 960}, {262144, -1, 960}};
    const size_t capacity = storage_count(262144, 2);
    double *x = (double *)malloc(capacity * sizeof(*x));
    double *c_y = (double *)malloc(capacity * sizeof(*c_y));
    double *fortran_y = (double *)malloc(capacity * sizeof(*fortran_y));
    size_t i;
    int passed = 1;
    if (x == NULL || c_y == NULL || fortran_y == NULL)
        return EXIT_FAILURE;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        passed = run_case(&cases[i], x, c_y, fortran_y) && passed;
    free(x);
    free(c_y);
    free(fortran_y);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
