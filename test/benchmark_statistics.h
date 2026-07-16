#ifndef F2C_TEST_BENCHMARK_STATISTICS_H
#define F2C_TEST_BENCHMARK_STATISTICS_H

#include <math.h>
#include <stddef.h>
#include <time.h>

enum { F2C_BENCHMARK_SAMPLE_COUNT = 11 };

typedef struct F2cBenchmarkSample {
    double generated_seconds;
    double fortran_seconds;
    double ratio;
} F2cBenchmarkSample;

/* An ABBA pair cancels first/second-order effects such as frequency ramping
 * without selecting a favorable run from either implementation. */
static F2cBenchmarkSample f2c_benchmark_abba_sample(double generated_first, double fortran_second,
                                                    double fortran_first, double generated_second) {
    F2cBenchmarkSample sample;
    sample.generated_seconds = generated_first + generated_second;
    sample.fortran_seconds = fortran_first + fortran_second;
    sample.ratio = sample.generated_seconds / sample.fortran_seconds;
    return sample;
}

/* Process CPU time keeps paired single-threaded measurements stable when CI
 * runners are preempted by unrelated workloads. */
static double f2c_benchmark_seconds(void) {
    const clock_t now = clock();
    return now == (clock_t)-1 ? 0.0 : (double)now / (double)CLOCKS_PER_SEC;
}

static int f2c_benchmark_sample_valid(const F2cBenchmarkSample *sample) {
    return sample != NULL && sample->generated_seconds > 0.0 && sample->fortran_seconds > 0.0 &&
           sample->ratio > 0.0 && isfinite(sample->generated_seconds) &&
           isfinite(sample->fortran_seconds) && isfinite(sample->ratio);
}

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
