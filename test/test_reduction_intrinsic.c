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
    F2cOptions options = {"reduction_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "program reduction_negative\n"
                                "  implicit none\n"
                                "%s"
                                "  print *, %s\n"
                                "end program reduction_negative\n",
                                declarations, expression);
    expect(length > 0 && (size_t)length < sizeof(source), "negative fixture is bounded");
    if (length <= 0 || (size_t)length >= sizeof(source))
        return;
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid reduction call suppresses generated code");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_argument_contracts(void) {
    expect_diagnostic("  integer :: scalar\n", "sum(scalar)",
                      "SUM argument ARRAY must be an array");
    expect_diagnostic("  real :: values(4)\n", "all(values)",
                      "ALL argument MASK must be a LOGICAL array");
    expect_diagnostic("  logical :: values(4)\n", "product(values)",
                      "PRODUCT argument ARRAY must be an array of a supported INTEGER, REAL, or "
                      "COMPLEX kind");
    expect_diagnostic("  complex :: values(4)\n", "maxval(values)",
                      "MAXVAL argument ARRAY must be an INTEGER or REAL array");
    expect_diagnostic("  real :: values(4)\n  real :: dimension\n", "sum(values, dimension)",
                      "SUM argument DIM must be a scalar INTEGER expression");
    expect_diagnostic("  real :: values(2,2)\n", "sum(values, dim=3)",
                      "DIM in SUM must be between 1 and array rank 2");
    expect_diagnostic("  real :: values(4)\n  integer :: mask(4)\n",
                      "sum(values, mask=mask)",
                      "SUM argument MASK must be a LOGICAL scalar or an array conformable with "
                      "ARRAY");
    expect_diagnostic("  real :: values(4)\n  logical :: mask(3)\n",
                      "sum(values, mask=mask)",
                      "MASK in SUM is not conformable with ARRAY in dimension 1");
    expect_diagnostic("  logical :: values(4)\n  integer :: selected_kind\n",
                      "count(values, kind=selected_kind)",
                      "COUNT argument KIND must be a supported scalar INTEGER constant (1, 2, 4, "
                      "or 8)");
    expect_diagnostic("  real :: values(4)\n", "maxloc(values, kind=3)",
                      "MAXLOC argument KIND must be a supported scalar INTEGER constant (1, 2, 4, "
                      "or 8)");
    expect_diagnostic("  real :: values(4)\n", "maxloc(values, back=1)",
                      "MAXLOC argument BACK must be a scalar LOGICAL expression");
    expect_diagnostic("  real :: values(4)\n", "sum(values, mystery=.true.)",
                      "SUM has no argument named 'mystery'");
    expect_diagnostic("  real :: values(4)\n", "sum(array=values, values)",
                      "positional argument in SUM cannot follow a keyword argument");
    expect_diagnostic("  real :: values(4)\n", "sum(array=values, array=values)",
                      "SUM argument 'array' is specified more than once");
}

static void test_dot_product_contracts(void) {
    expect_diagnostic("  real :: matrix(2,2), vector(2)\n", "dot_product(matrix, vector)",
                      "DOT_PRODUCT argument VECTOR_A must be a rank-one array");
    expect_diagnostic("  real :: left(2), right(2,2)\n", "dot_product(left, right)",
                      "DOT_PRODUCT argument VECTOR_B must be a rank-one array");
    expect_diagnostic("  logical :: left(2)\n  real :: right(2)\n", "dot_product(left, right)",
                      "DOT_PRODUCT vectors must both be numeric or both be LOGICAL");
    expect_diagnostic("  real :: left(2), right(3)\n", "dot_product(left, right)",
                      "DOT_PRODUCT vectors are not conformable");
    expect_diagnostic("  real :: left(2), right(2)\n",
                      "dot_product(vector_a=left, vector_a=right)",
                      "DOT_PRODUCT argument 'vector_a' is specified more than once");
}

static void test_typed_scalar_lowering(void) {
    static const char source[] =
        "subroutine reduction_valid(integers, reals, logicals, left, right, outputs, dot_kind)\n"
        "  implicit none\n"
        "  integer, intent(in) :: integers(4)\n"
        "  real, intent(in) :: reals(4)\n"
        "  logical, intent(in) :: logicals(4)\n"
        "  real, intent(in) :: left(4), right(4)\n"
        "  real, intent(out) :: outputs(6)\n"
        "  integer, intent(out) :: dot_kind\n"
        "  integer(kind=8) :: wide_integers(4)\n"
        "  dot_kind = kind(dot_product(wide_integers, reals))\n"
        "  outputs(1) = sum(integers)\n"
        "  outputs(2) = product(array=reals)\n"
        "  outputs(3) = maxval(reals)\n"
        "  outputs(4) = minval(array=reals)\n"
        "  outputs(5) = count(mask=logicals, kind=4)\n"
        "  outputs(6) = dot_product(vector_b=right, vector_a=left)\n"
        "  if (all(mask=logicals) .and. any(logicals)) outputs(1) = maxloc(reals, dim=1)\n"
        "  outputs(2) = minloc(array=reals, dim=1, kind=8)\n"
        "end subroutine reduction_valid\n";
    F2cOptions options = {"reduction_valid.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "valid reductions produce typed C17");
    expect(result.code != NULL && strstr(result.code, "F2C_SUM(") != NULL &&
               strstr(result.code, "F2C_PRODUCT(") != NULL &&
               strstr(result.code, "F2C_MAXIMUM(") != NULL &&
               strstr(result.code, "F2C_MINIMUM(") != NULL,
           "numeric reductions select the typed helper family");
    expect(result.code != NULL && strstr(result.code, "f2c_count_l(") != NULL &&
               strstr(result.code, "f2c_all_l(") != NULL &&
               strstr(result.code, "f2c_any_l(") != NULL,
           "logical reductions select the typed helper family");
    expect(result.code != NULL && strstr(result.code, "f2c_dot_f(") != NULL &&
               strstr(result.code, "F2C_MAXIMUM_LOCATION(") != NULL &&
               strstr(result.code, "F2C_MINIMUM_LOCATION(") != NULL,
           "dot and location reductions lower from their typed intrinsic IDs");
    expect(result.code != NULL && strstr(result.code, "(*dot_kind) = INT32_C(4)") != NULL,
           "DOT_PRODUCT derives result kind from the dominant non-integer category");
    f2c_result_free(&result);
}

static void test_mixed_and_complex_lowering(void) {
    static const char source[] =
        "subroutine mixed_reductions(c4, c8, i8, r4, l1, l8, c_result, z_result, r_result, "
        "l_result)\n"
        "  implicit none\n"
        "  complex, intent(in) :: c4(3)\n"
        "  double complex, intent(in) :: c8(3)\n"
        "  integer(kind=8), intent(in) :: i8(3)\n"
        "  real, intent(in) :: r4(3)\n"
        "  logical(kind=1), intent(in) :: l1(3)\n"
        "  logical(kind=8), intent(in) :: l8(3)\n"
        "  complex, intent(out) :: c_result\n"
        "  double complex, intent(out) :: z_result\n"
        "  real, intent(out) :: r_result\n"
        "  logical(kind=8), intent(out) :: l_result\n"
        "  c_result = sum(c4) + product(c4) + dot_product(c4, r4)\n"
        "  z_result = sum(c8) + product(c8) + dot_product(c4, c8)\n"
        "  r_result = dot_product(i8, r4)\n"
        "  l_result = dot_product(l1, l8)\n"
        "end subroutine mixed_reductions\n";
    F2cOptions options = {"mixed_reductions.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "mixed-kind and complex reductions produce portable C17");
    expect(result.code != NULL && strstr(result.code, "f2c_sum_c") != NULL &&
               strstr(result.code, "f2c_product_z") != NULL &&
               strstr(result.code, "f2c_dot_c") != NULL &&
               strstr(result.code, "f2c_dot_z") != NULL,
           "complex SUM, PRODUCT, and DOT_PRODUCT use kind-specific helpers");
    expect(result.code != NULL && strstr(result.code, "f2c_dot_f") != NULL &&
               strstr(result.code, "f2c_dot_l") != NULL &&
               strstr(result.code, "sizeof(*(l1))") != NULL &&
               strstr(result.code, "sizeof(*(l8))") != NULL,
           "mixed numeric and non-default logical vectors retain their element representations");
    expect(result.code != NULL && strstr(result.code, "(f2c_complex_float)0") == NULL &&
               strstr(result.code, "(f2c_complex_double)0") == NULL,
           "complex reduction failure paths use portable structure values");
    f2c_result_free(&result);
}

static void test_mask_back_and_kind_lowering(void) {
    static const char source[] =
        "subroutine masked_reductions(values, selected, total, first, last, compact_count)\n"
        "  implicit none\n"
        "  integer, intent(in) :: values(4)\n"
        "  logical(kind=1), intent(in) :: selected(4)\n"
        "  integer, intent(out) :: total, first, last\n"
        "  integer(kind=1), intent(out) :: compact_count\n"
        "  total = sum(values, mask=selected) + product(values, mask=.false.)\n"
        "  first = maxloc(values, dim=1, mask=selected)\n"
        "  last = maxloc(array=values, dim=1, mask=selected, back=.true.)\n"
        "  compact_count = count(mask=selected, kind=1)\n"
        "end subroutine masked_reductions\n";
    F2cOptions options = {"masked_reductions.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "MASK, BACK, and KIND reduction arguments lower to C17");
    expect(result.code != NULL && strstr(result.code, "F2C_SUM_MASK") != NULL &&
               strstr(result.code, "F2C_PRODUCT_MASK") != NULL &&
               strstr(result.code, "F2C_MAXIMUM_LOCATION_MASK") != NULL,
           "masked value and location reductions use the filtered helper family");
    expect(result.code != NULL && strstr(result.code, "sizeof(*(selected))") != NULL &&
               strstr(result.code, "f2c_reduction_integer_result") != NULL,
           "logical mask kind and requested integer result kind remain explicit");
    f2c_result_free(&result);
}

static void test_array_result_lowering(void) {
    static const char source[] =
        "subroutine array_reductions(values, mask, dimension, totals, locations, coordinates)\n"
        "  implicit none\n"
        "  integer, intent(in) :: values(2, 3)\n"
        "  logical(kind=1), intent(in) :: mask(2, 3)\n"
        "  integer, intent(in) :: dimension\n"
        "  integer, allocatable, intent(out) :: totals(:)\n"
        "  integer, intent(out) :: locations(3), coordinates(2)\n"
        "  totals = sum(values + 1, dim=dimension, mask=mask)\n"
        "  locations = maxloc(values, dim=1, back=.true.)\n"
        "  coordinates = minloc(values, mask=mask)\n"
        "end subroutine array_reductions\n";
    F2cOptions options = {"array_reductions.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "array-valued reductions lower through the transform pipeline");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_transform_reduction_source_values") != NULL &&
               strstr(result.code, "f2c_reduction_logical_at((const void *)(mask)") != NULL,
           "array expressions are evaluated once and masks use their typed storage view");
    expect(result.code != NULL && strstr(result.code, "f2c_transform_dimension") != NULL &&
               strstr(result.code, "f2c_transform_result_extent_1") != NULL &&
               strstr(result.code, "f2c_transform_back") != NULL,
           "runtime DIM, dynamic result shape, and BACK remain explicit");
    expect(result.code != NULL && strstr(result.code, "free(totals)") != NULL &&
               strstr(result.code, "totals_extent_1") != NULL,
           "allocatable reduction results commit their new storage and shape");
    f2c_result_free(&result);
}

static void test_portable_complex_storage(void) {
    static const char source[] =
        "subroutine portable_complex(values, result)\n"
        "  implicit none\n"
        "  complex, intent(in) :: values(2, 2)\n"
        "  complex, intent(out) :: result(2)\n"
        "  result = sum(values, dim=1)\n"
        "  if (dot_product(values(:, 1), values(:, 2)) == (0.0, 0.0)) then\n"
        "    result = [(1.0, 2.0), (3.0, 4.0)]\n"
        "  end if\n"
        "end subroutine portable_complex\n";
    F2cOptions options = {"portable_complex.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "complex constructors and dimensional reductions produce C17");
    expect(result.code != NULL && strstr(result.code, "(f2c_complex_float)(") == NULL &&
               strstr(result.code, "(f2c_complex_double)(") == NULL,
           "complex values are never emitted through nonstandard structure casts");
    expect(result.code != NULL && strstr(result.code, "f2c_make_c(0.0f, 0.0f)") != NULL,
           "complex conformance failures use a type-correct zero value");
    f2c_result_free(&result);
}

int main(void) {
    test_argument_contracts();
    test_dot_product_contracts();
    test_typed_scalar_lowering();
    test_mixed_and_complex_lowering();
    test_mask_back_and_kind_lowering();
    test_array_result_lowering();
    test_portable_complex_storage();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
