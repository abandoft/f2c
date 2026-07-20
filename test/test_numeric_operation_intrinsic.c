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

static void test_typed_lowering(void) {
    static const char source[] =
        "program numeric_operation_valid\n"
        "  implicit none\n"
        "  integer(kind=1) :: i1, j1\n"
        "  integer(kind=2) :: i2, j2\n"
        "  integer(kind=4) :: i4, j4\n"
        "  integer(kind=8) :: i8, j8\n"
        "  real :: r4, s4\n"
        "  double precision :: r8, s8\n"
        "  logical :: mask\n"
        "  i1 = mod(i1, j1)\n"
        "  i2 = modulo(i2, j2)\n"
        "  i4 = dim(i4, j4)\n"
        "  i8 = sign(i8, j8)\n"
        "  i1 = ceiling(r4, kind=1)\n"
        "  i2 = floor(a=r8, kind=2)\n"
        "  i8 = nint(kind=8, a=r8)\n"
        "  r4 = aint(r4) + anint(r4)\n"
        "  r8 = aint(a=r8, kind=8) + anint(kind=8, a=r8)\n"
        "  r4 = dim(r4, s4) + mod(r4, s4) + modulo(r4, s4) + sign(r4, s4)\n"
        "  r8 = dim(r8, s8) + mod(r8, s8) + modulo(r8, s8) + sign(r8, s8)\n"
        "  i4 = merge(mask=mask, fsource=j4, tsource=i4)\n"
        "end program numeric_operation_valid\n";
    F2cOptions options = {"numeric_operation_valid.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "valid scalar numeric-operation calls produce typed C17");
    expect(result.code != NULL && strstr(result.code, "f2c_mod_i8(") != NULL &&
               strstr(result.code, "f2c_modulo_i16(") != NULL &&
               strstr(result.code, "f2c_dim_i32(") != NULL &&
               strstr(result.code, "f2c_sign_i64(") != NULL,
           "integer operations preserve every supported kind");
    expect(result.code != NULL && strstr(result.code, "f2c_ceiling_integer(") != NULL &&
               strstr(result.code, "f2c_floor_integer(") != NULL &&
               strstr(result.code, "f2c_nint_integer(") != NULL,
           "integer-valued rounding uses checked result conversion");
    expect(result.code != NULL && strstr(result.code, "truncf(") != NULL &&
               strstr(result.code, "roundf(") != NULL && strstr(result.code, "trunc(") != NULL &&
               strstr(result.code, "round(") != NULL,
           "AINT and ANINT operate in the input REAL kind");
    expect(result.code != NULL && strstr(result.code, "f2c_dim_r4(") != NULL &&
               strstr(result.code, "f2c_mod_r4(") != NULL &&
               strstr(result.code, "f2c_modulo_r8(") != NULL &&
               strstr(result.code, "f2c_sign_r8(") != NULL,
           "real operations preserve binary32 and binary64 kinds");
    expect(result.code != NULL && strstr(result.code, "F2C_MOD") == NULL &&
               strstr(result.code, "lrint") == NULL,
           "legacy untyped MOD and rounding lowering is absent");
    f2c_result_free(&result);
}

static void test_elemental_and_legacy_lowering(void) {
    static const char source[] =
        "subroutine numeric_operation_elemental(values, divisors, masks, selected)\n"
        "  implicit none\n"
        "  real, intent(inout) :: values(4)\n"
        "  real, intent(in) :: divisors(4)\n"
        "  logical, intent(in) :: masks(4)\n"
        "  real, intent(out) :: selected(4)\n"
        "  double precision :: wide\n"
        "  integer :: index\n"
        "  values = modulo(a=values, p=divisors)\n"
        "  selected = merge(mask=masks, fsource=divisors, tsource=values)\n"
        "  wide = dint(wide) + dnint(wide) + ddim(wide, 1.0d0) + &\n"
        "         dmod(wide, 2.0d0) + dsign(wide, -1.0d0)\n"
        "  index = idnint(wide) + idim(index, 1) + isign(index, -1)\n"
        "  values(1) = amod(values(1), divisors(1))\n"
        "end subroutine numeric_operation_elemental\n";
    F2cOptions options = {"numeric_operation_elemental.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "elemental calls and legacy specific aliases lower through typed IR");
    expect(result.code != NULL && strstr(result.code, "f2c_modulo_r4(") != NULL &&
               strstr(result.code, "masks[") != NULL,
           "elemental arrays are scalarized with canonical keyword association");
    expect(result.code != NULL && strstr(result.code, "f2c_dim_r8(") != NULL &&
               strstr(result.code, "f2c_mod_r8(") != NULL &&
               strstr(result.code, "f2c_sign_i32(") != NULL,
           "legacy aliases reuse kind-specific helpers");
    f2c_result_free(&result);
}

static void test_external_name_precedence(void) {
    static const char caller[] = "subroutine use_external_mod(value)\n"
                                 "  implicit none\n"
                                 "  real :: value, mod\n"
                                 "  external mod\n"
                                 "  value = mod(value, 2.0)\n"
                                 "end subroutine use_external_mod\n";
    static const char definition[] = "real function mod(value, divisor)\n"
                                     "  implicit none\n"
                                     "  real, intent(in) :: value, divisor\n"
                                     "  mod = value / divisor\n"
                                     "end function mod\n";
    F2cInput inputs[2] = {
        {caller, sizeof(caller) - 1U, {"external_mod_caller.f90", F2C_SOURCE_FREE, 0}},
        {definition, sizeof(definition) - 1U, {"external_mod_definition.f90", F2C_SOURCE_FREE, 0}}};
    F2cResult result = f2c_transpile_project(inputs, 2U);
    expect(result.code != NULL && result.error_count == 0U,
           "an explicitly external MOD function overrides the intrinsic");
    expect(result.code != NULL && strstr(result.code, "f2c_mod_r4(") == NULL,
           "external MOD calls do not lower to numeric-operation support");
    f2c_result_free(&result);
}

static void test_support_is_on_demand(void) {
    static const char source[] =
        "program plain\n  implicit none\n  print *, 1\nend program plain\n";
    F2cOptions options = {"plain.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U, "plain program transpiles");
    expect(result.code != NULL && strstr(result.code, "f2c_checked_integer_result") == NULL &&
               strstr(result.code, "F2C_DEFINE_NUMERIC_INTEGER") == NULL,
           "numeric-operation support is omitted when unused");
    f2c_result_free(&result);
}

int main(void) {
    test_rounding_contracts();
    test_binary_contracts();
    test_merge_contracts();
    test_legacy_specific_contracts();
    test_typed_lowering();
    test_elemental_and_legacy_lowering();
    test_external_name_precedence();
    test_support_is_on_demand();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
