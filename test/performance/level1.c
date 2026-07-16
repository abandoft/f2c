#include "common.h"
#include "matrix.h"

#include <stdlib.h>
#include <string.h>

typedef struct Level1Case {
    int32_t n;
    int32_t stride;
    int repetitions;
} Level1Case;

typedef double (*ddot_function)(int32_t *, double *, int32_t *, double *, int32_t *);
typedef double (*dnrm2_function)(int32_t *, double *, int32_t *);
typedef void (*dscal_function)(int32_t *, double *, double *, int32_t *);

double ddot(int32_t *, double *, int32_t *, double *, int32_t *);
double ddot_(int32_t *, double *, int32_t *, double *, int32_t *);
double dnrm2(int32_t *, double *, int32_t *);
double dnrm2_(int32_t *, double *, int32_t *);
void dscal(int32_t *, double *, double *, int32_t *);
void dscal_(int32_t *, double *, double *, int32_t *);

static volatile double reduction_sink;

static size_t vector_count(const Level1Case *test) {
    return 1U + (size_t)(test->n - 1) * (size_t)abs(test->stride);
}

static void initialize(double *values, size_t count, int offset) {
    size_t i;
    for (i = 0U; i < count; ++i)
        values[i] = f2c_benchmark_value(i, offset);
}

static double measure_ddot(ddot_function function, const Level1Case *test, double *x, double *y) {
    int repeat;
    double begin = f2c_benchmark_seconds();
    for (repeat = 0; repeat < test->repetitions; ++repeat) {
        int32_t n = test->n;
        int32_t stride = test->stride;
        reduction_sink = function(&n, x, &stride, y, &stride);
    }
    return f2c_benchmark_seconds() - begin;
}

static double measure_dnrm2(dnrm2_function function, const Level1Case *test, double *x) {
    int repeat;
    double begin = f2c_benchmark_seconds();
    for (repeat = 0; repeat < test->repetitions; ++repeat) {
        int32_t n = test->n;
        int32_t stride = test->stride;
        reduction_sink = function(&n, x, &stride);
    }
    return f2c_benchmark_seconds() - begin;
}

static double measure_dscal(dscal_function function, const Level1Case *test, double *x) {
    int32_t n = test->n;
    int32_t stride = test->stride;
    double alpha = 0.999999;
    int repeat;
    double begin = f2c_benchmark_seconds();
    for (repeat = 0; repeat < test->repetitions; ++repeat)
        function(&n, &alpha, x, &stride);
    return f2c_benchmark_seconds() - begin;
}

static int verify(const Level1Case *test, double *x, double *y, double *native) {
    const size_t count = vector_count(test);
    int32_t n = test->n;
    int32_t stride = test->stride;
    double alpha = -0.75;
    double generated_value;
    double native_value;
    size_t i;
    initialize(x, count, 7);
    initialize(y, count, 31);
    generated_value = ddot(&n, x, &stride, y, &stride);
    native_value = ddot_(&n, x, &stride, y, &stride);
    if (!f2c_benchmark_close(generated_value, native_value, 2.0e-12))
        return 0;
    generated_value = dnrm2(&n, x, &stride);
    native_value = dnrm2_(&n, x, &stride);
    if (!f2c_benchmark_close(generated_value, native_value, 2.0e-12))
        return 0;
    memcpy(native, x, count * sizeof(*native));
    dscal(&n, &alpha, x, &stride);
    dscal_(&n, &alpha, native, &stride);
    for (i = 0U; i < count; ++i) {
        if (!f2c_benchmark_close(x[i], native[i], 2.0e-12))
            return 0;
    }
    return 1;
}

static int run_case(const Level1Case *test, double *x, double *y, double *native) {
    static const char *const kernels[] = {"DDOT", "DNRM2", "DSCAL"};
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    char description[64];
    size_t kernel;
    size_t round;
    int passed = 1;
    const size_t count = vector_count(test);
    if (!verify(test, x, y, native)) {
        fputs("generated C and Fortran Level 1 results differ\n", stderr);
        return 0;
    }
    (void)snprintf(description, sizeof(description), "n=%d;inc=%d", test->n, test->stride);
    for (kernel = 0U; kernel < sizeof(kernels) / sizeof(kernels[0]); ++kernel) {
        /* Reference DSCAL explicitly returns for INCX <= 0; negative strides
         * are meaningful for DDOT/DNRM2 but are not a DSCAL workload. */
        if (kernel == 2U && test->stride < 0)
            continue;
        for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
            const int generated_outer = f2c_benchmark_generated_is_outer(round);
            double generated_first;
            double generated_second;
            double fortran_first;
            double fortran_second;
            if (kernel == 0U) {
                if (generated_outer) {
                    initialize(x, count, 7);
                    initialize(y, count, 31);
                    generated_first = measure_ddot(ddot, test, x, y);
                    initialize(native, count, 7);
                    initialize(y, count, 31);
                    fortran_first = measure_ddot(ddot_, test, native, y);
                    initialize(native, count, 7);
                    initialize(y, count, 31);
                    fortran_second = measure_ddot(ddot_, test, native, y);
                    initialize(x, count, 7);
                    initialize(y, count, 31);
                    generated_second = measure_ddot(ddot, test, x, y);
                } else {
                    initialize(native, count, 7);
                    initialize(y, count, 31);
                    fortran_first = measure_ddot(ddot_, test, native, y);
                    initialize(x, count, 7);
                    initialize(y, count, 31);
                    generated_first = measure_ddot(ddot, test, x, y);
                    initialize(x, count, 7);
                    initialize(y, count, 31);
                    generated_second = measure_ddot(ddot, test, x, y);
                    initialize(native, count, 7);
                    initialize(y, count, 31);
                    fortran_second = measure_ddot(ddot_, test, native, y);
                }
            } else if (kernel == 1U) {
                if (generated_outer) {
                    initialize(x, count, 7);
                    generated_first = measure_dnrm2(dnrm2, test, x);
                    initialize(native, count, 7);
                    fortran_first = measure_dnrm2(dnrm2_, test, native);
                    initialize(native, count, 7);
                    fortran_second = measure_dnrm2(dnrm2_, test, native);
                    initialize(x, count, 7);
                    generated_second = measure_dnrm2(dnrm2, test, x);
                } else {
                    initialize(native, count, 7);
                    fortran_first = measure_dnrm2(dnrm2_, test, native);
                    initialize(x, count, 7);
                    generated_first = measure_dnrm2(dnrm2, test, x);
                    initialize(x, count, 7);
                    generated_second = measure_dnrm2(dnrm2, test, x);
                    initialize(native, count, 7);
                    fortran_second = measure_dnrm2(dnrm2_, test, native);
                }
            } else {
                if (generated_outer) {
                    initialize(x, count, 7);
                    generated_first = measure_dscal(dscal, test, x);
                    initialize(native, count, 7);
                    fortran_first = measure_dscal(dscal_, test, native);
                    initialize(native, count, 7);
                    fortran_second = measure_dscal(dscal_, test, native);
                    initialize(x, count, 7);
                    generated_second = measure_dscal(dscal, test, x);
                } else {
                    initialize(native, count, 7);
                    fortran_first = measure_dscal(dscal_, test, native);
                    initialize(x, count, 7);
                    generated_first = measure_dscal(dscal, test, x);
                    initialize(x, count, 7);
                    generated_second = measure_dscal(dscal, test, x);
                    initialize(native, count, 7);
                    fortran_second = measure_dscal(dscal_, test, native);
                }
            }
            samples[round] = f2c_benchmark_paired_sample(
                round, generated_first, fortran_first, fortran_second, generated_second);
        }
        passed = f2c_benchmark_report(kernels[kernel], description, samples,
                                      sizeof(samples) / sizeof(samples[0])) &&
                 passed;
    }
    return passed;
}

int f2c_benchmark_level1(void) {
    static const Level1Case cases[] = {{4096, 1, 8192}, {4096, 2, 8192}, {4096, -1, 8192},
                                       {65536, 1, 512}, {65536, 2, 512}, {65536, -1, 512}};
    const size_t maximum_count = 1U + 65535U * 2U;
    double *x = (double *)malloc(maximum_count * sizeof(*x));
    double *y = (double *)malloc(maximum_count * sizeof(*y));
    double *native = (double *)malloc(maximum_count * sizeof(*native));
    size_t i;
    int passed = 1;
    if (x == NULL || y == NULL || native == NULL) {
        free(x);
        free(y);
        free(native);
        return 0;
    }
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        passed = run_case(&cases[i], x, y, native) && passed;
    free(x);
    free(y);
    free(native);
    return passed;
}
