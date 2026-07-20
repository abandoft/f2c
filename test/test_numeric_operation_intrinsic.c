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
    char source[3072];
    F2cOptions options = {"numeric_operation_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "program numeric_operation_negative\n"
                                "  implicit none\n"
                                "%s"
                                "  print *, %s\n"
                                "end program numeric_operation_negative\n",
                                declarations, expression);
    expect(length > 0 && (size_t)length < sizeof(source), "negative fixture is bounded");
    if (length <= 0 || (size_t)length >= sizeof(source))
        return;
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid numeric operation suppresses generated code");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_rounding_contracts(void) {
    expect_diagnostic("", "aint()", "AINT requires exactly 1 argument");
    expect_diagnostic("  integer :: value\n", "aint(value)", "AINT argument A must be REAL");
    expect_diagnostic("  complex :: value\n", "anint(value)", "ANINT argument A must be REAL");
    expect_diagnostic("  integer :: value\n", "ceiling(value)", "CEILING argument A must be REAL");
    expect_diagnostic("  logical :: value\n", "floor(value)", "FLOOR argument A must be REAL");
    expect_diagnostic("  real(kind=16) :: value\n", "nint(value)",
                      "NINT argument A uses unsupported REAL kind 16");
    expect_diagnostic("  real :: value\n", "nint(value, kind=3)",
                      "NINT argument KIND must be a supported scalar INTEGER initialization "
                      "constant (1, 2, 4, or 8)");
    expect_diagnostic("  real :: value\n  integer :: kind_value\n", "aint(value, kind_value)",
                      "AINT argument KIND must be a supported scalar INTEGER initialization "
                      "constant (4 or 8)");
    expect_diagnostic("  real :: value\n", "nint(kind=8, a=value, kind=4)",
                      "NINT argument 'kind' is specified more than once");
}

static void test_binary_contracts(void) {
    expect_diagnostic("  integer :: left\n  real :: right\n", "dim(left, right)",
                      "DIM argument y must have the same type and kind as the first argument");
    expect_diagnostic("  integer(kind=4) :: left\n  integer(kind=8) :: right\n", "mod(left, right)",
                      "MOD argument p must have the same type and kind as the first argument");
    expect_diagnostic("  complex :: left, right\n", "modulo(left, right)",
                      "MODULO arguments must be INTEGER or REAL with a supported kind");
    expect_diagnostic("  real :: left\n", "mod(left, 0.0)", "MOD argument P must not be zero");
    expect_diagnostic("  integer, parameter :: zero = 0\n  integer :: left\n",
                      "modulo(a=left, p=zero)", "MODULO argument P must not be zero");
    expect_diagnostic("  integer :: left\n  real :: right\n", "sign(left, right)",
                      "SIGN argument b must have the same type and kind as the first argument");
}

static void test_merge_contracts(void) {
    expect_diagnostic("  integer :: first\n  real :: second\n  logical :: choose\n",
                      "merge(first, second, choose)",
                      "MERGE argument FSOURCE must have the same type and kind as TSOURCE");
    expect_diagnostic("  integer :: first, second, mask\n", "merge(first, second, mask)",
                      "MERGE argument MASK must be LOGICAL");
    expect_diagnostic("  character(len=2) :: first\n  character(len=3) :: second\n"
                      "  logical :: choose\n",
                      "merge(first, second, choose)",
                      "MERGE argument FSOURCE must have the same CHARACTER length as TSOURCE");
    expect_diagnostic("  integer :: first, second\n  logical :: choose\n",
                      "merge(mask=choose, mystery=second, tsource=first)",
                      "MERGE has no argument named 'mystery'");
}

static void test_legacy_specific_contracts(void) {
    expect_diagnostic("  real :: value\n", "dint(value)",
                      "dint requires a DOUBLE PRECISION(kind=8) first argument");
    expect_diagnostic("  real :: value\n", "idnint(value)",
                      "idnint requires a DOUBLE PRECISION(kind=8) first argument");
    expect_diagnostic("  integer(kind=8) :: left, right\n", "idim(left, right)",
                      "idim requires a INTEGER(kind=4) first argument");
}

int main(void) {
    test_rounding_contracts();
    test_binary_contracts();
    test_merge_contracts();
    test_legacy_specific_contracts();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
