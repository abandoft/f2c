#include "f2c/f2c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DiagnosticCapture {
    size_t count;
    F2cDiagnosticCode code;
    F2cDiagnosticSeverity severity;
    size_t line;
    size_t column;
    size_t end_column;
} DiagnosticCapture;

static int failures = 0;

static void capture_diagnostic(const F2cDiagnostic *diagnostic, void *user_data) {
    DiagnosticCapture *capture = (DiagnosticCapture *)user_data;
    if (diagnostic->severity != F2C_DIAGNOSTIC_ERROR)
        return;
    ++capture->count;
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

static void expect_contains(const char *text, const char *needle, const char *message) {
    expect(text != NULL && strstr(text, needle) != NULL, message);
}

static F2cResult transpile(const char *source, const char *name, DiagnosticCapture *capture) {
    F2cOptions options = {name, F2C_SOURCE_FREE, 0};
    F2cInput input = {source, strlen(source), options};
    F2cConfig config;
    memset(&config, 0, sizeof(config));
    config.structure_size = sizeof(config);
    config.diagnostic_callback = capture_diagnostic;
    config.diagnostic_user_data = capture;
    return f2c_transpile_project_config(&input, 1U, &config);
}

static void test_tokenized_type_and_range_mapping(void) {
    static const char source[] = "subroutine mapped(alpha, index)\n"
                                 "  implicit real(kind=8) (a-b, d-h, o-z), integer*8 (i-n), &\n"
                                 "  & character(len=2+2) (c)\n"
                                 "  cword = 'ok'\n"
                                 "end subroutine mapped\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, "implicit_mapping.f90", &capture);
    expect(result.error_count == 0U && capture.count == 0U,
           "continued tokenized IMPLICIT mappings translate without errors");
    expect_contains(result.header, "void mapped(double *alpha, int64_t *index);",
                    "IMPLICIT selectors define the public scalar ABI");
    expect_contains(result.code, "char cword[",
                    "an implicit CHARACTER local receives concrete generated storage");
    expect_contains(result.code, "(2 + 2)",
                    "implicit CHARACTER length remains a typed specification expression");
    f2c_result_free(&result);
}

static void test_overlap_range_location(void) {
    static const char source[] = "subroutine overlap()\n"
                                 "  implicit integer (a-c), real (c-z)\n"
                                 "end subroutine overlap\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, "implicit_overlap.f90", &capture);
    expect(result.code == NULL && result.error_count == 1U && capture.count == 1U,
           "an overlapping IMPLICIT map fails once and emits no partial C");
    expect(capture.code == F2C_DIAGNOSTIC_SEMANTIC && capture.line == 2U && capture.column == 33U &&
               capture.end_column == 34U,
           "overlap diagnostics identify the exact conflicting token span");
    f2c_result_free(&result);
}

static void test_none_duplicate_is_atomic(void) {
    static const char source[] = "subroutine duplicate_none()\n"
                                 "  implicit none(type, type)\n"
                                 "end subroutine duplicate_none\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, "implicit_none_duplicate.f90", &capture);
    expect(result.code == NULL && result.error_count == 1U && capture.count == 1U,
           "a duplicate IMPLICIT NONE specifier produces one atomic failure");
    expect(capture.code == F2C_DIAGNOSTIC_SEMANTIC && capture.line == 2U && capture.column == 23U,
           "duplicate NONE diagnostics use a stable code and exact second-specifier span");
    f2c_result_free(&result);
}

static void test_descending_range_syntax(void) {
    static const char source[] = "subroutine descending()\n"
                                 "  implicit integer (z-a)\n"
                                 "end subroutine descending\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, "implicit_descending.f90", &capture);
    expect(result.code == NULL && result.error_count == 1U,
           "a descending IMPLICIT range is rejected before emission");
    expect(capture.code == F2C_DIAGNOSTIC_SYNTAX && capture.line == 2U && capture.column == 21U,
           "descending ranges retain their physical source position and syntax code");
    expect_contains(result.diagnostics, "invalid IMPLICIT letter range 'z-a'",
                    "descending-range diagnostics preserve the offending range text");
    f2c_result_free(&result);
}

int main(void) {
    test_tokenized_type_and_range_mapping();
    test_overlap_range_location();
    test_none_duplicate_is_atomic();
    test_descending_range_syntax();
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all implicit semantic tests passed");
    return EXIT_SUCCESS;
}
