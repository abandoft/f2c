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

static void test_blank_common_and_mixed_blocks(void) {
    static const char source[] = "subroutine write_storage(value)\n"
                                 "  integer :: values(2), total, control, value\n"
                                 "  character(len=4) :: label\n"
                                 "  common values, total /named/ control\n"
                                 "  common // label\n"
                                 "  values = (/ value, value + 1 /)\n"
                                 "  total = values(1) + values(2)\n"
                                 "  control = total + 1\n"
                                 "  label = 'done'\n"
                                 "end subroutine write_storage\n"
                                 "subroutine clear_storage()\n"
                                 "  integer :: entries(2), sum, mode\n"
                                 "  character(len=4) :: text\n"
                                 "  common entries, sum /named/ mode\n"
                                 "  common // text\n"
                                 "  entries = 0\n"
                                 "  sum = 0\n"
                                 "  mode = 0\n"
                                 "  text = '    '\n"
                                 "end subroutine clear_storage\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.error_count == 0U,
           "blank and named COMMON blocks may coexist in one declaration sequence");
    expect(result.code != NULL && strstr(result.code, "struct f2c_blank_common") != NULL,
           "blank COMMON has stable shared generated storage");
    expect(result.code != NULL && strstr(result.code, "struct f2c_common_named") != NULL,
           "a following named COMMON block retains independent storage");
    f2c_result_free(&result);
}

static void test_common_storage_mismatch_diagnostic(void) {
    static const char source[] = "subroutine integer_view()\n"
                                 "  integer :: value\n"
                                 "  common /shared/ value\n"
                                 "end subroutine integer_view\n"
                                 "subroutine real_view()\n"
                                 "  real :: value\n"
                                 "  common /shared/ value\n"
                                 "end subroutine real_view\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "incompatible cross-unit COMMON layouts suppress generated C");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SEMANTIC && capture.line == 7U &&
               capture.column == 19U,
           "COMMON layout mismatch reports the incompatible member span");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, "storage-incompatible") != NULL,
           "COMMON layout mismatch explains the storage contract");
    f2c_result_free(&result);
}

static void test_block_data_common_initialization(void) {
    static const char source[] =
        "program inspect_state\n"
        "  implicit none\n"
        "  integer :: counter, values(3)\n"
        "  complex :: point\n"
        "  integer :: declaration_value\n"
        "  character(len=4) :: label\n"
        "  common /state/ counter, values, point, declaration_value, label\n"
        "  print *, counter, values, point, declaration_value, label\n"
        "end program inspect_state\n"
        "block data initialize_state\n"
        "  implicit none\n"
        "  integer :: counter, values(3)\n"
        "  complex :: point\n"
        "  integer :: declaration_value = 19\n"
        "  character(len=4) :: label\n"
        "  common /state/ counter, values, point, declaration_value, label\n"
        "  data counter / 7 /, values / 11, 13, 17 /\n"
        "  data point / (1.25, -0.5) /, label / 'ok' /\n"
        "end block data initialize_state\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.error_count == 0U && result.code != NULL,
           "BLOCK DATA initializes named COMMON storage at translation time");
    expect(result.code != NULL &&
               strstr(result.code, "F2C_COMMON_INITIALIZED_STORAGE struct f2c_common_state") !=
                   NULL &&
               strstr(result.code, ".field_0 = INT64_C(7)") != NULL &&
               strstr(result.code, ".field_1 = {INT64_C(11), INT64_C(13), INT64_C(17)}") != NULL &&
               strstr(result.code, ".field_2 = F2C_COMPLEX_FLOAT_INITIALIZER") != NULL &&
               strstr(result.code, ".field_3 = INT64_C(19)") != NULL &&
               strstr(result.code, ".field_4 = \"ok  \"") != NULL,
           "COMMON fields receive portable scalar, array, COMPLEX, and CHARACTER initializers");
    expect(result.code != NULL && strstr(result.code, "initialize_state(") == NULL,
           "BLOCK DATA is not emitted as a callable C procedure");
    f2c_result_free(&result);
}

static void test_block_data_constraints(void) {
    static const char invalid_owner[] = "program invalid_owner\n"
                                        "  integer :: value\n"
                                        "  common /state/ value\n"
                                        "  data value / 1 /\n"
                                        "end program invalid_owner\n";
    static const char invalid_local[] = "block data invalid_local\n"
                                        "  integer :: value\n"
                                        "  data value / 1 /\n"
                                        "end block data invalid_local\n";
    static const char invalid_declaration_owner[] = "program invalid_declaration_owner\n"
                                                    "  integer :: value = 1\n"
                                                    "  common /state/ value\n"
                                                    "end program invalid_declaration_owner\n";
    static const char invalid_blank[] = "block data invalid_blank\n"
                                        "  integer :: value\n"
                                        "  common value\n"
                                        "  data value / 1 /\n"
                                        "end block data invalid_blank\n";
    static const char invalid_executable[] = "block data invalid_executable\n"
                                             "  integer :: value\n"
                                             "  common /state/ value\n"
                                             "  value = 1\n"
                                             "end block data invalid_executable\n";
    static const char duplicate_owner[] = "block data first_owner\n"
                                          "  integer :: first, second\n"
                                          "  common /state/ first, second\n"
                                          "  data first / 1 /\n"
                                          "end block data first_owner\n"
                                          "block data second_owner\n"
                                          "  integer :: first, second\n"
                                          "  common /state/ first, second\n"
                                          "  data second / 2 /\n"
                                          "end block data second_owner\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(invalid_owner, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "only in a BLOCK DATA program unit") != NULL,
           "ordinary program units cannot initialize COMMON with DATA");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(invalid_declaration_owner, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "may be initialized only in BLOCK DATA") != NULL,
           "declaration initialization of COMMON also requires BLOCK DATA ownership");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(invalid_local, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "must belong to a named COMMON block") != NULL,
           "BLOCK DATA cannot initialize private local storage");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(invalid_blank, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "must belong to a named COMMON block") != NULL,
           "BLOCK DATA cannot initialize blank COMMON");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(invalid_executable, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "specification statements and DATA initialization") !=
                   NULL,
           "BLOCK DATA rejects executable statements");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(duplicate_owner, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "more than one BLOCK DATA program unit") != NULL,
           "one named COMMON block has a single BLOCK DATA initialization owner");
    f2c_result_free(&result);
}

int main(void) {
    test_equivalence_rank_diagnostic();
    test_conflicting_equivalence_groups();
    test_blank_common_and_mixed_blocks();
    test_common_storage_mismatch_diagnostic();
    test_block_data_common_initialization();
    test_block_data_constraints();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
