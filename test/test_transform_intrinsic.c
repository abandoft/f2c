#include "f2c/f2c.h"

#include "semantic/intrinsic.h"

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

static void expect_intrinsic(const char *name, F2cIntrinsicId expected) {
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(name);
    expect(signature != NULL && signature->id == expected, name);
    expect(signature != NULL && f2c_intrinsic_is_transformational(signature->id),
           "transformational intrinsic has a typed identity");
}

static void test_typed_identities(void) {
    expect_intrinsic("cshift", F2C_INTRINSIC_CSHIFT);
    expect_intrinsic("eoshift", F2C_INTRINSIC_EOSHIFT);
    expect_intrinsic("findloc", F2C_INTRINSIC_FINDLOC);
    expect_intrinsic("matmul", F2C_INTRINSIC_MATMUL);
    expect_intrinsic("pack", F2C_INTRINSIC_PACK);
    expect_intrinsic("reshape", F2C_INTRINSIC_RESHAPE);
    expect_intrinsic("spread", F2C_INTRINSIC_SPREAD);
    expect_intrinsic("transpose", F2C_INTRINSIC_TRANSPOSE);
    expect_intrinsic("unpack", F2C_INTRINSIC_UNPACK);
    expect(f2c_intrinsic_is_transformational(F2C_INTRINSIC_SUM),
           "array-valued reductions share the transformational pipeline");
    expect(!f2c_intrinsic_is_transformational(F2C_INTRINSIC_SIN),
           "elemental intrinsics are not classified as transformational");
}

static void expect_diagnostic(const char *declarations, const char *expression,
                              const char *message) {
    char source[4096];
    F2cOptions options = {"transform_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "program transform_negative\n"
                                "  implicit none\n"
                                "%s"
                                "  print *, %s\n"
                                "end program transform_negative\n",
                                declarations, expression);
    expect(length > 0 && (size_t)length < sizeof(source), "negative fixture is bounded");
    if (length <= 0 || (size_t)length >= sizeof(source))
        return;
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid transform suppresses generated code");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_argument_contracts(void) {
    expect_diagnostic("  integer :: values(4)\n", "reshape(values, 2)",
                      "RESHAPE argument SHAPE must be a rank-one INTEGER array");
    expect_diagnostic("  integer :: values(4), mask(4)\n", "pack(values, mask)",
                      "PACK argument MASK must be a LOGICAL scalar or an array conformable with "
                      "ARRAY");
    expect_diagnostic("  integer :: values(2,2), field(4)\n  logical :: mask(4)\n",
                      "unpack(values, mask, field)",
                      "UNPACK argument VECTOR must be a rank-one array");
    expect_diagnostic("  integer :: values(4)\n", "spread(values, 1, -1)",
                      "SPREAD NCOPIES must not be negative");
    expect_diagnostic("  integer :: values(4)\n", "cshift(values, [1, 2])",
                      "CSHIFT argument SHIFT must be an INTEGER scalar or a rank-(ARRAY rank - 1) "
                      "array");
    expect_diagnostic("  integer :: values(4)\n  logical :: boundary\n",
                      "eoshift(values, 1, boundary=boundary)",
                      "EOSHIFT argument BOUNDARY must be a scalar or rank-(ARRAY rank - 1) value "
                      "with ARRAY element type and kind");
    expect_diagnostic("  integer :: values(4)\n", "findloc(values, 1.0)",
                      "FINDLOC argument VALUE must be a scalar with ARRAY element type and kind");
    expect_diagnostic("  integer :: values(4)\n", "transpose(values)",
                      "TRANSPOSE argument MATRIX must be a rank-two array");
    expect_diagnostic("  integer :: left(2), right(2)\n", "matmul(left, right)",
                      "MATMUL does not accept two rank-one operands; use DOT_PRODUCT");
    expect_diagnostic("  integer :: values(4)\n  logical :: mask(4)\n",
                      "pack(array=values, mystery=mask)", "PACK has no argument named 'mystery'");
    expect_diagnostic("  integer :: values(4)\n", "reshape(values, [2, -1])",
                      "RESHAPE SHAPE extents must not be negative");
    expect_diagnostic("  integer :: values(4)\n", "reshape(values, [2, 2], order=[1, 1])",
                      "RESHAPE ORDER must be a permutation of result dimensions");
}

static void test_nested_lowering(void) {
    static const char source[] = "program nested_transform\n"
                                 "  implicit none\n"
                                 "  integer :: values(6), matrix(2,2), total\n"
                                 "  logical :: mask(6)\n"
                                 "  values = [1, 2, 3, 4, 5, 6]\n"
                                 "  mask = [.true., .false., .true., .false., .true., .false.]\n"
                                 "  matrix = reshape(pack(values, values > 2), [2, 2])\n"
                                 "  matrix = reshape(pack(values, values > 2), [2, 2]) + 1\n"
                                 "  total = sum(reshape(pack(values, mask), [3]))\n"
                                 "end program nested_transform\n";
    F2cOptions options = {"nested_transform.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "nested transformational expressions produce typed C17");
    expect(result.code != NULL && strstr(result.code, "f2c_array_transform_transform_") != NULL &&
               strstr(result.code, "f2c_array_assignment_transform_") != NULL &&
               strstr(result.code, "f2c_array_scalar_transform_") != NULL,
           "assignment and scalar contexts use the shared owned temporary pipeline");
    expect(result.code != NULL && strstr(result.code, "_extent_1") != NULL &&
               strstr(result.code, "f2c_inquiry_size") != NULL,
           "nested temporaries retain dynamic typed shape metadata");
    f2c_result_free(&result);
}

int main(void) {
    test_typed_identities();
    test_argument_contracts();
    test_nested_lowering();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
