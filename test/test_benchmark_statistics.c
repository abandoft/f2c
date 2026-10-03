#include "performance/samples.h"

#include <stdlib.h>

static int close_enough(double left, double right) { return fabs(left - right) < 1.0e-12; }

static int test_sample_report(void) {
    F2cBenchmarkSample samples[] = {
        {1.0, 2.0, 0.5}, {1.0, 2.0, 0.5}, {1.0, 2.0, 0.5}, {1.0, 2.0, 0.5}};
    static const char *const expected[] = {"F2C_PERF_SAMPLE,TEST,n=1,0,ABBA,0,1,2,0.5\n",
                                           "F2C_PERF_SAMPLE,TEST,n=1,1,BAAB,0,1,2,0.5\n",
                                           "F2C_PERF_SAMPLE,TEST,n=1,2,ABBA,1,1,2,0.5\n",
                                           "F2C_PERF_SAMPLE,TEST,n=1,3,BAAB,1,1,2,0.5\n"};
    char line[128];
    FILE *stream = NULL;
    int passed = 1;
#if defined(_MSC_VER)
    if (tmpfile_s(&stream) != 0)
        return 0;
#else
    stream = tmpfile();
#endif
    if (stream == NULL)
        return 0;
    samples[3].ratio = NAN;
    if (f2c_benchmark_write_samples(stream, "TEST", "n=1", samples, 4U) || ftell(stream) != 0L)
        passed = 0;
    samples[3].ratio = 0.5;
    if (f2c_benchmark_write_samples(NULL, "TEST", "n=1", samples, 4U) ||
        f2c_benchmark_write_samples(stream, NULL, "n=1", samples, 4U) ||
        f2c_benchmark_write_samples(stream, "TEST", NULL, samples, 4U) ||
        f2c_benchmark_write_samples(stream, "TEST", "n=1", NULL, 4U) ||
        f2c_benchmark_write_samples(stream, "TEST", "n=1", samples, 0U) ||
        f2c_benchmark_write_samples(stream, "TEST,bad", "n=1", samples, 4U) ||
        f2c_benchmark_write_samples(stream, "TEST", "n=1\n", samples, 4U) ||
        f2c_benchmark_write_samples(stream, "", "n=1", samples, 4U) || ftell(stream) != 0L)
        passed = 0;
    if (!f2c_benchmark_write_samples(stream, "TEST", "n=1", samples, 4U))
        passed = 0;
    rewind(stream);
    for (size_t i = 0U; i < 4U; ++i) {
        if (fgets(line, sizeof(line), stream) == NULL || strcmp(line, expected[i]) != 0 ||
            !close_enough(samples[i].ratio, 0.5))
            passed = 0;
    }
    if (fgetc(stream) != EOF || ferror(stream))
        passed = 0;
    if (fclose(stream) != 0)
        passed = 0;
    return passed;
}

int main(void) {
    F2cBenchmarkSample generated_outer = f2c_benchmark_symmetric_sample(0U, 1.0, 2.0, 3.0, 4.0);
    F2cBenchmarkSample fortran_outer = f2c_benchmark_symmetric_sample(1U, 1.0, 2.0, 6.0, 4.0);
    F2cBenchmarkSample paired_generated = f2c_benchmark_paired_sample(0U, 1.0, 2.0, 3.0, 4.0);
    F2cBenchmarkSample paired_fortran = f2c_benchmark_paired_sample(1U, 2.0, 1.0, 4.0, 6.0);
    F2cBenchmarkSample median_samples[] = {{2.0, 4.0, 0.5}, {6.0, 4.0, 1.5}};
    F2cBenchmarkSample median =
        f2c_benchmark_median(median_samples, sizeof(median_samples) / sizeof(median_samples[0]));

    if (F2C_BENCHMARK_SAMPLE_COUNT != 24)
        return EXIT_FAILURE;
    {
        size_t counts[2][2] = {{0U, 0U}, {0U, 0U}};
        size_t round;
        for (round = 0U; round < F2C_BENCHMARK_SAMPLE_COUNT; ++round) {
            const size_t workspace = f2c_benchmark_use_second_workspace(round) ? 1U : 0U;
            const size_t order = f2c_benchmark_generated_is_outer(round) ? 1U : 0U;
            ++counts[workspace][order];
            if (workspace != (round % 4U >= 2U ? 1U : 0U))
                return EXIT_FAILURE;
        }
        for (size_t workspace = 0U; workspace < 2U; ++workspace)
            for (size_t order = 0U; order < 2U; ++order)
                if (counts[workspace][order] != F2C_BENCHMARK_SAMPLE_COUNT / 4U)
                    return EXIT_FAILURE;
    }
    if (!close_enough(generated_outer.generated_seconds, 5.0) ||
        !close_enough(generated_outer.fortran_seconds, 5.0))
        return EXIT_FAILURE;
    if (!close_enough(fortran_outer.generated_seconds, 8.0) ||
        !close_enough(fortran_outer.fortran_seconds, 5.0))
        return EXIT_FAILURE;
    if (!close_enough(paired_generated.generated_seconds, 5.0) ||
        !close_enough(paired_generated.fortran_seconds, 5.0) ||
        !close_enough(paired_fortran.generated_seconds, 8.0) ||
        !close_enough(paired_fortran.fortran_seconds, 5.0))
        return EXIT_FAILURE;
    if (!close_enough(median.generated_seconds, 4.0) ||
        !close_enough(median.fortran_seconds, 4.0) || !close_enough(median.ratio, 1.0))
        return EXIT_FAILURE;
    if (!f2c_benchmark_sample_valid(&median) || f2c_benchmark_seconds() < 0.0 ||
        !test_sample_report())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
