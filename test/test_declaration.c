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

static void test_attribute_keywords_as_identifiers(void) {
    static const char source[] =
        "program keyword_identifiers\n"
        "  implicit none\n"
        "  integer :: dimension, external, parameter, save, equivalence\n"
        "  dimension = 1\n"
        "  external = 2\n"
        "  parameter = 3\n"
        "  save = 4\n"
        "  equivalence = 5\n"
        "  if (dimension + external + parameter + save + equivalence /= 15) stop 1\n"
        "end program keyword_identifiers\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, &capture);
    expect(result.code != NULL && result.error_count == 0U,
           "attribute keywords remain legal identifiers in executable assignments");
    expect(!capture.captured,
           "keyword-named variables are classified from token context without diagnostics");
    f2c_result_free(&result);
}

static void test_selected_kind_type_selectors(void) {
    static const char source[] = "program selected_kind_type_selectors\n"
                                 "  implicit none\n"
                                 "  integer, parameter :: wide_integer = selected_int_kind(18)\n"
                                 "  integer(kind=wide_integer) :: integer_value\n"
                                 "  real(selected_real_kind(6)) :: single_value\n"
                                 "  real(kind=selected_real_kind(r=307)) :: double_value\n"
                                 "  integer_value = 1_wide_integer\n"
                                 "  single_value = 2.0\n"
                                 "  double_value = 3.0d0\n"
                                 "end program selected_kind_type_selectors\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, &capture);
    expect(result.code != NULL && result.error_count == 0U,
           "SELECTED_*_KIND constants are accepted in declaration type selectors");
    expect(result.code != NULL && strstr(result.code, "int64_t integer_value") != NULL,
           "SELECTED_INT_KIND drives the declared INTEGER width");
    expect(result.code != NULL && strstr(result.code, "float single_value") != NULL,
           "precision-only SELECTED_REAL_KIND selects binary32");
    expect(result.code != NULL && strstr(result.code, "double double_value") != NULL,
           "range-only SELECTED_REAL_KIND selects binary64");
    expect(!capture.captured, "valid selected-kind type selectors have no diagnostics");
    f2c_result_free(&result);
}

static void test_contiguous_attribute(void) {
    static const char source[] = "subroutine contiguous_contract(values, pointer_values)\n"
                                 "  implicit none\n"
                                 "  integer, contiguous, intent(inout) :: values(:)\n"
                                 "  integer, pointer, intent(inout) :: pointer_values(:)\n"
                                 "  contiguous :: pointer_values\n"
                                 "end subroutine contiguous_contract\n";
    static const char scalar_source[] = "subroutine scalar_contiguous(value)\n"
                                        "  integer, contiguous :: value\n"
                                        "end subroutine scalar_contiguous\n";
    static const char local_source[] = "program local_contiguous\n"
                                       "  integer, contiguous :: values(4)\n"
                                       "end program local_contiguous\n";
    static const char interface_source[] = "program contiguous_interface_caller\n"
                                           "  interface\n"
                                           "    subroutine consume(values)\n"
                                           "      integer, contiguous :: values(:)\n"
                                           "    end subroutine consume\n"
                                           "  end interface\n"
                                           "  integer :: values(2)\n"
                                           "  call consume(values)\n"
                                           "end program contiguous_interface_caller\n";
    static const char definition_source[] = "subroutine consume(values)\n"
                                            "  integer :: values(:)\n"
                                            "end subroutine consume\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, &capture);
    expect(result.code != NULL && result.error_count == 0U,
           "CONTIGUOUS accepts assumed-shape dummies and array pointers");
    expect(result.code != NULL && strstr(result.code, "f2c_descriptor_is_contiguous") != NULL,
           "CONTIGUOUS dummy descriptors enforce their ABI contract at procedure entry");
    expect(!capture.captured, "valid CONTIGUOUS declarations have no diagnostics");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile(scalar_source, &capture);
    expect(result.code == NULL && result.error_count != 0U, "CONTIGUOUS rejects scalar entities");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile(local_source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "CONTIGUOUS rejects ordinary explicit-shape local arrays");
    f2c_result_free(&result);

    {
        F2cInput inputs[2] = {{interface_source,
                               sizeof(interface_source) - 1U,
                               {"contiguous_interface_caller.f90", F2C_SOURCE_FREE, 0}},
                              {definition_source,
                               sizeof(definition_source) - 1U,
                               {"consume.f90", F2C_SOURCE_FREE, 0}}};
        result = f2c_transpile_project(inputs, 2U);
        expect(result.code == NULL && result.error_count != 0U,
               "project interfaces reject mismatched CONTIGUOUS attributes");
        expect(result.diagnostics != NULL && strstr(result.diagnostics, "incompatible") != NULL,
               "CONTIGUOUS interface mismatch has an actionable diagnostic");
        f2c_result_free(&result);
    }
}

static void test_assumed_size_declaration_context(void) {
    static const char source[] = "program local_assumed_size\n"
                                 "  integer :: values(*)\n"
                                 "end program local_assumed_size\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "assumed-size arrays are rejected outside dummy arguments");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "invalid assumed-size declaration has a stable semantic diagnostic");
    f2c_result_free(&result);
}

int main(void) {
    test_duplicate_attribute();
    test_duplicate_shape();
    test_continued_initializer_location();
    test_attribute_keywords_as_identifiers();
    test_selected_kind_type_selectors();
    test_contiguous_attribute();
    test_assumed_size_declaration_context();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
