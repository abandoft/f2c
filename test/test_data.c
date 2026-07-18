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

static F2cResult transpile(const char *name, const char *source) {
    F2cOptions options = {name, F2C_SOURCE_FREE, 0};
    return f2c_transpile(source, strlen(source), &options);
}

static void expect_failure(const char *name, const char *source, const char *diagnostic,
                           const char *message) {
    F2cResult result = transpile(name, source);
    expect(result.error_count != 0U && result.code == NULL, message);
    expect(result.diagnostics != NULL && strstr(result.diagnostics, diagnostic) != NULL,
           "DATA failure reports the precise validation rule");
    f2c_result_free(&result);
}

static void test_valid_data_semantics(void) {
    static const char source[] = "program valid_data\n"
                                 "  integer, parameter :: repeat = 2\n"
                                 "  integer :: matrix(2,2), vector(2), mixed(2), tail, i, j\n"
                                 "  character(len=8) :: label\n"
                                 "  data ((matrix(i,j), i=1,2), j=1,2) / 1, 2, 3, 4 /\n"
                                 "  data vector / repeat*5 /, label / 'a/b,c' /\n"
                                 "  data mixed, tail / 6, 7, 8 /\n"
                                 "end program valid_data\n";
    F2cResult result = transpile("valid_data.f90", source);
    expect(result.error_count == 0U && result.code != NULL,
           "nested, repeated, multi-group DATA reaches typed C17 emission");
    expect(result.code != NULL && strstr(result.code, "matrix[") != NULL &&
               strstr(result.code, "vector[1] = 5;") != NULL &&
               strstr(result.code, "mixed[1] = 7;") != NULL &&
               strstr(result.code, "static int32_t tail = 8;") != NULL,
           "validated implied-DO and repeated values expand in Fortran order");
    expect(result.code != NULL && strstr(result.code, "static bool f2c_data_initialized_") != NULL,
           "non-scalar DATA lowering is protected by one-time procedure initialization");
    expect(result.code != NULL && strstr(result.code, "a/b,c") != NULL,
           "character DATA payload delimiters survive canonical token parsing");
    f2c_result_free(&result);
}

static void test_data_diagnostics(void) {
    expect_failure("missing_separator.f90",
                   "program missing_separator\n"
                   "  integer :: first, second\n"
                   "  data first / 1 / second / 2 /\n"
                   "end program missing_separator\n",
                   "malformed DATA statement group syntax",
                   "DATA groups require an explicit comma separator");
    expect_failure("invalid_repeat.f90",
                   "program invalid_repeat\n"
                   "  integer :: value\n"
                   "  data value / 0*1 /\n"
                   "end program invalid_repeat\n",
                   "DATA repeat must be a positive scalar INTEGER constant",
                   "DATA rejects zero repetition factors");
    expect_failure("nonconstant_value.f90",
                   "program nonconstant_value\n"
                   "  integer :: target, source\n"
                   "  data target / source /\n"
                   "end program nonconstant_value\n",
                   "DATA value must be a scalar initialization expression",
                   "DATA rejects nonconstant initializer values");
    expect_failure("count_mismatch.f90",
                   "program count_mismatch\n"
                   "  integer :: values(2)\n"
                   "  data values / 1 /\n"
                   "end program count_mismatch\n",
                   "DATA value count 1 does not match target element count 2",
                   "DATA validates expanded whole-array element counts");
    expect_failure("dummy_target.f90",
                   "subroutine dummy_target(value)\n"
                   "  integer :: value\n"
                   "  data value / 1 /\n"
                   "end subroutine dummy_target\n",
                   "DATA target must be a definable non-dummy, non-dynamic variable",
                   "DATA rejects dummy-argument targets");
    expect_failure("type_mismatch.f90",
                   "program type_mismatch\n"
                   "  logical :: flag\n"
                   "  data flag / 1 /\n"
                   "end program type_mismatch\n",
                   "DATA value type or kind is incompatible with its target",
                   "DATA validates target and initializer types");
    expect_failure("duplicate_initializer.f90",
                   "program duplicate_initializer\n"
                   "  integer :: value = 1\n"
                   "  data value / 2 /\n"
                   "end program duplicate_initializer\n",
                   "DATA target 'value' already has an initializer",
                   "DATA rejects a second initializer for the same scalar object");
    expect_failure("zero_step.f90",
                   "program zero_step\n"
                   "  integer :: values(2), i\n"
                   "  data (values(i), i=1,2,0) / 1, 2 /\n"
                   "end program zero_step\n",
                   "DATA implied-DO step cannot be zero",
                   "DATA rejects zero-step implied DO expansion");
}

int main(void) {
    test_valid_data_semantics();
    test_data_diagnostics();
    if (failures != 0)
        fprintf(stderr, "%d DATA semantic test(s) failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
