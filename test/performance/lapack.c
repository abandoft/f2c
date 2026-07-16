#include "common.h"
#include "matrix.h"

#include <stdlib.h>
#include <string.h>

typedef struct LapackCase {
    int32_t n;
    int repetitions;
} LapackCase;

typedef void (*dgetrf_function)(int32_t *, int32_t *, double *, int32_t *, int32_t *, int32_t *);
typedef void (*dpotrf_function)(char *, int32_t *, double *, int32_t *, int32_t *, size_t);

void dgetrf(int32_t *, int32_t *, double *, int32_t *, int32_t *, int32_t *);
void dgetrf_(int32_t *, int32_t *, double *, int32_t *, int32_t *, int32_t *);
void dpotrf(char *, int32_t *, double *, int32_t *, int32_t *, size_t);
void dpotrf_(char *, int32_t *, double *, int32_t *, int32_t *, size_t);
int32_t ilaenv(int32_t *, char *, char *, int32_t *, int32_t *, int32_t *, int32_t *, size_t,
               size_t);
int32_t ilaenv_(int32_t *, char *, char *, int32_t *, int32_t *, int32_t *, int32_t *, size_t,
                size_t);

static int32_t fixed_block_size(void) { return 32; }

int32_t ilaenv(int32_t *ispec, char *name, char *options, int32_t *n1, int32_t *n2, int32_t *n3,
               int32_t *n4, size_t name_length, size_t options_length) {
    (void)name;
    (void)options;
    (void)n1;
    (void)n2;
    (void)n3;
    (void)n4;
    (void)name_length;
    (void)options_length;
    return *ispec == 1 ? fixed_block_size() : 1;
}

int32_t ilaenv_(int32_t *ispec, char *name, char *options, int32_t *n1, int32_t *n2, int32_t *n3,
                int32_t *n4, size_t name_length, size_t options_length) {
    return ilaenv(ispec, name, options, n1, n2, n3, n4, name_length, options_length);
}

static void initialize_general(double *matrix, int32_t n) {
    int32_t column;
    int32_t row;
    for (column = 0; column < n; ++column) {
        for (row = 0; row < n; ++row) {
            const size_t index = (size_t)row + (size_t)n * (size_t)column;
            matrix[index] = f2c_benchmark_value(index, 17);
            if (row == column)
                matrix[index] += 4.0;
        }
    }
}

static void initialize_positive_definite(double *matrix, int32_t n) {
    int32_t column;
    int32_t row;
    for (column = 0; column < n; ++column) {
        for (row = 0; row <= column; ++row) {
            const size_t upper = (size_t)row + (size_t)n * (size_t)column;
            const size_t lower = (size_t)column + (size_t)n * (size_t)row;
            const double value =
                row == column ? (double)n + 2.0 : f2c_benchmark_value(upper, 29) / (double)n;
            matrix[upper] = value;
            matrix[lower] = value;
        }
    }
}

static int verify_dgetrf(const LapackCase *test, double *input, double *generated, double *native,
                         int32_t *generated_pivots, int32_t *native_pivots) {
    int32_t n = test->n;
    int32_t generated_info = -1;
    int32_t native_info = -1;
    const size_t count = (size_t)n * (size_t)n;
    size_t i;
    initialize_general(input, n);
    memcpy(generated, input, count * sizeof(*generated));
    memcpy(native, input, count * sizeof(*native));
    dgetrf(&n, &n, generated, &n, generated_pivots, &generated_info);
    dgetrf_(&n, &n, native, &n, native_pivots, &native_info);
    if (generated_info != 0 || native_info != 0)
        return 0;
    for (i = 0U; i < (size_t)n; ++i) {
        if (generated_pivots[i] != native_pivots[i])
            return 0;
    }
    for (i = 0U; i < count; ++i) {
        if (!f2c_benchmark_close(generated[i], native[i], 5.0e-11))
            return 0;
    }
    return 1;
}

static double measure_dgetrf(dgetrf_function function, const LapackCase *test, const double *input,
                             double *work, int32_t *pivots) {
    const size_t bytes = (size_t)test->n * (size_t)test->n * sizeof(*work);
    double elapsed = 0.0;
    int repeat;
    for (repeat = 0; repeat < test->repetitions; ++repeat) {
        int32_t n = test->n;
        int32_t info = -1;
        double begin;
        memcpy(work, input, bytes);
        begin = f2c_benchmark_seconds();
        function(&n, &n, work, &n, pivots, &info);
        elapsed += f2c_benchmark_seconds() - begin;
        if (info != 0)
            return HUGE_VAL;
    }
    return elapsed;
}

static int run_dgetrf(const LapackCase *test, double *input, double *generated, double *native,
                      int32_t *generated_pivots, int32_t *native_pivots) {
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    char description[64];
    size_t round;
    if (!verify_dgetrf(test, input, generated, native, generated_pivots, native_pivots)) {
        fputs("generated C and Fortran DGETRF results differ\n", stderr);
        return 0;
    }
    initialize_general(input, test->n);
    (void)snprintf(description, sizeof(description), "n=%d;nb=%d", test->n, fixed_block_size());
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        const int generated_outer = f2c_benchmark_generated_is_outer(round);
        double generated_first;
        double generated_second;
        double fortran_first;
        double fortran_second;
        if (generated_outer) {
            generated_first = measure_dgetrf(dgetrf, test, input, generated, generated_pivots);
            fortran_first = measure_dgetrf(dgetrf_, test, input, native, native_pivots);
            fortran_second = measure_dgetrf(dgetrf_, test, input, native, native_pivots);
            generated_second = measure_dgetrf(dgetrf, test, input, generated, generated_pivots);
        } else {
            fortran_first = measure_dgetrf(dgetrf_, test, input, native, native_pivots);
            generated_first = measure_dgetrf(dgetrf, test, input, generated, generated_pivots);
            generated_second = measure_dgetrf(dgetrf, test, input, generated, generated_pivots);
            fortran_second = measure_dgetrf(dgetrf_, test, input, native, native_pivots);
        }
        samples[round] = f2c_benchmark_paired_sample(
            round, generated_first, fortran_first, fortran_second, generated_second);
    }
    return f2c_benchmark_report("DGETRF", description, samples,
                                sizeof(samples) / sizeof(samples[0]));
}

static int verify_dpotrf(const LapackCase *test, char uplo, double *input, double *generated,
                         double *native) {
    int32_t n = test->n;
    int32_t generated_info = -1;
    int32_t native_info = -1;
    const size_t count = (size_t)n * (size_t)n;
    size_t column;
    size_t row;
    initialize_positive_definite(input, n);
    memcpy(generated, input, count * sizeof(*generated));
    memcpy(native, input, count * sizeof(*native));
    dpotrf(&uplo, &n, generated, &n, &generated_info, 1U);
    dpotrf_(&uplo, &n, native, &n, &native_info, 1U);
    if (generated_info != 0 || native_info != 0)
        return 0;
    for (column = 0U; column < (size_t)n; ++column) {
        const size_t first = uplo == 'U' ? 0U : column;
        const size_t last = uplo == 'U' ? column : (size_t)n - 1U;
        for (row = first; row <= last; ++row) {
            const size_t index = row + (size_t)n * column;
            if (!f2c_benchmark_close(generated[index], native[index], 5.0e-11))
                return 0;
        }
    }
    return 1;
}

static double measure_dpotrf(dpotrf_function function, const LapackCase *test, char uplo,
                             const double *input, double *work) {
    const size_t bytes = (size_t)test->n * (size_t)test->n * sizeof(*work);
    double elapsed = 0.0;
    int repeat;
    for (repeat = 0; repeat < test->repetitions; ++repeat) {
        int32_t n = test->n;
        int32_t info = -1;
        double begin;
        memcpy(work, input, bytes);
        begin = f2c_benchmark_seconds();
        function(&uplo, &n, work, &n, &info, 1U);
        elapsed += f2c_benchmark_seconds() - begin;
        if (info != 0)
            return HUGE_VAL;
    }
    return elapsed;
}

static int run_dpotrf(const LapackCase *test, char uplo, double *input, double *generated,
                      double *native) {
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    char description[64];
    size_t round;
    if (!verify_dpotrf(test, uplo, input, generated, native)) {
        fputs("generated C and Fortran DPOTRF results differ\n", stderr);
        return 0;
    }
    initialize_positive_definite(input, test->n);
    (void)snprintf(description, sizeof(description), "n=%d;uplo=%c;nb=%d", test->n, uplo,
                   fixed_block_size());
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        const int generated_outer = f2c_benchmark_generated_is_outer(round);
        double generated_first;
        double generated_second;
        double fortran_first;
        double fortran_second;
        if (generated_outer) {
            generated_first = measure_dpotrf(dpotrf, test, uplo, input, generated);
            fortran_first = measure_dpotrf(dpotrf_, test, uplo, input, native);
            fortran_second = measure_dpotrf(dpotrf_, test, uplo, input, native);
            generated_second = measure_dpotrf(dpotrf, test, uplo, input, generated);
        } else {
            fortran_first = measure_dpotrf(dpotrf_, test, uplo, input, native);
            generated_first = measure_dpotrf(dpotrf, test, uplo, input, generated);
            generated_second = measure_dpotrf(dpotrf, test, uplo, input, generated);
            fortran_second = measure_dpotrf(dpotrf_, test, uplo, input, native);
        }
        samples[round] = f2c_benchmark_paired_sample(
            round, generated_first, fortran_first, fortran_second, generated_second);
    }
    return f2c_benchmark_report("DPOTRF", description, samples,
                                sizeof(samples) / sizeof(samples[0]));
}

int f2c_benchmark_lapack(void) {
    static const LapackCase cases[] = {{128, 512}, {384, 16}, {768, 4}};
    const size_t matrix_maximum = 768U * 768U;
    double *input = (double *)malloc(matrix_maximum * sizeof(*input));
    double *generated = (double *)malloc(matrix_maximum * sizeof(*generated));
    double *native = (double *)malloc(matrix_maximum * sizeof(*native));
    int32_t *generated_pivots = (int32_t *)malloc(768U * sizeof(*generated_pivots));
    int32_t *native_pivots = (int32_t *)malloc(768U * sizeof(*native_pivots));
    size_t i;
    int passed = 1;
    if (input == NULL || generated == NULL || native == NULL || generated_pivots == NULL ||
        native_pivots == NULL) {
        free(input);
        free(generated);
        free(native);
        free(generated_pivots);
        free(native_pivots);
        return 0;
    }
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        passed = run_dgetrf(&cases[i], input, generated, native, generated_pivots, native_pivots) &&
                 passed;
        passed = run_dpotrf(&cases[i], 'U', input, generated, native) && passed;
        passed = run_dpotrf(&cases[i], 'L', input, generated, native) && passed;
    }
    free(input);
    free(generated);
    free(native);
    free(generated_pivots);
    free(native_pivots);
    return passed;
}
