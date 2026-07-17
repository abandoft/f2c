#include "f2c/f2c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DiagnosticCapture {
    int captured;
    F2cDiagnosticCode code;
    F2cDiagnosticSeverity severity;
    size_t line;
    size_t column;
    size_t end_column;
} DiagnosticCapture;

static int failures = 0;

static void capture_diagnostic(const F2cDiagnostic *diagnostic, void *user_data) {
    DiagnosticCapture *capture = (DiagnosticCapture *)user_data;
    if (capture->captured && diagnostic->severity < capture->severity)
        return;
    capture->captured = 1;
    capture->code = diagnostic->code;
    capture->severity = diagnostic->severity;
    capture->line = diagnostic->begin.line;
    capture->column = diagnostic->begin.column;
    capture->end_column = diagnostic->end.column;
}

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static F2cResult transpile_with_diagnostics(const char *source, DiagnosticCapture *capture) {
    F2cOptions options = {"storage.f90", F2C_SOURCE_FREE, 0};
    F2cInput input = {source, strlen(source), options};
    F2cConfig config;
    memset(&config, 0, sizeof(config));
    config.structure_size = sizeof(config);
    config.diagnostic_callback = capture_diagnostic;
    config.diagnostic_user_data = capture;
    return f2c_transpile_project_config(&input, 1U, &config);
}

static void test_equivalence_rank_diagnostic(void) {
    static const char source[] = "program invalid_equivalence\n"
                                 "  real :: values(2), scalar\n"
                                 "  equivalence (values(1, 2), scalar)\n"
                                 "end program invalid_equivalence\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid EQUIVALENCE rank suppresses generated C");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SYNTAX,
           "invalid EQUIVALENCE rank has a stable syntax code");
    expect(capture.line == 3U && capture.column == 26U && capture.end_column == 27U,
           "invalid EQUIVALENCE rank identifies the extra subscript token");
    f2c_result_free(&result);
}

static void test_conflicting_equivalence_groups(void) {
    static const char source[] = "program conflicting_equivalence\n"
                                 "  real :: storage(2), alias(2), other(2)\n"
                                 "  equivalence (storage(1), alias(1))\n"
                                 "  equivalence (other(1), alias(1))\n"
                                 "end program conflicting_equivalence\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "conflicting storage groups suppress generated C");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SEMANTIC && capture.line == 4U,
           "conflicting storage groups have a stable semantic diagnostic");
    f2c_result_free(&result);
}

int main(void) {
    test_equivalence_rank_diagnostic();
    test_conflicting_equivalence_groups();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
