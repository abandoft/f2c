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

static F2cResult transpile(const char *source, DiagnosticCapture *capture) {
    F2cOptions options = {"declaration.f90", F2C_SOURCE_FREE, 0};
    F2cInput input = {source, strlen(source), options};
    F2cConfig config;
    memset(&config, 0, sizeof(config));
    config.structure_size = sizeof(config);
    config.diagnostic_callback = capture_diagnostic;
    config.diagnostic_user_data = capture;
    return f2c_transpile_project_config(&input, 1U, &config);
}

static void test_duplicate_attribute(void) {
    static const char source[] = "program duplicate_attribute\n"
                                 "  integer, save, save :: value\n"
                                 "end program duplicate_attribute\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "duplicate declaration attributes suppress generated C");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SEMANTIC && capture.line == 2U,
           "duplicate declaration attributes have a stable semantic diagnostic");
    f2c_result_free(&result);
}

static void test_duplicate_shape(void) {
    static const char source[] = "program duplicate_shape\n"
                                 "  real, dimension(2) :: values(2)\n"
                                 "end program duplicate_shape\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "duplicate entity and attribute shapes suppress generated C");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "duplicate entity and attribute shapes have a stable semantic diagnostic");
    f2c_result_free(&result);
}

static void test_continued_initializer_location(void) {
    static const char source[] = "subroutine malformed_initializer\n"
                                 "  integer :: value = 1 + &\n"
                                 "  & )\n"
                                 "end subroutine malformed_initializer\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "malformed continued initializers suppress generated C");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SYNTAX,
           "malformed continued initializers have a stable syntax diagnostic");
    expect(capture.line == 3U && capture.column == 5U && capture.end_column == 6U,
           "initializer diagnostics retain the physical continuation token range");
    f2c_result_free(&result);
}

int main(void) {
    test_duplicate_attribute();
    test_duplicate_shape();
    test_continued_initializer_location();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
