/* Exercise the real sampling loops with small deterministic kernel stubs.
 * Numerical conformance is tested separately against Reference BLAS/LAPACK. */
#include "performance/level1.c"
#include "performance/level3.c"

enum { TRACE_DDOT, TRACE_DNRM2, TRACE_DSCAL, TRACE_DTRSM, TRACE_DSYRK, TRACE_COUNT };

typedef struct WorkspaceTrace {
    double *first;
    double *second;
    size_t calls;
    size_t lane_elements;
    double initial;
} WorkspaceTrace;

static WorkspaceTrace traces[TRACE_COUNT];
static int failures;

static void record_workspace(size_t kernel, int native, double *data) {
    WorkspaceTrace *trace = &traces[kernel];
    const size_t call = trace->calls++;
    size_t sample_call;
    size_t group;
    size_t round;
    int outer;
    int expected_native;
    double *expected;
    if (call < 2U)
        return; /* Correctness checks intentionally retain separate outputs. */
    sample_call = call - 2U;
    group = sample_call / 2U; /* Each test measurement has two repetitions. */
    round = group / 4U;
    outer = group % 4U == 0U || group % 4U == 3U;
    expected_native = f2c_benchmark_generated_is_outer(round) ? !outer : outer;
    expected = f2c_benchmark_use_second_workspace(round) ? trace->second : trace->first;
    if (kernel == TRACE_DTRSM)
        expected += (sample_call % 2U) * trace->lane_elements;
    if (data != expected || native != expected_native)
        ++failures;
    if ((kernel == TRACE_DTRSM || sample_call % 2U == 0U) &&
        !f2c_benchmark_close(data[0], trace->initial, 1.0e-15))
        ++failures; /* Every measurement starts with restored input, outside timing. */
}

static double dot_stub(int native, int32_t *n, double *x, int32_t *incx, double *y, int32_t *incy) {
    double sum = 0.0;
    record_workspace(TRACE_DDOT, native, x);
    for (int32_t i = 0; i < *n; ++i)
        sum += x[i * *incx] * y[i * *incy];
    return sum;
}

double ddot(int32_t *n, double *x, int32_t *incx, double *y, int32_t *incy) {
    return dot_stub(0, n, x, incx, y, incy);
}

double ddot_(int32_t *n, double *x, int32_t *incx, double *y, int32_t *incy) {
    return dot_stub(1, n, x, incx, y, incy);
}

static double norm_stub(int native, int32_t *n, double *x, int32_t *stride) {
    double sum = 0.0;
    record_workspace(TRACE_DNRM2, native, x);
    for (int32_t i = 0; i < *n; ++i)
        sum += x[i * *stride] * x[i * *stride];
    return sqrt(sum);
}

double dnrm2(int32_t *n, double *x, int32_t *stride) { return norm_stub(0, n, x, stride); }
double dnrm2_(int32_t *n, double *x, int32_t *stride) { return norm_stub(1, n, x, stride); }

static void scale_stub(int native, int32_t *n, double *alpha, double *x, int32_t *stride) {
    record_workspace(TRACE_DSCAL, native, x);
    for (int32_t i = 0; i < *n; ++i)
        x[i * *stride] *= *alpha;
}

void dscal(int32_t *n, double *alpha, double *x, int32_t *stride) {
    scale_stub(0, n, alpha, x, stride);
}

void dscal_(int32_t *n, double *alpha, double *x, int32_t *stride) {
    scale_stub(1, n, alpha, x, stride);
}

#define DEFINE_TRSM_STUB(name, native)                                                             \
    void name(char *side, char *uplo, char *trans, char *diag, int32_t *m, int32_t *n,             \
              double *alpha, double *a, int32_t *lda, double *b, int32_t *ldb, size_t side_len,    \
              size_t uplo_len, size_t trans_len, size_t diag_len) {                                \
        (void)side;                                                                                \
        (void)uplo;                                                                                \
        (void)trans;                                                                               \
        (void)diag;                                                                                \
        (void)a;                                                                                   \
        (void)lda;                                                                                 \
        (void)ldb;                                                                                 \
        (void)side_len;                                                                            \
        (void)uplo_len;                                                                            \
        (void)trans_len;                                                                           \
        (void)diag_len;                                                                            \
        record_workspace(TRACE_DTRSM, native, b);                                                  \
        for (size_t i = 0U; i < (size_t)*m * (size_t)*n; ++i)                                      \
            b[i] *= *alpha;                                                                        \
    }

DEFINE_TRSM_STUB(dtrsm, 0)
DEFINE_TRSM_STUB(dtrsm_, 1)

#define DEFINE_SYRK_STUB(name, native)                                                             \
    void name(char *uplo, char *trans, int32_t *n, int32_t *k, double *alpha, double *a,           \
              int32_t *lda, double *beta, double *c, int32_t *ldc, size_t uplo_len,                \
              size_t trans_len) {                                                                  \
        (void)uplo;                                                                                \
        (void)trans;                                                                               \
        (void)k;                                                                                   \
        (void)lda;                                                                                 \
        (void)ldc;                                                                                 \
        (void)uplo_len;                                                                            \
        (void)trans_len;                                                                           \
        record_workspace(TRACE_DSYRK, native, c);                                                  \
        for (size_t i = 0U; i < (size_t)*n * (size_t)*n; ++i)                                      \
            c[i] = *alpha * a[i] + *beta * c[i];                                                   \
    }

DEFINE_SYRK_STUB(dsyrk, 0)
DEFINE_SYRK_STUB(dsyrk_, 1)

int main(void) {
    double x[16] = {0};
    double y[16] = {0};
    double reference[16] = {0};
    double a[16] = {0};
    double input[16] = {0};
    double first[16U * F2C_DTRSM_BATCH_SIZE] = {0};
    double second[16U * F2C_DTRSM_BATCH_SIZE] = {0};
    const Level1Case level1 = {8, 2, 2};
    const Level3Case level3 = {4, 'R', 'N', 2};
    for (size_t kernel = TRACE_DDOT; kernel <= TRACE_DSCAL; ++kernel)
        traces[kernel] = (WorkspaceTrace){x, reference, 0U, 0U, f2c_benchmark_value(0U, 7)};
    traces[TRACE_DTRSM] = (WorkspaceTrace){first, second, 0U, 16U, f2c_benchmark_value(0U, 29)};
    traces[TRACE_DSYRK] = (WorkspaceTrace){first, second, 0U, 0U, f2c_benchmark_value(0U, 43)};
    (void)run_case(&level1, x, y, reference);
    (void)run_dtrsm(&level3, a, input, first, second);
    (void)run_dsyrk(&level3, a, first, second);
    for (size_t kernel = 0U; kernel < TRACE_COUNT; ++kernel)
        if (traces[kernel].calls != 2U + F2C_BENCHMARK_SAMPLE_COUNT * 4U * 2U)
            ++failures;
    if (failures != 0)
        fprintf(stderr, "paired workspace contract failed %d times\n", failures);
    return failures != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
