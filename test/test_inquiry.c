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
    F2cOptions options = {"inquiry_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "program inquiry_negative\n"
                                "  implicit none\n"
                                "%s"
                                "  print *, %s\n"
                                "end program inquiry_negative\n",
                                declarations, expression);
    expect(length > 0 && (size_t)length < sizeof(source), "negative inquiry fixture fits buffer");
    if (length <= 0 || (size_t)length >= sizeof(source))
        return;
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid inquiry intrinsic suppresses generated code");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_array_argument(void) {
    expect_diagnostic("  integer :: value\n", "size(value)",
                      "size requires a non-scalar array argument");
}

static void test_dimension(void) {
    static const char declarations[] = "  integer :: values(2, 2), dims(1)\n"
                                       "  real :: real_dim\n";
    expect_diagnostic(declarations, "size(values, real_dim)",
                      "DIM in size must be a scalar INTEGER expression");
    expect_diagnostic(declarations, "size(values, dims)",
                      "DIM in size must be a scalar INTEGER expression");
    expect_diagnostic(declarations, "size(values, 3)",
                      "DIM in size must be between 1 and array rank 2");
}

static void test_kind(void) {
    static const char declarations[] = "  integer :: values(2), selected_kind\n";
    expect_diagnostic(declarations, "size(values, kind=16)",
                      "KIND in size must be a supported scalar INTEGER constant");
    expect_diagnostic(declarations, "shape(values, kind=selected_kind)",
                      "KIND in shape must be a supported scalar INTEGER constant");
}

static void test_keyword(void) {
    expect_diagnostic("  integer :: values(2)\n", "ubound(values, bogus=1)",
                      "ubound has no argument named 'bogus'");
}

int main(void) {
    test_array_argument();
    test_dimension();
    test_kind();
    test_keyword();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
