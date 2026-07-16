#ifndef F2C_TEST_BENCHMARK_STATISTICS_H
#define F2C_TEST_BENCHMARK_STATISTICS_H

#include <math.h>
#include <stddef.h>
#include <time.h>

enum { F2C_BENCHMARK_SAMPLE_COUNT = 12 };

typedef struct F2cBenchmarkSample {
    double generated_seconds;
    double fortran_seconds;
    double ratio;
} F2cBenchmarkSample;

/* Alternating ABBA and BAAB pairs balance nonlinear frequency and thermal
 * effects without selecting a favorable run from either implementation. */
static inline int f2c_benchmark_generated_is_outer(size_t round) { return (round & 1U) == 0U; }

static inline F2cBenchmarkSample f2c_benchmark_symmetric_sample(size_t round, double outer_first,
                                                                double inner_first,
                                                                double inner_second,
                                                                double outer_second) {
    F2cBenchmarkSample sample;
    if (f2c_benchmark_generated_is_outer(round)) {
        sample.generated_seconds = outer_first + outer_second;
        sample.fortran_seconds = inner_first + inner_second;
    } else {
        sample.generated_seconds = inner_first + inner_second;
        sample.fortran_seconds = outer_first + outer_second;
    }
    sample.ratio = sample.generated_seconds / sample.fortran_seconds;
    return sample;
}

/* Collect implementation-specific timings after the caller has executed the
 * ABBA or BAAB order selected by f2c_benchmark_generated_is_outer(). */
static inline F2cBenchmarkSample f2c_benchmark_paired_sample(
    size_t round, double generated_first, double fortran_first, double fortran_second,
    double generated_second) {
    const int generated_outer = f2c_benchmark_generated_is_outer(round);
    return f2c_benchmark_symmetric_sample(
        round, generated_outer ? generated_first : fortran_first,
        generated_outer ? fortran_first : generated_first,
        generated_outer ? fortran_second : generated_second,
        generated_outer ? generated_second : fortran_second);
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
    F2cBenchmarkSample result;
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
    if ((count & 1U) != 0U)
        return samples[count / 2U];
    result.generated_seconds =
        (samples[count / 2U - 1U].generated_seconds + samples[count / 2U].generated_seconds) / 2.0;
    result.fortran_seconds =
        (samples[count / 2U - 1U].fortran_seconds + samples[count / 2U].fortran_seconds) / 2.0;
    result.ratio = result.generated_seconds / result.fortran_seconds;
    return result;
}

#endif
