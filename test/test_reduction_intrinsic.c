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
    expect(result.code != NULL && strstr(result.code, "F2C_DOT(") != NULL &&
               strstr(result.code, "F2C_MAXIMUM_LOCATION(") != NULL &&
               strstr(result.code, "F2C_MINIMUM_LOCATION(") != NULL,
           "dot and location reductions lower from their typed intrinsic IDs");
    expect(result.code != NULL && strstr(result.code, "(*dot_kind) = INT32_C(4)") != NULL,
           "DOT_PRODUCT derives result kind from the dominant non-integer category");
    f2c_result_free(&result);
}

int main(void) {
    test_argument_contracts();
    test_dot_product_contracts();
    test_typed_scalar_lowering();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
