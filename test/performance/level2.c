#include "common.h"
#include "matrix.h"

#include <stdlib.h>
#include <string.h>

typedef struct DgerCase {
    int32_t n;
    int32_t stride;
    int repetitions;
} DgerCase;

void dger(int32_t *, int32_t *, double *, double *, int32_t *, double *, int32_t *, double *,
          int32_t *);
void dger_(int32_t *, int32_t *, double *, double *, int32_t *, double *, int32_t *, double *,
           int32_t *);

static size_t vector_count(const DgerCase *test) {
    return 1U + (size_t)(test->n - 1) * (size_t)abs(test->stride);
}

static void initialize(double *values, size_t count, int offset) {
    size_t i;
    for (i = 0U; i < count; ++i)
        values[i] = f2c_benchmark_value(i, offset);
}

static double measure(void (*function)(int32_t *, int32_t *, double *, double *, int32_t *,
                                       double *, int32_t *, double *, int32_t *),
                      const DgerCase *test, double *x, double *y, double *matrix) {
    int32_t n = test->n;
    int32_t stride = test->stride;
    double alpha = 1.0e-6;
    int repeat;
    double begin = f2c_benchmark_seconds();
    for (repeat = 0; repeat < test->repetitions; ++repeat)
        function(&n, &n, &alpha, x, &stride, y, &stride, matrix, &n);
    return f2c_benchmark_seconds() - begin;
}

static int verify(const DgerCase *test, double *x, double *y, double *generated, double *native) {
    const size_t matrix_count = (size_t)test->n * (size_t)test->n;
    int32_t n = test->n;
    int32_t stride = test->stride;
    double alpha = -0.75;
    size_t i;
    initialize(generated, matrix_count, 19);
    memcpy(native, generated, matrix_count * sizeof(*native));
    dger(&n, &n, &alpha, x, &stride, y, &stride, generated, &n);
    dger_(&n, &n, &alpha, x, &stride, y, &stride, native, &n);
    for (i = 0U; i < matrix_count; ++i) {
        if (!f2c_benchmark_close(generated[i], native[i], 2.0e-12))
            return 0;
    }
    return 1;
}

static int run_case(const DgerCase *test, double *x, double *y, double *generated, double *native) {
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    const size_t matrix_count = (size_t)test->n * (size_t)test->n;
    char description[64];
    size_t round;
    initialize(x, vector_count(test), 7);
    initialize(y, vector_count(test), 31);
    if (!verify(test, x, y, generated, native)) {
        fputs("generated C and Fortran DGER results differ\n", stderr);
        return 0;
    }
    (void)snprintf(description, sizeof(description), "n=%d;inc=%d", test->n, test->stride);
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        double generated_seconds;
        double native_seconds;
        initialize(generated, matrix_count, 19);
        memcpy(native, generated, matrix_count * sizeof(*native));
        if ((round & 1U) == 0U) {
            generated_seconds = measure(dger, test, x, y, generated);
            native_seconds = measure(dger_, test, x, y, native);
        } else {
            native_seconds = measure(dger_, test, x, y, native);
            generated_seconds = measure(dger, test, x, y, generated);
        }
        samples[round].generated_seconds = generated_seconds;
        samples[round].fortran_seconds = native_seconds;
        samples[round].ratio = generated_seconds / native_seconds;
    }
    return f2c_benchmark_report("DGER", description, samples, sizeof(samples) / sizeof(samples[0]));
}

int f2c_benchmark_level2(void) {
    static const DgerCase cases[] = {{64, 1, 8192},  {64, 2, 8192},  {64, -1, 8192},
                                     {192, 1, 1024}, {192, 2, 1024}, {192, -1, 1024}};
    const size_t vector_maximum = 1U + 191U * 2U;
    const size_t matrix_maximum = 192U * 192U;
    double *x = (double *)malloc(vector_maximum * sizeof(*x));
    double *y = (double *)malloc(vector_maximum * sizeof(*y));
    double *generated = (double *)malloc(matrix_maximum * sizeof(*generated));
    double *native = (double *)malloc(matrix_maximum * sizeof(*native));
    size_t i;
    int passed = 1;
    if (x == NULL || y == NULL || generated == NULL || native == NULL) {
        free(x);
        free(y);
        free(generated);
        free(native);
        return 0;
    }
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        passed = run_case(&cases[i], x, y, generated, native) && passed;
    free(x);
    free(y);
    free(generated);
    free(native);
    return passed;
}
