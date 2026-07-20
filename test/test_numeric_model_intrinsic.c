#include "f2c/f2c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void expect_diagnostic(const char *declarations, const char *expression,
                              const char *message) {
    char source[2048];
    F2cOptions options = {"numeric_model_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "program numeric_model_negative\n"
                                "  implicit none\n"
                                "%s"
                                "  print *, %s\n"
                                "end program numeric_model_negative\n",
                                declarations, expression);
    expect(length > 0 && (size_t)length < sizeof(source), "negative fixture is bounded");
    if (length <= 0 || (size_t)length >= sizeof(source))
        return;
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid numeric model intrinsic suppresses generated code");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_model_argument_types(void) {
    expect_diagnostic("  complex :: value\n", "digits(value)",
                      "DIGITS argument X has an unsupported type");
    expect_diagnostic("  integer :: value\n", "epsilon(value)",
                      "EPSILON argument X has an unsupported type");
    expect_diagnostic("  logical :: value\n", "huge(value)",
                      "HUGE argument X has an unsupported type");
    expect_diagnostic("  integer :: value\n", "precision(value)",
                      "PRECISION argument X has an unsupported type");
    expect_diagnostic("  complex :: value\n", "radix(value)",
                      "RADIX argument X has an unsupported type");
    expect_diagnostic("  complex :: value\n", "maxexponent(value)",
                      "MAXEXPONENT argument X has an unsupported type");
    expect_diagnostic("  type :: item\n    integer :: value\n  end type\n  type(item) :: value\n",
                      "kind(value)", "KIND argument X has an unsupported type");
}

static void test_model_kind_support(void) {
    expect_diagnostic("  real(kind=16) :: value\n", "digits(value)",
                      "DIGITS argument X uses unsupported kind 16");
}

static void test_kind_selection_arguments(void) {
    expect_diagnostic("  real :: value\n", "selected_int_kind(value)",
                      "SELECTED_INT_KIND argument R must be a scalar INTEGER");
    expect_diagnostic("  integer :: values(2)\n", "selected_int_kind(values)",
                      "SELECTED_INT_KIND argument R must be a scalar INTEGER");
    expect_diagnostic("", "selected_real_kind(radix=2)", "SELECTED_REAL_KIND requires P or R");
    expect_diagnostic("  real :: value\n", "selected_real_kind(p=value)",
                      "SELECTED_REAL_KIND argument P must be a scalar INTEGER");
    expect_diagnostic("  integer :: values(2)\n", "selected_real_kind(r=values)",
                      "SELECTED_REAL_KIND argument R must be a scalar INTEGER");
    expect_diagnostic("", "selected_real_kind(p=6, mystery=2)",
                      "SELECTED_REAL_KIND has no argument named 'mystery'");
    expect_diagnostic("", "selected_real_kind(p=6, p=7)",
                      "SELECTED_REAL_KIND argument 'p' is specified more than once");
}

static void test_valid_contracts(void) {
    static const char source[] =
        "program numeric_model_valid\n"
        "  implicit none\n"
        "  integer(kind=1) :: i1\n"
        "  real :: r4(2)\n"
        "  double precision :: r8\n"
        "  complex :: c4\n"
        "  print *, digits(i1), huge(i1), radix(i1), range(i1), kind(i1)\n"
        "  print *, digits(r4), epsilon(r4), huge(r4), tiny(r4)\n"
        "  print *, minexponent(r8), maxexponent(r8), precision(c4), range(c4)\n"
        "  print *, selected_int_kind(18), selected_real_kind(6), &\n"
        "    selected_real_kind(r=307), selected_real_kind(p=15, radix=2)\n"
        "end program numeric_model_valid\n";
    F2cOptions options = {"numeric_model_valid.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "valid numeric model intrinsic contracts produce typed C17 IR");
    expect(result.code != NULL &&
               strstr(result.code, "F2C_WRITE(f2c_unit_stream(6, false), (INT8_MAX))") != NULL,
           "INTEGER(1) HUGE lowers to its exact-width C17 model constant");
    expect(result.code != NULL &&
               strstr(result.code, "F2C_WRITE(f2c_unit_stream(6, false), (FLT_EPSILON))") != NULL,
           "REAL(4) EPSILON lowers to the binary32 C17 model constant");
    expect(result.code != NULL && strstr(result.code, "_Generic((i1)") == NULL &&
               strstr(result.code, "_Generic((r4)") == NULL,
           "numeric inquiries do not use value-evaluating generic selections");
    f2c_result_free(&result);
}

static void test_inquiry_arguments_are_not_evaluated(void) {
    static const char source[] = "program numeric_model_inquiry_evaluation\n"
                                 "  implicit none\n"
                                 "  integer, external :: touch_integer\n"
                                 "  print *, digits(touch_integer()), huge(touch_integer())\n"
                                 "end program numeric_model_inquiry_evaluation\n";
    F2cOptions options = {"numeric_model_inquiry_evaluation.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "numeric inquiry calls with procedure expressions transpile");
    expect(result.code != NULL &&
               strstr(result.code, "extern int32_t touch_integer(void);") != NULL,
           "the inquiry fixture retains the external procedure declaration");
    expect(result.code != NULL && strstr(result.code, "touch_integer()") == NULL,
           "numeric inquiry codegen does not evaluate its model expression");
    expect(result.code != NULL &&
               strstr(result.code, "F2C_WRITE(f2c_unit_stream(6, false), (INT32_MAX))") != NULL,
           "INTEGER(4) HUGE remains an integer model constant");
    f2c_result_free(&result);
}

static void test_dynamic_kind_selection(void) {
    static const char source[] = "program dynamic_kind_selection\n"
                                 "  implicit none\n"
                                 "  integer :: p, r, base\n"
                                 "  p = 6\n"
                                 "  r = 37\n"
                                 "  base = 2\n"
                                 "  print *, selected_int_kind(r)\n"
                                 "  print *, selected_real_kind(radix=base, r=r, p=p)\n"
                                 "end program dynamic_kind_selection\n";
    F2cOptions options = {"dynamic_kind_selection.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "runtime selected-kind arguments produce portable C17");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_selected_int_kind((int64_t)(r))") != NULL,
           "dynamic SELECTED_INT_KIND uses its fixed-model helper");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_selected_real_kind((int64_t)(p), true, (int64_t)(r), true, "
                                   "(int64_t)(base), true)") != NULL,
           "dynamic SELECTED_REAL_KIND binds shuffled keywords exactly once");
    f2c_result_free(&result);
}

int main(void) {
    test_model_argument_types();
    test_model_kind_support();
    test_kind_selection_arguments();
    test_valid_contracts();
    test_inquiry_arguments_are_not_evaluated();
    test_dynamic_kind_selection();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
