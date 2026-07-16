#include "common.h"
#include "matrix.h"

#include <stdlib.h>
#include <string.h>

typedef struct Level3Case {
    int32_t n;
    char option_a;
    char option_b;
    int repetitions;
} Level3Case;

enum { F2C_DTRSM_BATCH_SIZE = 8 };

typedef void (*dtrsm_function)(char *, char *, char *, char *, int32_t *, int32_t *, double *,
                               double *, int32_t *, double *, int32_t *, size_t, size_t, size_t,
                               size_t);
typedef void (*dsyrk_function)(char *, char *, int32_t *, int32_t *, double *, double *, int32_t *,
                               double *, double *, int32_t *, size_t, size_t);

void dtrsm(char *, char *, char *, char *, int32_t *, int32_t *, double *, double *, int32_t *,
           double *, int32_t *, size_t, size_t, size_t, size_t);
void dtrsm_(char *, char *, char *, char *, int32_t *, int32_t *, double *, double *, int32_t *,
            double *, int32_t *, size_t, size_t, size_t, size_t);
void dsyrk(char *, char *, int32_t *, int32_t *, double *, double *, int32_t *, double *, double *,
           int32_t *, size_t, size_t);
void dsyrk_(char *, char *, int32_t *, int32_t *, double *, double *, int32_t *, double *, double *,
            int32_t *, size_t, size_t);

static void initialize_dense(double *matrix, int32_t n, int offset) {
    size_t i;
    const size_t count = (size_t)n * (size_t)n;
    for (i = 0U; i < count; ++i)
        matrix[i] = f2c_benchmark_value(i, offset);
}

static void initialize_triangular(double *matrix, int32_t n) {
    int32_t column;
    int32_t row;
    memset(matrix, 0, (size_t)n * (size_t)n * sizeof(*matrix));
    for (column = 0; column < n; ++column) {
        for (row = 0; row <= column; ++row) {
            matrix[row + n * column] =
                row == column
                    ? 2.0 + (double)(row % 7) / 16.0
                    : f2c_benchmark_value((size_t)row + (size_t)n * (size_t)column, 13) / (double)n;
        }
    }
}

static double measure_dtrsm(dtrsm_function function, const Level3Case *test, double *a,
                            const double *input, double *work) {
    const size_t elements = (size_t)test->n * (size_t)test->n;
    const size_t bytes = elements * sizeof(*work);
    char side = test->option_a;
    char uplo = 'U';
    char trans = test->option_b;
    char diag = 'N';
    int32_t n = test->n;
    double alpha = 0.75;
    double elapsed = 0.0;
    int completed = 0;
    while (completed < test->repetitions) {
        const int remaining = test->repetitions - completed;
        const int active = remaining < F2C_DTRSM_BATCH_SIZE ? remaining : F2C_DTRSM_BATCH_SIZE;
        double begin;
        int lane;
        for (lane = 0; lane < active; ++lane)
            memcpy(work + (size_t)lane * elements, input, bytes);
        begin = f2c_benchmark_seconds();
        for (lane = 0; lane < active; ++lane) {
            function(&side, &uplo, &trans, &diag, &n, &n, &alpha, a, &n,
                     work + (size_t)lane * elements, &n, 1U, 1U, 1U, 1U);
        }
        elapsed += f2c_benchmark_seconds() - begin;
        completed += active;
    }
    return elapsed;
}

static int verify_dtrsm(const Level3Case *test, double *a, double *input, double *generated,
                        double *native) {
    char side = test->option_a;
    char uplo = 'U';
    char trans = test->option_b;
    char diag = 'N';
    int32_t n = test->n;
    double alpha = -0.75;
    const size_t count = (size_t)n * (size_t)n;
    size_t i;
    memcpy(generated, input, count * sizeof(*generated));
    memcpy(native, input, count * sizeof(*native));
    dtrsm(&side, &uplo, &trans, &diag, &n, &n, &alpha, a, &n, generated, &n, 1U, 1U, 1U, 1U);
    dtrsm_(&side, &uplo, &trans, &diag, &n, &n, &alpha, a, &n, native, &n, 1U, 1U, 1U, 1U);
    for (i = 0U; i < count; ++i) {
        if (!f2c_benchmark_close(generated[i], native[i], 3.0e-12))
            return 0;
    }
    return 1;
}

static int run_dtrsm(const Level3Case *test, double *a, double *input, double *generated,
                     double *native) {
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    char description[64];
    size_t round;
    initialize_triangular(a, test->n);
    initialize_dense(input, test->n, 29);
    if (!verify_dtrsm(test, a, input, generated, native)) {
        fputs("generated C and Fortran DTRSM results differ\n", stderr);
        return 0;
    }
    (void)snprintf(description, sizeof(description), "n=%d;side=%c;trans=%c", test->n,
                   test->option_a, test->option_b);
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        const double generated_first = measure_dtrsm(dtrsm, test, a, input, generated);
        const double fortran_first = measure_dtrsm(dtrsm_, test, a, input, native);
        const double fortran_second = measure_dtrsm(dtrsm_, test, a, input, native);
        const double generated_second = measure_dtrsm(dtrsm, test, a, input, generated);
        samples[round] = f2c_benchmark_abba_sample(generated_first, fortran_first, fortran_second,
                                                   generated_second);
    }
    return f2c_benchmark_report("DTRSM", description, samples,
                                sizeof(samples) / sizeof(samples[0]));
}

static double measure_dsyrk(dsyrk_function function, const Level3Case *test, double *a,
                            double *matrix) {
    char uplo = 'U';
    char trans = test->option_a;
    int32_t n = test->n;
    double alpha = 0.75;
    double beta = 0.0;
    int repeat;
    double begin = f2c_benchmark_seconds();
    for (repeat = 0; repeat < test->repetitions; ++repeat)
        function(&uplo, &trans, &n, &n, &alpha, a, &n, &beta, matrix, &n, 1U, 1U);
    return f2c_benchmark_seconds() - begin;
}

static int verify_dsyrk(const Level3Case *test, double *a, double *generated, double *native) {
    char uplo = 'U';
    char trans = test->option_a;
    int32_t n = test->n;
    double alpha = -0.75;
    double beta = 0.25;
    size_t column;
    size_t row;
    initialize_dense(generated, n, 43);
    memcpy(native, generated, (size_t)n * (size_t)n * sizeof(*native));
    dsyrk(&uplo, &trans, &n, &n, &alpha, a, &n, &beta, generated, &n, 1U, 1U);
    dsyrk_(&uplo, &trans, &n, &n, &alpha, a, &n, &beta, native, &n, 1U, 1U);
    for (column = 0U; column < (size_t)n; ++column) {
        for (row = 0U; row <= column; ++row) {
            const size_t index = row + (size_t)n * column;
            if (!f2c_benchmark_close(generated[index], native[index], 3.0e-12))
                return 0;
        }
    }
    return 1;
}

static int run_dsyrk(const Level3Case *test, double *a, double *generated, double *native) {
    F2cBenchmarkSample samples[F2C_BENCHMARK_SAMPLE_COUNT];
    char description[64];
    size_t round;
    initialize_dense(a, test->n, 7);
    if (!verify_dsyrk(test, a, generated, native)) {
        fputs("generated C and Fortran DSYRK results differ\n", stderr);
        return 0;
    }
    (void)snprintf(description, sizeof(description), "n=%d;trans=%c", test->n, test->option_a);
    for (round = 0U; round < sizeof(samples) / sizeof(samples[0]); ++round) {
        double generated_first;
        double generated_second;
        double fortran_first;
        double fortran_second;
        initialize_dense(generated, test->n, 43);
        generated_first = measure_dsyrk(dsyrk, test, a, generated);
        initialize_dense(native, test->n, 43);
        fortran_first = measure_dsyrk(dsyrk_, test, a, native);
        initialize_dense(native, test->n, 43);
        fortran_second = measure_dsyrk(dsyrk_, test, a, native);
        initialize_dense(generated, test->n, 43);
        generated_second = measure_dsyrk(dsyrk, test, a, generated);
        samples[round] = f2c_benchmark_abba_sample(generated_first, fortran_first, fortran_second,
                                                   generated_second);
    }
    return f2c_benchmark_report("DSYRK", description, samples,
                                sizeof(samples) / sizeof(samples[0]));
}

int f2c_benchmark_level3(void) {
    static const Level3Case dtrsm_cases[] = {
        {32, 'L', 'N', 16384}, {32, 'L', 'T', 16384}, {32, 'R', 'N', 16384}, {32, 'R', 'T', 16384},
        {96, 'L', 'N', 1024},  {96, 'L', 'T', 1024},  {96, 'R', 'N', 1024},  {96, 'R', 'T', 1024},
        {192, 'L', 'N', 128},  {192, 'L', 'T', 128},  {192, 'R', 'N', 128},  {192, 'R', 'T', 128}};
    static const Level3Case dsyrk_cases[] = {{32, 'N', 0, 16384}, {32, 'T', 0, 16384},
                                             {96, 'N', 0, 1024},  {96, 'T', 0, 1024},
                                             {192, 'N', 0, 128},  {192, 'T', 0, 128}};
    const size_t maximum = 192U * 192U;
    double *a = (double *)malloc(maximum * sizeof(*a));
    double *input = (double *)malloc(maximum * sizeof(*input));
    double *generated = (double *)malloc(maximum * F2C_DTRSM_BATCH_SIZE * sizeof(*generated));
    double *native = (double *)malloc(maximum * F2C_DTRSM_BATCH_SIZE * sizeof(*native));
    size_t i;
    int passed = 1;
    if (a == NULL || input == NULL || generated == NULL || native == NULL) {
        free(a);
        free(input);
        free(generated);
        free(native);
        return 0;
    }
    for (i = 0U; i < sizeof(dtrsm_cases) / sizeof(dtrsm_cases[0]); ++i)
        passed = run_dtrsm(&dtrsm_cases[i], a, input, generated, native) && passed;
    for (i = 0U; i < sizeof(dsyrk_cases) / sizeof(dsyrk_cases[0]); ++i)
        passed = run_dsyrk(&dsyrk_cases[i], a, generated, native) && passed;
    free(a);
    free(input);
    free(generated);
    free(native);
    return passed;
}
