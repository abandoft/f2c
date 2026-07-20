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
    F2cOptions options = {"real_representation_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "program real_representation_negative\n"
                                "  implicit none\n"
                                "%s"
                                "  print *, %s\n"
                                "end program real_representation_negative\n",
                                declarations, expression);
    expect(length > 0 && (size_t)length < sizeof(source), "negative fixture is bounded");
    if (length <= 0 || (size_t)length >= sizeof(source))
        return;
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid real representation intrinsic suppresses generated code");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_argument_contracts(void) {
    expect_diagnostic("  integer :: value\n", "exponent(value)",
                      "EXPONENT argument X must be REAL");
    expect_diagnostic("  complex :: value\n", "fraction(value)",
                      "FRACTION argument X must be REAL");
    expect_diagnostic("  real :: value\n  integer :: direction\n", "nearest(value, direction)",
                      "NEAREST argument S must be REAL");
    expect_diagnostic("  logical :: value\n", "rrspacing(value)",
                      "RRSPACING argument X must be REAL");
    expect_diagnostic("  real :: value, power\n", "scale(value, power)",
                      "SCALE argument I must be INTEGER");
    expect_diagnostic("  integer :: value, power\n", "set_exponent(value, power)",
                      "SET_EXPONENT argument X must be REAL");
    expect_diagnostic("  integer :: value\n", "spacing(value)", "SPACING argument X must be REAL");
    expect_diagnostic("  real(kind=16) :: value\n", "fraction(value)",
                      "FRACTION argument X uses unsupported REAL kind 16");
    expect_diagnostic("  real :: value\n  integer(kind=16) :: power\n", "scale(value, power)",
                      "SCALE argument I uses unsupported INTEGER kind 16");
}

static void test_keyword_and_direction_contracts(void) {
    expect_diagnostic("  real :: value\n", "nearest(x=value, s=0.0)",
                      "NEAREST argument S must not be zero");
    expect_diagnostic("  real :: value\n", "nearest(x=value, s=-0.0)",
                      "NEAREST argument S must not be zero");
    expect_diagnostic("  real, parameter :: zero = 0.0\n  real :: value\n",
                      "nearest(x=value, s=zero)", "NEAREST argument S must not be zero");
    expect_diagnostic("  real :: value\n", "fraction(mystery=value)",
                      "FRACTION has no argument named 'mystery'");
    expect_diagnostic("  real :: value\n", "spacing(x=value, x=value)",
                      "SPACING argument 'x' is specified more than once");
}

static void test_typed_lowering(void) {
    static const char source[] = "program real_representation_valid\n"
                                 "  implicit none\n"
                                 "  real :: r4(2), direction(2)\n"
                                 "  double precision :: r8\n"
                                 "  integer(kind=8) :: power(2)\n"
                                 "  integer :: exponents(2)\n"
                                 "  exponents = exponent(x=r4)\n"
                                 "  r4 = fraction(x=r4)\n"
                                 "  r4 = nearest(s=direction, x=r4)\n"
                                 "  r4 = rrspacing(x=r4)\n"
                                 "  r4 = scale(i=power, x=r4)\n"
                                 "  r4 = set_exponent(i=power, x=r4)\n"
                                 "  r4 = spacing(x=r4)\n"
                                 "  r8 = fraction(r8) + nearest(r8, r4(1)) + spacing(r8)\n"
                                 "end program real_representation_valid\n";
    F2cOptions options = {"real_representation_valid.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "valid scalar and elemental representation calls produce typed C17");
    expect(result.code != NULL && strstr(result.code, "f2c_exponent_r4(") != NULL,
           "EXPONENT uses the binary32 typed helper");
    expect(result.code != NULL && strstr(result.code, "f2c_fraction_r4(") != NULL &&
               strstr(result.code, "f2c_fraction_r8(") != NULL,
           "FRACTION preserves the kind of X");
    expect(result.code != NULL && strstr(result.code, "f2c_nearest_r4(") != NULL &&
               strstr(result.code, "f2c_nearest_r8(") != NULL,
           "NEAREST preserves X kind while accepting a different S kind");
    expect(result.code != NULL && strstr(result.code, "f2c_scale_r4(") != NULL &&
               strstr(result.code, "(int64_t)(power[") != NULL,
           "SCALE preserves the full supported INTEGER exponent range");
    expect(result.code != NULL && strstr(result.code, "f2c_set_exponent_r4(") != NULL &&
               strstr(result.code, "f2c_spacing_r8(") != NULL,
           "SET_EXPONENT and SPACING use typed representation lowering");
    expect(result.code != NULL && strstr(result.code, "ilogb") == NULL,
           "legacy value-unsafe EXPONENT lowering is absent");
    expect(result.code != NULL && strstr(result.code, "f2c_selected_int_kind") == NULL,
           "representation-only output does not emit unrelated kind-selection helpers");
    f2c_result_free(&result);
}

static void test_support_is_on_demand(void) {
    static const char source[] =
        "program plain\n  implicit none\n  print *, 1\nend program plain\n";
    F2cOptions options = {"plain.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U, "plain program transpiles");
    expect(result.code != NULL && strstr(result.code, "f2c_exponent_r4") == NULL &&
               strstr(result.code, "f2c_spacing_r8") == NULL,
           "real representation support is omitted when unused");
    f2c_result_free(&result);
}

static void test_constant_lowering(void) {
    static const char source[] = "module representation_constants\n"
                                 "  implicit none\n"
                                 "  integer, parameter :: e0 = exponent(0.0)\n"
                                 "  integer, parameter :: es = exponent(nearest(0.0, 1.0))\n"
                                 "  real, parameter :: f = fraction(6.0)\n"
                                 "  real, parameter :: n = nearest(1.0, 1.0)\n"
                                 "  real, parameter :: r = rrspacing(1.5)\n"
                                 "  real, parameter :: s = scale(0.75, 2)\n"
                                 "  real, parameter :: se = set_exponent(6.0, 2)\n"
                                 "  real, parameter :: sp = spacing(1.0)\n"
                                 "end module representation_constants\n";
    F2cOptions options = {"representation_constants.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "representation intrinsics are valid module initialization expressions");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_module_representation_constants_e0 = INT32_C(0)") != NULL &&
               strstr(result.code, "f2c_module_representation_constants_es = -INT32_C(148)") !=
                   NULL,
           "EXPONENT constants include zero and subnormal boundaries");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_module_representation_constants_f = 0x1.8p-1f") != NULL &&
               strstr(result.code, "f2c_module_representation_constants_n = 0x1.000002p+0f") !=
                   NULL,
           "FRACTION and NEAREST constants use exact hexadecimal C17 literals");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_module_representation_constants_r = 0x1.8p+23f") != NULL &&
               strstr(result.code, "f2c_module_representation_constants_s = 0x1.8p+1f") != NULL &&
               strstr(result.code, "f2c_module_representation_constants_se = 0x1.8p+1f") != NULL &&
               strstr(result.code, "f2c_module_representation_constants_sp = 0x1p-23f") != NULL,
           "remaining representation constants are valid static C17 initializers");
    f2c_result_free(&result);
}

int main(void) {
    test_argument_contracts();
    test_keyword_and_direction_contracts();
    test_typed_lowering();
    test_support_is_on_demand();
    test_constant_lowering();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
