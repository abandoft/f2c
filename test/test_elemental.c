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

static void expect_diagnostic(const char *source, const char *message, const char *description) {
    F2cOptions options = {"elemental_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    expect(result.code == NULL && result.error_count != 0U, description);
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_elemental_declaration_constraints(void) {
    static const char array_dummy[] =
        "elemental integer function invalid_dummy(values) result(answer)\n"
        "  integer, intent(in) :: values(2)\n"
        "  answer = values(1)\n"
        "end function invalid_dummy\n";
    static const char missing_intent[] =
        "elemental integer function invalid_intent(value) result(answer)\n"
        "  integer :: value\n"
        "  answer = value\n"
        "end function invalid_intent\n";
    static const char array_result[] = "elemental function invalid_result(value) result(answer)\n"
                                       "  integer, intent(in) :: value\n"
                                       "  integer :: answer(2)\n"
                                       "  answer = value\n"
                                       "end function invalid_result\n";
    static const char pointer_dummy[] = "elemental subroutine invalid_pointer(value)\n"
                                        "  integer, pointer, intent(in) :: value\n"
                                        "end subroutine invalid_pointer\n";
    expect_diagnostic(array_dummy,
                      "dummy argument 'values' of an ELEMENTAL procedure must be scalar",
                      "ELEMENTAL array dummy declarations suppress generated code");
    expect_diagnostic(missing_intent,
                      "dummy argument 'value' of an ELEMENTAL procedure requires INTENT",
                      "ELEMENTAL dummies without INTENT suppress generated code");
    expect_diagnostic(array_result, "result of an ELEMENTAL function must be scalar",
                      "ELEMENTAL array results suppress generated code");
    expect_diagnostic(pointer_dummy,
                      "dummy argument 'value' of an ELEMENTAL procedure cannot be ALLOCATABLE "
                      "or POINTER",
                      "ELEMENTAL POINTER dummies suppress generated code");
}

static void test_elemental_actual_conformance(void) {
    static const char extent_mismatch[] =
        "program extent_mismatch\n"
        "  implicit none\n"
        "  integer :: left(2), right(3), result(2)\n"
        "  result = combine(left, right)\n"
        "contains\n"
        "  elemental integer function combine(x, y) result(value)\n"
        "    integer, intent(in) :: x, y\n"
        "    value = x + y\n"
        "  end function combine\n"
        "end program extent_mismatch\n";
    static const char rank_mismatch[] = "program rank_mismatch\n"
                                        "  implicit none\n"
                                        "  integer :: vector(2), matrix(2,2), result(2)\n"
                                        "  result = combine(vector, matrix)\n"
                                        "contains\n"
                                        "  elemental integer function combine(x, y) result(value)\n"
                                        "    integer, intent(in) :: x, y\n"
                                        "    value = x + y\n"
                                        "  end function combine\n"
                                        "end program rank_mismatch\n";
    expect_diagnostic(extent_mismatch,
                      "ELEMENTAL procedure 'combine' has nonconformable actual argument extent "
                      "in dimension 1",
                      "known nonconformable ELEMENTAL extents are rejected in semantic analysis");
    expect_diagnostic(rank_mismatch,
                      "ELEMENTAL procedure 'combine' has nonconformable actual argument ranks 1 "
                      "and 2",
                      "nonconformable ELEMENTAL ranks are rejected in semantic analysis");
}

static void test_elemental_interface_compatibility(void) {
    static const char caller[] = "program interface_caller\n"
                                 "  implicit none\n"
                                 "  interface\n"
                                 "    elemental integer function evaluate(value) result(answer)\n"
                                 "      integer, intent(in) :: value\n"
                                 "    end function evaluate\n"
                                 "  end interface\n"
                                 "  print *, evaluate(1)\n"
                                 "end program interface_caller\n";
    static const char definition[] = "integer function evaluate(value) result(answer)\n"
                                     "  integer, intent(in) :: value\n"
                                     "  answer = value\n"
                                     "end function evaluate\n";
    F2cInput inputs[2] = {
        {caller, sizeof(caller) - 1U, {"interface_caller.f90", F2C_SOURCE_FREE, 0}},
        {definition, sizeof(definition) - 1U, {"evaluate.f90", F2C_SOURCE_FREE, 0}}};
    F2cResult result = f2c_transpile_project(inputs, 2U);
    expect(result.code == NULL && result.error_count != 0U,
           "project interfaces reject mismatched ELEMENTAL attributes");
    expect(result.diagnostics != NULL &&
               strstr(result.diagnostics, "incompatible ELEMENTAL attribute") != NULL,
           "ELEMENTAL interface mismatch has an actionable diagnostic");
    f2c_result_free(&result);
}

int main(void) {
    test_elemental_declaration_constraints();
    test_elemental_actual_conformance();
    test_elemental_interface_compatibility();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
