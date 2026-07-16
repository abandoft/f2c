#ifndef F2C_TEST_BENCHMARK_STATISTICS_H
#define F2C_TEST_BENCHMARK_STATISTICS_H

#include <stddef.h>

enum { F2C_BENCHMARK_SAMPLE_COUNT = 11 };

typedef struct F2cBenchmarkSample {
    double generated_seconds;
    double fortran_seconds;
    double ratio;
} F2cBenchmarkSample;

static F2cBenchmarkSample f2c_benchmark_median(F2cBenchmarkSample *samples, size_t count) {
    size_t i;
    for (i = 1U; i < count; ++i) {
        F2cBenchmarkSample value = samples[i];
        size_t position = i;
        while (position != 0U && samples[position - 1U].ratio > value.ratio) {
            samples[position] = samples[position - 1U];
            --position;
        }
        samples[position] = value;
    }
    return samples[count / 2U];
}

#endif
