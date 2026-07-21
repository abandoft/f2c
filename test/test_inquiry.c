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

static void expect_assumed_size_diagnostic(const char *array_declaration, const char *expression,
                                           const char *message) {
    char source[2048];
    F2cOptions options = {"assumed_size_inquiry_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "subroutine assumed_size_inquiry_negative(n, array)\n"
                                "  implicit none\n"
                                "  integer :: n, dim\n"
                                "  %s\n"
                                "  dim = 1\n"
                                "  print *, %s\n"
                                "end subroutine assumed_size_inquiry_negative\n",
                                array_declaration, expression);
    expect(length > 0 && (size_t)length < sizeof(source),
           "assumed-size negative inquiry fixture fits buffer");
    if (length <= 0 || (size_t)length >= sizeof(source))
        return;
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "invalid assumed-size inquiry suppresses generated code");
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

static void test_assumed_size_constraints(void) {
    static const char source[] = "subroutine assumed_size_inquiry(n, array, result)\n"
                                 "  implicit none\n"
                                 "  integer :: n, dim, array(0:n-1, -2:*), result(7)\n"
                                 "  dim = 1\n"
                                 "  result(1) = size(array, dim)\n"
                                 "  result(2:3) = lbound(array)\n"
                                 "  result(4) = lbound(array, 2)\n"
                                 "  result(5) = ubound(array, dim)\n"
                                 "  result(6) = size(array(:, -2:0))\n"
                                 "  result(7) = array(n - 1, 0)\n"
                                 "end subroutine assumed_size_inquiry\n";
    F2cOptions options = {"assumed_size_inquiry.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "legal assumed-size bounds, sections, and inquiries transpile");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_inquiry_extent((int64_t)(dim), 1U") != NULL,
           "dynamic SIZE and UBOUND hide the unknown final extent");
    expect(result.code != NULL && strstr(result.code, "(const size_t[]){") != NULL &&
               strstr(result.code, "(size_t)(1U)") != NULL,
           "LBOUND models the final assumed-size dimension without inventing its extent");
    f2c_result_free(&result);

    expect_assumed_size_diagnostic("integer :: array(n, *)", "shape(array)",
                                   "SHAPE source cannot be an assumed-size array");
    expect_assumed_size_diagnostic("integer :: array(n, *)", "size(array)",
                                   "size of an assumed-size array requires DIM");
    expect_assumed_size_diagnostic(
        "integer :: array(n, *)", "size(array, 2)",
        "DIM in size for an assumed-size array must be less than array rank 2");
    expect_assumed_size_diagnostic("integer :: array(n, *)", "ubound(array)",
                                   "ubound of an assumed-size array requires DIM");
    expect_assumed_size_diagnostic(
        "integer :: array(n, *)", "ubound(array, 2)",
        "DIM in ubound for an assumed-size array must be less than array rank 2");
    expect_assumed_size_diagnostic(
        "integer :: array(n, *)", "sum(array(:, :))",
        "array section in the final dimension of an assumed-size array requires an upper bound");
    expect_assumed_size_diagnostic("integer :: array(n, *)", "sum(array)",
                                   "cannot appear in an expression that requires its shape");
    expect_assumed_size_diagnostic("integer :: array(n, *)", "array",
                                   "I/O item cannot be a whole assumed-size array");
    expect_assumed_size_diagnostic(
        "integer :: array(*)", "size(array, dim)",
        "DIM in size for an assumed-size array must be less than array rank 1");

    {
        static const char assignment[] = "subroutine assign_assumed_size(n, array)\n"
                                         "  integer :: n, array(n, *)\n"
                                         "  array = 0\n"
                                         "end subroutine assign_assumed_size\n";
        result = f2c_transpile(assignment, sizeof(assignment) - 1U, &options);
        expect(result.code == NULL && result.error_count != 0U,
               "whole assumed-size assignment is rejected before code generation");
        expect(result.diagnostics != NULL &&
                   strstr(result.diagnostics,
                          "assignment target cannot be a whole assumed-size array") != NULL,
               "whole assumed-size assignment has an actionable diagnostic");
        f2c_result_free(&result);
    }

    {
        static const char caller[] = "subroutine forward_assumed_size(n, array)\n"
                                     "  integer :: n, array(n, *)\n"
                                     "  call consume_shape(array)\n"
                                     "end subroutine forward_assumed_size\n";
        static const char callee[] = "subroutine consume_shape(array)\n"
                                     "  integer :: array(:, :)\n"
                                     "end subroutine consume_shape\n";
        F2cInput inputs[2] = {
            {caller, sizeof(caller) - 1U, {"forward_assumed_size.f90", F2C_SOURCE_FREE, 0}},
            {callee, sizeof(callee) - 1U, {"consume_shape.f90", F2C_SOURCE_FREE, 0}}};
        result = f2c_transpile_project(inputs, 2U);
        expect(result.code == NULL && result.error_count != 0U,
               "whole assumed-size actual cannot satisfy an assumed-shape dummy");
        expect(result.diagnostics != NULL &&
                   strstr(result.diagnostics, "whole assumed-size array") != NULL,
               "assumed-size to assumed-shape association has an actionable diagnostic");
        f2c_result_free(&result);
    }
}

int main(void) {
    test_array_argument();
    test_dimension();
    test_kind();
    test_keyword();
    test_assumed_size_constraints();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
