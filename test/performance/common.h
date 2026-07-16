#ifndef F2C_TEST_PERFORMANCE_COMMON_H
#define F2C_TEST_PERFORMANCE_COMMON_H

#include "../benchmark_statistics.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static inline double f2c_benchmark_value(size_t index, int offset) {
    return (double)(((int32_t)((index + (size_t)offset) % 251U)) - 125) / 251.0;
}

static inline int f2c_benchmark_close(double generated, double native, double tolerance) {
    const double scale = fmax(1.0, fabs(native));
    return fabs(generated - native) <= tolerance * scale;
}

static inline int f2c_benchmark_report(const char *kernel, const char *description,
                                       F2cBenchmarkSample *samples, size_t count) {
    const F2cBenchmarkSample result = f2c_benchmark_median(samples, count);
    printf("%s %s: generated C %.6fs, Fortran %.6fs, ratio %.3f\n", kernel, description,
           result.generated_seconds, result.fortran_seconds, result.ratio);
    printf("F2C_PERF,%s,%s,%.9f,%.9f,%.6f\n", kernel, description, result.generated_seconds,
           result.fortran_seconds, result.ratio);
    return f2c_benchmark_sample_valid(&result);
}

#endif
