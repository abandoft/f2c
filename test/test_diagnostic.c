#include "f2c/f2c.h"

#include <stdio.h>
#include <string.h>

typedef struct DiagnosticCapture {
    const char *needle;
    size_t count;
    F2cDiagnosticCode code;
    F2cSourceLocation begin;
    F2cSourceLocation end;
    char source_name[64];
} DiagnosticCapture;

static int failures;

static int message_contains(const F2cDiagnostic *diagnostic, const char *needle) {
    const size_t needle_length = strlen(needle);
    size_t offset;
    if (needle_length > diagnostic->message_length)
        return 0;
    for (offset = 0U; offset <= diagnostic->message_length - needle_length; ++offset) {
        if (memcmp(diagnostic->message + offset, needle, needle_length) == 0)
            return 1;
    }
    return 0;
}

static void capture_diagnostic(const F2cDiagnostic *diagnostic, void *user_data) {
    DiagnosticCapture *capture = (DiagnosticCapture *)user_data;
    if (!message_contains(diagnostic, capture->needle))
        return;
    ++capture->count;
    capture->code = diagnostic->code;
    capture->begin = diagnostic->begin;
    capture->end = diagnostic->end;
    if (diagnostic->begin.source_name != NULL) {
        const size_t source_length = strlen(diagnostic->begin.source_name);
        const size_t copied = source_length < sizeof(capture->source_name) - 1U
                                  ? source_length
                                  : sizeof(capture->source_name) - 1U;
        memcpy(capture->source_name, diagnostic->begin.source_name, copied);
        capture->source_name[copied] = '\0';
    }
}

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static F2cResult transpile(const char *source, DiagnosticCapture *capture) {
    F2cOptions options = {"diagnostic.f90", F2C_SOURCE_FREE, 0};
    F2cInput input = {source, strlen(source), options};
    F2cConfig config;
    memset(&config, 0, sizeof(config));
    config.structure_size = sizeof(config);
    config.diagnostic_callback = capture_diagnostic;
    config.diagnostic_user_data = capture;
    return f2c_transpile_project_config(&input, 1U, &config);
}

static void test_keyword_association_span(void) {
    static const char source[] = "program exact_keyword\n"
                                 "  implicit none\n"
                                 "  interface\n"
                                 "    subroutine target(value)\n"
                                 "      integer, intent(in) :: value\n"
                                 "    end subroutine target\n"
                                 "  end interface\n"
                                 "  call target(target=1)\n"
                                 "end program exact_keyword\n";
    DiagnosticCapture capture = {.needle = "has no dummy argument"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid keyword association suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "invalid keyword association emits one typed semantic diagnostic");
    expect(capture.begin.line == 8U && capture.begin.column == 15U && capture.end.line == 8U &&
               capture.end.column == 23U,
           "keyword diagnostics use the association AST span instead of the earlier call name");
    expect(strcmp(capture.source_name, "diagnostic.f90") == 0,
           "keyword diagnostics retain the canonical source name");
    f2c_result_free(&result);
}

static void test_call_designator_span(void) {
    static const char source[] = "program exact_call\n"
                                 "  implicit none\n"
                                 "  interface\n"
                                 "    subroutine target(value)\n"
                                 "      integer, intent(in) :: value\n"
                                 "    end subroutine target\n"
                                 "  end interface\n"
                                 "  call target()\n"
                                 "end program exact_call\n";
    DiagnosticCapture capture = {.needle = "has no actual argument"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "missing required actual argument suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "missing actual argument emits one typed semantic diagnostic");
    expect(capture.begin.line == 8U && capture.begin.column == 8U && capture.end.line == 8U &&
               capture.end.column == 14U,
           "call diagnostics use the exact procedure designator token span");
    f2c_result_free(&result);
}

static void test_continued_dummy_span(void) {
    static const char source[] = "subroutine repeated(first, &\n"
                                 " & second, first)\n"
                                 "end subroutine repeated\n";
    DiagnosticCapture capture = {.needle = "duplicate dummy argument"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "duplicate continued dummy suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "duplicate dummy emits one typed semantic diagnostic");
    expect(capture.begin.line == 2U && capture.begin.column == 12U && capture.end.line == 2U &&
               capture.end.column == 17U,
           "continued header diagnostics retain the repeated dummy's physical token span");
    f2c_result_free(&result);
}

static void test_result_name_span(void) {
    static const char source[] = "integer function compute(value) result(compute)\n"
                                 "  integer, intent(in) :: value\n"
                                 "end function compute\n";
    DiagnosticCapture capture = {.needle = "FUNCTION result name"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "conflicting function result name suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "conflicting function result emits one typed semantic diagnostic");
    expect(capture.begin.line == 1U && capture.begin.column == 40U && capture.end.line == 1U &&
               capture.end.column == 47U,
           "result-name diagnostics use the RESULT designator rather than the function name");
    f2c_result_free(&result);
}

static void test_trailing_header_token_span(void) {
    static const char source[] = "integer function compute() result(answer) junk\n"
                                 "end function compute\n";
    DiagnosticCapture capture = {.needle = "unexpected tokens after program-unit header"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "trailing header syntax suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SYNTAX,
           "trailing header token emits one typed syntax diagnostic");
    expect(capture.begin.line == 1U && capture.begin.column == 43U && capture.end.line == 1U &&
               capture.end.column == 47U,
           "trailing header diagnostics use the first unconsumed token span");
    f2c_result_free(&result);
}

static void test_continued_procedure_attribute_span(void) {
    static const char source[] = "subroutine duplicate_attribute()\n"
                                 "  procedure(operation), pointer, &\n"
                                 "   & pointer :: callback\n"
                                 "end subroutine duplicate_attribute\n";
    DiagnosticCapture capture = {.needle = "duplicate pointer attribute"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "duplicate continued PROCEDURE attribute suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "duplicate PROCEDURE attribute emits one typed semantic diagnostic");
    expect(capture.begin.line == 3U && capture.begin.column == 6U && capture.end.line == 3U &&
               capture.end.column == 13U,
           "continued PROCEDURE diagnostics retain the repeated attribute's physical span");
    f2c_result_free(&result);
}

static void test_unit_end_name_span(void) {
    static const char source[] = "subroutine alpha()\n"
                                 "end subroutine beta\n";
    DiagnosticCapture capture = {.needle = "END name 'beta'"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "mismatched unit END name suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "mismatched unit END name emits one typed semantic diagnostic");
    expect(capture.begin.line == 2U && capture.begin.column == 16U && capture.end.line == 2U &&
               capture.end.column == 20U,
           "unit END name diagnostic uses the closing designator span");
    f2c_result_free(&result);
}

static void test_unit_end_kind_span(void) {
    static const char source[] = "subroutine alpha()\n"
                                 "end function alpha\n";
    DiagnosticCapture capture = {.needle = "END kind does not match"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "mismatched unit END kind suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "mismatched unit END kind emits one typed semantic diagnostic");
    expect(capture.begin.line == 2U && capture.begin.column == 5U && capture.end.line == 2U &&
               capture.end.column == 13U,
           "unit END kind diagnostic uses the explicit closing-kind span");
    f2c_result_free(&result);
}

static void test_module_end_name_span(void) {
    static const char source[] = "module alpha\n"
                                 "end module beta\n";
    DiagnosticCapture capture = {.needle = "END name 'beta'"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "mismatched module END name suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "mismatched module END name emits one typed semantic diagnostic");
    expect(capture.begin.line == 2U && capture.begin.column == 12U && capture.end.line == 2U &&
               capture.end.column == 16U,
           "module END name diagnostic uses the closing module designator span");
    f2c_result_free(&result);
}

static void test_module_header_span(void) {
    static const char source[] = "module alpha junk\n"
                                 "end module alpha\n";
    DiagnosticCapture capture = {.needle = "MODULE statement requires exactly one valid name"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "malformed module header suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SYNTAX,
           "malformed module header emits one typed syntax diagnostic");
    expect(capture.begin.line == 1U && capture.begin.column == 14U && capture.end.line == 1U &&
               capture.end.column == 18U,
           "module-header diagnostics use the first unconsumed token span");
    f2c_result_free(&result);
}

static void test_internal_procedure_end_scope(void) {
    static const char source[] = "module procedures\n"
                                 "contains\n"
                                 "  subroutine first()\n"
                                 "  end subroutine first\n"
                                 "  subroutine second()\n"
                                 "  end subroutine second\n"
                                 "end module procedures\n";
    DiagnosticCapture capture = {.needle = "program-unit END kind does not match"};
    F2cResult result = transpile(source, &capture);
    expect(result.code != NULL && result.error_count == 0U,
           "internal procedure terminators remain inside the enclosing module");
    expect(capture.count == 0U,
           "internal procedure END statements never trigger a module-kind diagnostic");
    f2c_result_free(&result);
}

static void test_continued_use_duplicate_span(void) {
    static const char source[] = "subroutine duplicate_use()\n"
                                 "  use provider, only: first, &\n"
                                 "   & second, second\n"
                                 "end subroutine duplicate_use\n";
    DiagnosticCapture capture = {.needle = "duplicate local name in USE association list"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "duplicate continued USE association suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SYNTAX,
           "duplicate USE local name emits one typed syntax diagnostic");
    expect(capture.begin.line == 3U && capture.begin.column == 14U && capture.end.line == 3U &&
               capture.end.column == 20U,
           "continued USE diagnostics retain the duplicate designator's physical span");
    expect(strcmp(capture.source_name, "diagnostic.f90") == 0,
           "continued USE diagnostics retain the canonical source name");
    f2c_result_free(&result);
}

static void test_module_cycle_use_span(void) {
    static const char source[] = "module cycle_a\n"
                                 "  use cycle_b\n"
                                 "end module cycle_a\n"
                                 "module cycle_b\n"
                                 "  use cycle_a\n"
                                 "end module cycle_b\n";
    DiagnosticCapture capture = {.needle = "cyclic project module dependency"};
    F2cResult result = transpile(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "cyclic module dependency suppresses generated C17");
    expect(capture.count == 1U && capture.code == F2C_DIAGNOSTIC_SEMANTIC,
           "module cycle emits one typed semantic diagnostic");
    expect(capture.begin.line == 2U && capture.begin.column == 7U && capture.end.line == 2U &&
               capture.end.column == 14U,
           "module cycle diagnostic uses the exact USE module-name span");
    f2c_result_free(&result);
}

int main(void) {
    test_keyword_association_span();
    test_call_designator_span();
    test_continued_dummy_span();
    test_result_name_span();
    test_trailing_header_token_span();
    test_continued_procedure_attribute_span();
    test_unit_end_name_span();
    test_unit_end_kind_span();
    test_module_end_name_span();
    test_module_header_span();
    test_internal_procedure_end_scope();
    test_continued_use_duplicate_span();
    test_module_cycle_use_span();
    if (failures != 0) {
        fprintf(stderr, "%d diagnostic span test(s) failed\n", failures);
        return 1;
    }
    puts("all diagnostic span tests passed");
    return 0;
}
