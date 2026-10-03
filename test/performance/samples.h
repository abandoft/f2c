#ifndef F2C_TEST_PERFORMANCE_SAMPLES_H
#define F2C_TEST_PERFORMANCE_SAMPLES_H

#include "../benchmark_statistics.h"

#include <stdio.h>
#include <string.h>

/* Write only after all timed rounds have finished, before median selection
 * reorders the samples. Preserve collection order and full double precision. */
static inline int f2c_benchmark_write_samples(FILE *stream, const char *kernel,
                                              const char *description,
                                              const F2cBenchmarkSample *samples, size_t count) {
    if (stream == NULL || kernel == NULL || description == NULL || samples == NULL || count == 0U ||
        kernel[0] == '\0' || description[0] == '\0' || strpbrk(kernel, ",\r\n") != NULL ||
        strpbrk(description, ",\r\n") != NULL)
        return 0;
    for (size_t round = 0U; round < count; ++round)
        if (!f2c_benchmark_sample_valid(&samples[round]))
            return 0;
    for (size_t round = 0U; round < count; ++round) {
        if (fprintf(stream, "F2C_PERF_SAMPLE,%s,%s,%zu,%s,%d,%.17g,%.17g,%.17g\n", kernel,
                    description, round, f2c_benchmark_generated_is_outer(round) ? "ABBA" : "BAAB",
                    f2c_benchmark_use_second_workspace(round), samples[round].generated_seconds,
                    samples[round].fortran_seconds, samples[round].ratio) < 0)
            return 0;
    }
    return !ferror(stream);
}

#endif
