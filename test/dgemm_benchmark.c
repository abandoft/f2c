#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "benchmark_statistics.h"

typedef void (*dgemm_function)(char *, char *, int32_t *, int32_t *, int32_t *, double *, double *,
                               int32_t *, double *, int32_t *, double *, double *, int32_t *);

typedef struct DgemmCase {
    int32_t n;
    char transpose_a;
    char transpose_b;
    int repetitions;
} DgemmCase;

void dgemm(char *, char *, int32_t *, int32_t *, int32_t *, double *, double *, int32_t *, double *,
           int32_t *, double *, double *, int32_t *, size_t, size_t);
void dgemm_(char *, char *, int32_t *, int32_t *, int32_t *, double *, double *, int32_t *,
            double *, int32_t *, double *, double *, int32_t *, size_t, size_t);

static void dgemm_fortran(char *transpose_a, char *transpose_b, int32_t *m, int32_t *n, int32_t *k,
                          double *alpha, double *a, int32_t *lda, double *b, int32_t *ldb,
                          double *beta, double *c, int32_t *ldc) {
    dgemm_(transpose_a, transpose_b, m, n, k, alpha, a, lda, b, ldb, beta, c, ldc, 1U, 1U);
}

static void dgemm_generated(char *transpose_a, char *transpose_b, int32_t *m, int32_t *n,
                            int32_t *k, double *alpha, double *a, int32_t *lda, double *b,
                            int32_t *ldb, double *beta, double *c, int32_t *ldc) {
    dgemm(transpose_a, transpose_b, m, n, k, alpha, a, lda, b, ldb, beta, c, ldc, 1U, 1U);
}

static void initialize(double *matrix, int32_t n, int offset) {
    int32_t i;
    for (i = 0; i < n * n; ++i)
        matrix[i] = (double)(((i + offset) % 127) - 63) / 127.0;
}

static double measure(dgemm_function function, const DgemmCase *test, double *a, double *b,
                      double *c) {
    int32_t n = test->n;
    char transpose_a = test->transpose_a;
    char transpose_b = test->transpose_b;
    double alpha = 0.75;
    double beta = 0.0;
    double begin = f2c_benchmark_seconds();
    int repeat;
    for (repeat = 0; repeat < test->repetitions; ++repeat)
        function(&transpose_a, &transpose_b, &n, &n, &n, &alpha, a, &n, b, &n, &beta, c, &n);
    return f2c_benchmark_seconds() - begin;
}

static int verify(const DgemmCase *test, double *a, double *b, double *c, double *reference) {
    const size_t count = (size_t)test->n * (size_t)test->n;
    int32_t n = test->n;
    char transpose_a = test->transpose_a;
    char transpose_b = test->transpose_b;
    double alpha = -1.25;
    double beta = 0.5;
    size_t i;
    initialize(c, n, 43);
    memcpy(reference, c, count * sizeof(*reference));
    dgemm_generated(&transpose_a, &transpose_b, &n, &n, &n, &alpha, a, &n, b, &n, &beta, c, &n);
    dgemm_fortran(&transpose_a, &transpose_b, &n, &n, &n, &alpha, a, &n, b, &n, &beta, reference,
                  &n);
    for (i = 0U; i < count; ++i) {
        const double scale = fmax(1.0, fabs(reference[i]));
        if (fabs(c[i] - reference[i]) > 1.0e-12 * scale)
            return 0;
    }
    return 1;
}

static int run_case(const DgemmCase *test, double *a, double *b, double *c, double *reference) {
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    F2cBenchmarkSample result;
    size_t round;
    initialize(a, test->n, 7);
    initialize(b, test->n, 29);
    if (!verify(test, a, b, c, reference)) {
        fputs("C and Fortran DGEMM results differ\n", stderr);
        return 0;
    }
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        double generated_first;
        double generated_second;
        double fortran_first;
        double fortran_second;
        initialize(c, test->n, 43);
        generated_first = measure(dgemm_generated, test, a, b, c);
        initialize(reference, test->n, 43);
        fortran_first = measure(dgemm_fortran, test, a, b, reference);
        initialize(reference, test->n, 43);
        fortran_second = measure(dgemm_fortran, test, a, b, reference);
        initialize(c, test->n, 43);
        generated_second = measure(dgemm_generated, test, a, b, c);
        samples[round] = f2c_benchmark_abba_sample(generated_first, fortran_first, fortran_second,
                                                   generated_second);
    }
    result = f2c_benchmark_median(samples, sizeof(samples) / sizeof(samples[0]));
    printf("DGEMM n=%d trans=%c%c: generated C %.6fs, Fortran %.6fs, ratio %.3f\n", test->n,
           test->transpose_a, test->transpose_b, result.generated_seconds, result.fortran_seconds,
           result.ratio);
    printf("F2C_PERF,DGEMM,n=%d;trans=%c%c,%.9f,%.9f,%.6f\n", test->n, test->transpose_a,
           test->transpose_b, result.generated_seconds, result.fortran_seconds, result.ratio);
    return f2c_benchmark_sample_valid(&result);
}

int main(void) {
    static const DgemmCase cases[] = {
        {32, 'N', 'N', 16384}, {32, 'N', 'T', 16384}, {32, 'T', 'N', 16384},
        {96, 'N', 'N', 768},   {96, 'N', 'T', 768},   {96, 'T', 'N', 768},
        {192, 'N', 'N', 288},  {192, 'N', 'T', 288},  {192, 'T', 'N', 288}};
    const int32_t maximum_n = 192;
    const size_t count = (size_t)maximum_n * (size_t)maximum_n;
    double *a = (double *)malloc(count * sizeof(*a));
    double *b = (double *)malloc(count * sizeof(*b));
    double *c = (double *)malloc(count * sizeof(*c));
    double *reference = (double *)malloc(count * sizeof(*reference));
    size_t i;
    int passed = 1;
    if (a == NULL || b == NULL || c == NULL || reference == NULL)
        return EXIT_FAILURE;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        passed = run_case(&cases[i], a, b, c, reference) && passed;
    free(a);
    free(b);
    free(c);
    free(reference);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
