#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "benchmark_statistics.h"

typedef void (*dgetf2_function)(int32_t *, int32_t *, double *, int32_t *, int32_t *, int32_t *);

typedef struct Dgetf2Case {
    int32_t n;
    int repetitions;
} Dgetf2Case;

void dgetf2(int32_t *, int32_t *, double *, int32_t *, int32_t *, int32_t *);
void dgetf2_(int32_t *, int32_t *, double *, int32_t *, int32_t *, int32_t *);

static double seconds(void) {
    struct timespec now;
    (void)timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1.0e-9;
}

static void initialize(double *matrix, int32_t n) {
    int32_t column;
    int32_t row;
    for (column = 0; column < n; ++column) {
        for (row = 0; row < n; ++row) {
            const int32_t index = row + n * column;
            matrix[index] = (double)(((index + 17) % 251) - 125) / 251.0;
            if (row == column)
                matrix[index] += 4.0;
        }
    }
}

static int verify(int32_t n, double *c_matrix, double *fortran_matrix, int32_t *c_pivots,
                  int32_t *fortran_pivots) {
    int32_t info_c = -1;
    int32_t info_fortran = -1;
    const size_t count = (size_t)n * (size_t)n;
    size_t i;
    initialize(c_matrix, n);
    memcpy(fortran_matrix, c_matrix, count * sizeof(*fortran_matrix));
    dgetf2(&n, &n, c_matrix, &n, c_pivots, &info_c);
    dgetf2_(&n, &n, fortran_matrix, &n, fortran_pivots, &info_fortran);
    if (info_c != 0 || info_fortran != 0)
        return 0;
    for (i = 0U; i < (size_t)n; ++i) {
        if (c_pivots[i] != fortran_pivots[i])
            return 0;
    }
    for (i = 0U; i < count; ++i) {
        const double scale = fmax(1.0, fabs(fortran_matrix[i]));
        if (fabs(c_matrix[i] - fortran_matrix[i]) > 2.0e-12 * scale)
            return 0;
    }
    return 1;
}

static double measure(dgetf2_function function, const Dgetf2Case *test, const double *input,
                      double *work, int32_t *pivots) {
    const size_t bytes = (size_t)test->n * (size_t)test->n * sizeof(*work);
    double elapsed = 0.0;
    int repeat;
    for (repeat = 0; repeat < test->repetitions; ++repeat) {
        int32_t n = test->n;
        int32_t info = -1;
        double begin;
        memcpy(work, input, bytes);
        begin = seconds();
        function(&n, &n, work, &n, pivots, &info);
        elapsed += seconds() - begin;
        if (info != 0)
            return HUGE_VAL;
    }
    return elapsed;
}

static int run_case(const Dgetf2Case *test, double *input, double *work, double *reference,
                    int32_t *pivots, int32_t *reference_pivots) {
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    F2cBenchmarkSample result;
    size_t round;
    if (!verify(test->n, work, reference, pivots, reference_pivots)) {
        fputs("C and Fortran DGETF2 results differ\n", stderr);
        return 0;
    }
    initialize(input, test->n);
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        double c_time;
        double fortran_time;
        if ((round & 1) == 0) {
            c_time = measure(dgetf2, test, input, work, pivots);
            fortran_time = measure(dgetf2_, test, input, work, pivots);
        } else {
            fortran_time = measure(dgetf2_, test, input, work, pivots);
            c_time = measure(dgetf2, test, input, work, pivots);
        }
        samples[round].generated_seconds = c_time;
        samples[round].fortran_seconds = fortran_time;
        samples[round].ratio = c_time / fortran_time;
    }
    result = f2c_benchmark_median(samples, sizeof(samples) / sizeof(samples[0]));
    printf("DGETF2 n=%d: generated C %.6fs, Fortran %.6fs, ratio %.3f\n", test->n,
           result.generated_seconds, result.fortran_seconds, result.ratio);
    printf("F2C_PERF,DGETF2,n=%d,%.9f,%.9f,%.6f\n", test->n, result.generated_seconds,
           result.fortran_seconds, result.ratio);
    return result.ratio <= 1.05;
}

int main(void) {
    static const Dgetf2Case cases[] = {{64, 4096}, {256, 64}, {512, 12}};
    const int32_t maximum_n = 512;
    const size_t count = (size_t)maximum_n * (size_t)maximum_n;
    double *input = (double *)malloc(count * sizeof(*input));
    double *work = (double *)malloc(count * sizeof(*work));
    double *reference = (double *)malloc(count * sizeof(*reference));
    int32_t *pivots = (int32_t *)malloc((size_t)maximum_n * sizeof(*pivots));
    int32_t *reference_pivots = (int32_t *)malloc((size_t)maximum_n * sizeof(*reference_pivots));
    size_t i;
    int passed = 1;
    if (input == NULL || work == NULL || reference == NULL || pivots == NULL ||
        reference_pivots == NULL)
        return EXIT_FAILURE;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        passed = run_case(&cases[i], input, work, reference, pivots, reference_pivots) && passed;
    free(input);
    free(work);
    free(reference);
    free(pivots);
    free(reference_pivots);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
