#include "benchmark_statistics.h"

#include <stdlib.h>

static int close_enough(double left, double right) { return fabs(left - right) < 1.0e-12; }

int main(void) {
    F2cBenchmarkSample generated_outer = f2c_benchmark_symmetric_sample(0U, 1.0, 2.0, 3.0, 4.0);
    F2cBenchmarkSample fortran_outer = f2c_benchmark_symmetric_sample(1U, 1.0, 2.0, 6.0, 4.0);
    F2cBenchmarkSample abba = f2c_benchmark_abba_sample(2.0, 3.0, 5.0, 7.0);
    F2cBenchmarkSample median_samples[] = {{2.0, 4.0, 0.5}, {6.0, 4.0, 1.5}};
    F2cBenchmarkSample median =
        f2c_benchmark_median(median_samples, sizeof(median_samples) / sizeof(median_samples[0]));

    if (F2C_BENCHMARK_SAMPLE_COUNT != 12)
        return EXIT_FAILURE;
    if (!close_enough(generated_outer.generated_seconds, 5.0) ||
        !close_enough(generated_outer.fortran_seconds, 5.0))
        return EXIT_FAILURE;
    if (!close_enough(fortran_outer.generated_seconds, 8.0) ||
        !close_enough(fortran_outer.fortran_seconds, 5.0))
        return EXIT_FAILURE;
    if (!close_enough(abba.generated_seconds, 9.0) || !close_enough(abba.fortran_seconds, 8.0))
        return EXIT_FAILURE;
    if (!close_enough(median.generated_seconds, 4.0) ||
        !close_enough(median.fortran_seconds, 4.0) || !close_enough(median.ratio, 1.0))
        return EXIT_FAILURE;
    if (!f2c_benchmark_sample_valid(&median) || f2c_benchmark_seconds() < 0.0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
