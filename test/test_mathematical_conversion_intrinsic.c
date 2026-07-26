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
    char source[4096];
    F2cOptions options = {"mathematical_conversion_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "program mathematical_conversion_negative\n"
                                "  implicit none\n"
                                "%s"
                                "  print *, %s\n"
                                "end program mathematical_conversion_negative\n",
                                declarations, expression);
    expect(length > 0 && (size_t)length < sizeof(source), "negative fixture is bounded");
    if (length <= 0 || (size_t)length >= sizeof(source))
        return;
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid mathematical or conversion call suppresses generated code");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_mathematical_contracts(void) {
    expect_diagnostic("  logical :: value\n", "abs(value)",
                      "ABS argument A must be INTEGER, REAL, or COMPLEX");
    expect_diagnostic("  integer :: value\n", "acos(value)",
                      "ACOS argument X must be REAL or COMPLEX");
    expect_diagnostic("  complex :: value\n", "log10(value)", "LOG10 argument X must be REAL");
    expect_diagnostic("  real :: y\n  double precision :: x\n", "atan2(y, x)",
                      "ATAN2 arguments Y and X must have the same type and kind");
    expect_diagnostic("  integer :: first\n  real :: second\n", "max(first, second)",
                      "MAX argument A2 must have the same type and kind as A1");
    expect_diagnostic("  integer(kind=4) :: first\n  integer(kind=8) :: second\n",
                      "min(first, second)",
                      "MIN argument A2 must have the same type and kind as A1");
    expect_diagnostic("  double precision :: first\n  real :: second\n", "dprod(first, second)",
                      "DPROD argument x must be REAL(kind=4)");
    expect_diagnostic("  complex :: value\n", "alog(value)",
                      "alog requires a REAL(kind=4) first argument");
    expect_diagnostic("  integer :: first, second\n", "max(a1=first, mystery=second)",
                      "MAX has no argument named 'mystery'");
    expect_diagnostic("", "sqrt(-1.0)", "SQRT argument X must be greater than or equal to zero");
    expect_diagnostic("", "log(0.0)", "LOG argument X must be greater than zero");
    expect_diagnostic("", "asin(2.0)", "ASIN argument X must be magnitude no greater than one");
}

static void test_conversion_contracts(void) {
    expect_diagnostic("  logical :: value\n", "int(value)",
                      "INT argument A must be INTEGER, REAL, or COMPLEX");
    expect_diagnostic("  character :: value\n", "real(value)",
                      "REAL argument A must be INTEGER, REAL, or COMPLEX");
    expect_diagnostic("  real :: value\n", "aimag(value)", "AIMAG argument Z must be COMPLEX");
    expect_diagnostic("  real :: value\n", "conjg(value)", "CONJG argument Z must be COMPLEX");
    expect_diagnostic("  complex :: value\n", "cmplx(value, 2.0)",
                      "CMPLX argument Y must be absent when X is COMPLEX");
    expect_diagnostic("  real :: value\n", "cmplx(value, kind=2)",
                      "CMPLX argument KIND must be a supported scalar INTEGER initialization "
                      "constant (4 or 8)");
    expect_diagnostic("  real :: value\n  integer :: kind_value\n", "int(value, kind_value)",
                      "INT argument KIND must be a supported scalar INTEGER initialization "
                      "constant (1, 2, 4, or 8)");
    expect_diagnostic("  real :: value\n", "idint(value)",
                      "idint requires a DOUBLE PRECISION(kind=8) argument");
    expect_diagnostic("  integer :: value\n", "sngl(value)",
                      "sngl requires a DOUBLE PRECISION(kind=8) argument");
}

static void test_typed_lowering(void) {
    static const char source[] =
        "subroutine mathematical_conversion_valid(i1, i2, i4, i8, r4, r8, c4, c8)\n"
        "  implicit none\n"
        "  integer(kind=1), intent(inout) :: i1\n"
        "  integer(kind=2), intent(inout) :: i2\n"
        "  integer(kind=4), intent(inout) :: i4\n"
        "  integer(kind=8), intent(inout) :: i8\n"
        "  real, intent(inout) :: r4\n"
        "  double precision, intent(inout) :: r8\n"
        "  complex, intent(inout) :: c4\n"
        "  double complex, intent(inout) :: c8\n"
        "  integer, parameter :: folded_integer = int(3.75) + max(2, 4)\n"
        "  real, parameter :: folded_real = sqrt(4.0) + real(2)\n"
        "  i1 = int(r4, kind=1)\n"
        "  i2 = int(a=r8, kind=2)\n"
        "  i4 = int(c4)\n"
        "  i8 = int(kind=8, a=c8)\n"
        "  r4 = real(i8) + sngl(r8)\n"
        "  r8 = real(a=c4, kind=8) + dble(c8)\n"
        "  c4 = cmplx(kind=4, y=r4, x=i4)\n"
        "  c8 = dcmplx(r8, i8)\n"
        "  r4 = abs(c4) + aimag(c4) + acos(r4) + asin(r4) + atan(r4)\n"
        "  r4 = r4 + cos(r4) + cosh(r4) + exp(r4) + log(r4) + log10(r4)\n"
        "  r4 = r4 + sin(r4) + sinh(r4) + sqrt(r4) + tan(r4) + tanh(r4)\n"
        "  r4 = r4 + alog(r4) + alog10(r4)\n"
        "  r8 = atan2(y=r8, x=1.0d0) + max(r8, 1.0d0) + min(r8, 2.0d0) + dprod(r4, r4)\n"
        "  c4 = conjg(c4) + acos(c4) + asin(c4) + atan(c4) + cos(c4) + exp(c4)\n"
        "  c4 = c4 + log(c4) + sin(c4) + sqrt(c4) + tan(c4) + tanh(c4)\n"
        "  c4 = c4 + ccos(c4) + cexp(c4) + clog(c4) + csin(c4) + csqrt(c4)\n"
        "  i1 = max(i1, int(folded_real, kind=1))\n"
        "  i8 = min(i8, int(folded_integer, kind=8))\n"
        "end subroutine mathematical_conversion_valid\n";
    F2cOptions options = {"mathematical_conversion_valid.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "valid mathematical and conversion calls produce typed C17");
    expect(result.code != NULL && strstr(result.code, "f2c_int_integer(") != NULL &&
               strstr(result.code, "f2c_checked_integer_from_i64(") != NULL,
           "INT uses checked kind-specific conversion instead of an undefined C cast");
    expect(result.code != NULL && strstr(result.code, "f2c_make_c(") != NULL &&
               strstr(result.code, "f2c_make_z(") != NULL &&
               strstr(result.code, "crealf(") != NULL && strstr(result.code, "creal(") != NULL,
           "REAL and CMPLX preserve complex component kinds");
    expect(result.code != NULL && strstr(result.code, "acosf(") != NULL &&
               strstr(result.code, "coshf(") != NULL && strstr(result.code, "tanhf(") != NULL &&
               strstr(result.code, "cacosf(") != NULL && strstr(result.code, "casinf(") != NULL &&
               strstr(result.code, "catanf(") != NULL && strstr(result.code, "ctanf(") != NULL &&
               strstr(result.code, "ctanhf(") != NULL && strstr(result.code, "csqrtf(") != NULL,
           "mathematical calls select portable C17 real and complex libm entry points");
    expect(result.code != NULL && strstr(result.code, "f2c_fortran_i8max") != NULL &&
               strstr(result.code, "f2c_fortran_i64min") != NULL,
           "MAX and MIN retain narrow and wide INTEGER kinds");
    f2c_result_free(&result);
}

static void test_elemental_lowering(void) {
    static const char source[] = "subroutine mathematical_elemental(values, wide, converted)\n"
                                 "  implicit none\n"
                                 "  real, intent(inout) :: values(4)\n"
                                 "  double precision, intent(in) :: wide(4)\n"
                                 "  integer(kind=8), intent(out) :: converted(4)\n"
                                 "  values = sin(values) + sqrt(abs(values))\n"
                                 "  converted = int(wide, kind=8)\n"
                                 "end subroutine mathematical_elemental\n";
    F2cOptions options = {"mathematical_elemental.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "mathematical and conversion intrinsics remain elemental in typed IR");
    expect(result.code != NULL && strstr(result.code, "sinf(values[") != NULL &&
               strstr(result.code, "f2c_int_integer((double)(wide[") != NULL,
           "array intrinsic calls scalarize with kind-preserving lowering");
    f2c_result_free(&result);
}

static void test_external_name_precedence(void) {
    static const char caller[] = "subroutine use_external_sin(value)\n"
                                 "  implicit none\n"
                                 "  real :: value, sin\n"
                                 "  external sin\n"
                                 "  value = sin(value)\n"
                                 "end subroutine use_external_sin\n";
    static const char definition[] = "real function sin(value)\n"
                                     "  implicit none\n"
                                     "  real, intent(in) :: value\n"
                                     "  sin = value\n"
                                     "end function sin\n";
    F2cInput inputs[2] = {
        {caller, sizeof(caller) - 1U, {"external_sin_caller.f90", F2C_SOURCE_FREE, 0}},
        {definition, sizeof(definition) - 1U, {"external_sin_definition.f90", F2C_SOURCE_FREE, 0}}};
    F2cResult result = f2c_transpile_project(inputs, 2U);
    expect(result.code != NULL && result.error_count == 0U,
           "an explicitly external SIN function overrides the intrinsic");
    expect(result.code != NULL && strstr(result.code, "sinf(") == NULL,
           "external SIN calls do not lower to libm");
    f2c_result_free(&result);
}

static void test_iso_environment_kind_alias(void) {
    static const char source[] =
        "subroutine iso_environment_kind_alias(value, wide, result, converted)\n"
        "  use, intrinsic :: iso_fortran_env, only: real32, real64\n"
        "  implicit none\n"
        "  integer, parameter :: working_kind = real32\n"
        "  integer, parameter :: wide_kind = real64\n"
        "  real(kind=working_kind), intent(in) :: value\n"
        "  real(kind=wide_kind), intent(in) :: wide\n"
        "  complex(kind=working_kind), intent(out) :: result\n"
        "  integer(kind=wide_kind), intent(out) :: converted\n"
        "  result = cmplx(value, -value, kind=working_kind)\n"
        "  converted = int(wide, kind=wide_kind)\n"
        "end subroutine iso_environment_kind_alias\n";
    F2cOptions options = {"iso_environment_kind_alias.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "ISO_FORTRAN_ENV kind constants remain initialization constants through aliases");
    expect(result.code != NULL && strstr(result.code, "f2c_make_c(") != NULL &&
               strstr(result.code, "f2c_int_integer((double)") != NULL,
           "aliased ISO kinds select kind-preserving CMPLX and INT lowering");
    f2c_result_free(&result);
}

int main(void) {
    test_mathematical_contracts();
    test_conversion_contracts();
    test_typed_lowering();
    test_elemental_lowering();
    test_external_name_precedence();
    test_iso_environment_kind_alias();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
