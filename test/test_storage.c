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
                                 "  real :: storage(2), alias(2)\n"
                                 "  equivalence (storage(1), alias(1))\n"
                                 "  equivalence (storage(2), alias(1))\n"
                                 "end program conflicting_equivalence\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "conflicting storage groups suppress generated C");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SEMANTIC && capture.line == 4U,
           "conflicting storage groups have a stable semantic diagnostic");
    f2c_result_free(&result);
}

static void test_equivalence_storage_views(void) {
    static const char valid[] = "program equivalence_views\n"
                                "  integer :: bits\n"
                                "  real :: value, storage(3), window(2)\n"
                                "  equivalence (bits, value)\n"
                                "  equivalence (storage(2), window(1))\n"
                                "end program equivalence_views\n";
    static const char unaligned[] = "program unaligned_equivalence\n"
                                    "  real :: words(2)\n"
                                    "  double precision :: value\n"
                                    "  equivalence (words(2), value)\n"
                                    "end program unaligned_equivalence\n";
    static const char common_association[] = "program common_equivalence\n"
                                             "  integer :: shared(2), local(2)\n"
                                             "  common /state/ shared\n"
                                             "  equivalence (shared(2), local(1))\n"
                                             "end program common_equivalence\n";
    static const char different_blocks[] = "program invalid_common_equivalence\n"
                                           "  integer :: left, right\n"
                                           "  common /left_block/ left\n"
                                           "  common /right_block/ right\n"
                                           "  equivalence (left, right)\n"
                                           "end program invalid_common_equivalence\n";
    static const char backward_extension[] = "program invalid_common_extension\n"
                                             "  integer :: local(2), shared\n"
                                             "  common /state/ shared\n"
                                             "  equivalence (local(2), shared)\n"
                                             "end program invalid_common_extension\n";
    static const char conflicting_anchors[] = "program invalid_common_anchors\n"
                                              "  integer :: left, right\n"
                                              "  common /state/ left, right\n"
                                              "  equivalence (left, right)\n"
                                              "end program invalid_common_anchors\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(valid, &capture);
    expect(result.error_count == 0U && result.code != NULL,
           "heterogeneous and shifted EQUIVALENCE groups reach code generation");
    expect(result.code != NULL && strstr(result.code, "union {") != NULL &&
               strstr(result.code, "f2c_equivalence_0") != NULL &&
               strstr(result.code, "unsigned char prefix[4]") != NULL &&
               strstr(result.code, "EQUIVALENCE value offset") != NULL,
           "EQUIVALENCE emits offset-checked typed storage views");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(unaligned, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "alignment not representable by portable C17") != NULL,
           "unaligned EQUIVALENCE storage fails before unsafe C is emitted");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(common_association, &capture);
    expect(result.error_count == 0U && result.code != NULL,
           "COMMON/EQUIVALENCE composition reaches code generation");
    expect(result.code != NULL && strstr(result.code, "f2c_common_state_equivalence_") != NULL &&
               strstr(result.code, "COMMON EQUIVALENCE value offset") != NULL &&
               strstr(result.code, "COMMON EQUIVALENCE view size") != NULL,
           "COMMON/EQUIVALENCE emits checked global typed views");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(different_blocks, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "cannot associate different COMMON blocks") != NULL,
           "EQUIVALENCE cannot join two COMMON blocks");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(backward_extension, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "before its first storage unit") != NULL,
           "EQUIVALENCE cannot extend before the start of COMMON storage");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(conflicting_anchors, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "constraints disagree with COMMON") != NULL,
           "EQUIVALENCE constraints must preserve existing COMMON member offsets");
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

static void test_zero_sized_common_storage(void) {
    static const char source[] = "subroutine write_zero_storage()\n"
                                 "  integer :: empty(0), value\n"
                                 "  character(len=0) :: text\n"
                                 "  common /state/ empty, text, value\n"
                                 "  value = 42\n"
                                 "end subroutine write_zero_storage\n"
                                 "subroutine clear_zero_storage()\n"
                                 "  integer :: none(1:0), result\n"
                                 "  character(len=0) :: label\n"
                                 "  common /state/ none, label, result\n"
                                 "  result = 0\n"
                                 "end subroutine clear_zero_storage\n";
    static const char invalid_equivalence[] = "program invalid_zero_equivalence\n"
                                              "  integer :: empty(0), value\n"
                                              "  equivalence (empty, value)\n"
                                              "end program invalid_zero_equivalence\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.error_count == 0U && result.code != NULL,
           "zero-sized arrays and zero-length CHARACTER reach COMMON code generation");
    expect(result.code != NULL && strstr(result.code, "f2c_common_state_zero_") != NULL &&
               strstr(result.code, "empty_storage") == NULL &&
               strstr(result.code, "COMMON field offset") != NULL,
           "zero-sized COMMON entities use non-layout placeholder addresses");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(invalid_equivalence, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "requires constant nonempty intrinsic storage") != NULL,
           "zero-sized EQUIVALENCE designators fail before code generation");
    f2c_result_free(&result);
}

static void test_common_storage_mismatch_diagnostic(void) {
    static const char source[] = "subroutine integer_view()\n"
                                 "  integer :: value\n"
                                 "  common /shared/ value\n"
                                 "end subroutine integer_view\n"
                                 "subroutine double_view()\n"
                                 "  double precision :: value\n"
                                 "  common /shared/ value\n"
                                 "end subroutine double_view\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.code == NULL && result.error_count != 0U,
           "different named COMMON storage extents suppress generated C");
    expect(capture.captured && capture.code == F2C_DIAGNOSTIC_SEMANTIC && capture.line == 7U &&
               capture.column == 19U,
           "COMMON layout mismatch reports the incompatible member span");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, "storage bytes") != NULL,
           "COMMON layout mismatch reports both storage extents");
    f2c_result_free(&result);
}

static void test_common_storage_views(void) {
    static const char source[] = "subroutine array_view()\n"
                                 "  real :: values(2)\n"
                                 "  common /shared/ values\n"
                                 "end subroutine array_view\n"
                                 "subroutine scalar_view()\n"
                                 "  real :: first, second\n"
                                 "  common /shared/ first, second\n"
                                 "end subroutine scalar_view\n"
                                 "subroutine integer_view()\n"
                                 "  integer :: words(2)\n"
                                 "  common /shared/ words\n"
                                 "end subroutine integer_view\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.error_count == 0U && result.code != NULL,
           "COMMON accepts equal-size array, scalar, and heterogeneous storage views");
    expect(result.code != NULL && strstr(result.code, "union f2c_common_shared_storage") != NULL &&
               strstr(result.code, "struct f2c_common_shared_view_0") != NULL &&
               strstr(result.code, "struct f2c_common_shared_view_1") != NULL &&
               strstr(result.code, "struct f2c_common_shared_view_2") != NULL,
           "COMMON emits one checked view per declaring program unit");
    expect(result.code != NULL && strstr(result.code, "_Static_assert(offsetof(") != NULL &&
               strstr(result.code, "_Static_assert(sizeof(") != NULL,
           "COMMON views carry compile-time ABI layout checks");
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
               strstr(result.code,
                      "F2C_COMMON_INITIALIZED_STORAGE union f2c_common_state_storage") != NULL &&
               strstr(result.code, ".field_0 = INT64_C(7)") != NULL &&
               strstr(result.code, ".field_1 = {INT64_C(11), INT64_C(13), INT64_C(17)}") != NULL &&
               strstr(result.code, ".field_2 = F2C_COMPLEX_FLOAT_INITIALIZER") != NULL &&
               strstr(result.code, ".field_3 = INT64_C(19)") != NULL &&
               strstr(result.code, ".field_4 = {'o', 'k', ' ', ' '}") != NULL,
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

static void test_module_data_initialization(void) {
    static const char source[] = "module configured_state\n"
                                 "  implicit none\n"
                                 "  integer :: values(3), selected\n"
                                 "  complex :: point\n"
                                 "  character(len=3) :: names(2)\n"
                                 "  data values / 2, 3, 5 /, selected / 7 /\n"
                                 "  data point / (1.5, -2.0) /, names / 'a', 'xyz' /\n"
                                 "end module configured_state\n"
                                 "program inspect_module_data\n"
                                 "  use configured_state\n"
                                 "  implicit none\n"
                                 "  print *, values, selected, point, names\n"
                                 "end program inspect_module_data\n";
    static const char invalid_executable[] = "module invalid_module\n"
                                             "  implicit none\n"
                                             "  integer :: value\n"
                                             "  value = 1\n"
                                             "end module invalid_module\n";
    static const char invalid_use_target[] = "module data_provider\n"
                                             "  integer :: value\n"
                                             "end module data_provider\n"
                                             "module invalid_data_consumer\n"
                                             "  use data_provider\n"
                                             "  data value / 1 /\n"
                                             "end module invalid_data_consumer\n";
    DiagnosticCapture capture = {0};
    F2cResult result = transpile_with_diagnostics(source, &capture);
    expect(result.error_count == 0U && result.code != NULL,
           "module DATA statements reach typed static-storage emission");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_module_configured_state_values[") != NULL &&
               strstr(result.code, "= {INT64_C(2), INT64_C(3), INT64_C(5)}") != NULL &&
               strstr(result.code, "f2c_module_configured_state_selected = INT64_C(7)") != NULL &&
               strstr(result.code, "F2C_COMPLEX_FLOAT_INITIALIZER") != NULL &&
               strstr(result.code, "f2c_module_configured_state_names[(size_t)(3) *") != NULL,
           "module DATA preserves numeric, complex, and CHARACTER array storage");
    expect(result.code != NULL && strstr(result.code, "f2c_data_initialized_") == NULL,
           "module DATA never depends on a runtime first-use guard");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(invalid_executable, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics,
                      "MODULE specification part cannot contain executable statements") != NULL,
           "module specification parts reject executable statements before code generation");
    f2c_result_free(&result);

    memset(&capture, 0, sizeof(capture));
    result = transpile_with_diagnostics(invalid_use_target, &capture);
    expect(result.error_count != 0U && result.code == NULL && result.diagnostics != NULL &&
               strstr(result.diagnostics, "DATA target cannot be a use-associated entity") != NULL,
           "module DATA cannot initialize storage owned by another module");
    f2c_result_free(&result);
}

int main(void) {
    test_equivalence_rank_diagnostic();
    test_conflicting_equivalence_groups();
    test_equivalence_storage_views();
    test_blank_common_and_mixed_blocks();
    test_zero_sized_common_storage();
    test_common_storage_mismatch_diagnostic();
    test_common_storage_views();
    test_block_data_common_initialization();
    test_block_data_constraints();
    test_module_data_initialization();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
