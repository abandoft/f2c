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

static F2cResult transpile(const char *source, const char *name) {
    F2cOptions options = {name, F2C_SOURCE_FREE, 0};
    return f2c_transpile(source, strlen(source), &options);
}

static void test_random_intrinsic_lowering(void) {
    static const char source[] = "program random_values\n"
                                 "  implicit none\n"
                                 "  integer :: size, seed(4)\n"
                                 "  real :: scalar, values(2,3)\n"
                                 "  call random_seed(size=size)\n"
                                 "  call random_seed(put=seed)\n"
                                 "  call random_number(scalar)\n"
                                 "  call random_number(values(:,2:3))\n"
                                 "  call random_seed(get=seed)\n"
                                 "end program random_values\n";
    F2cResult result = transpile(source, "random_values.f90");
    expect(result.error_count == 0U, "RANDOM_NUMBER and RANDOM_SEED calls translate");
    expect(result.code != NULL && strstr(result.code, "static _Thread_local uint64_t") != NULL,
           "generated random state is request-local per thread");
    expect(result.code != NULL && strstr(result.code, "f2c_random_seed_put") != NULL &&
               strstr(result.code, "f2c_random_seed_get") != NULL,
           "RANDOM_SEED PUT and GET use explicit generated support");
    expect(result.code != NULL && strstr(result.code, "f2c_random_index_") != NULL,
           "array RANDOM_NUMBER lowers every element through an explicit loop");
    f2c_result_free(&result);
}

static void test_random_intrinsic_diagnostics(void) {
    static const char source[] = "program invalid_random\n"
                                 "  implicit none\n"
                                 "  integer :: integer_value, short_seed(1)\n"
                                 "  real :: real_value\n"
                                 "  call random_number(1.0)\n"
                                 "  call random_number(integer_value)\n"
                                 "  call random_seed(size=real_value)\n"
                                 "  call random_seed(put=short_seed)\n"
                                 "  call random_seed(size=integer_value, get=short_seed)\n"
                                 "  real_value = random_number(real_value)\n"
                                 "end program invalid_random\n";
    F2cResult result = transpile(source, "invalid_random.f90");
    expect(result.code == NULL && result.error_count >= 6U,
           "invalid random intrinsic contracts suppress generated C");
    expect(result.diagnostics != NULL &&
               strstr(result.diagnostics, "HARVEST must be definable") != NULL,
           "RANDOM_NUMBER rejects a nondefinable harvest");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, "supported REAL kind") != NULL,
           "RANDOM_NUMBER rejects a non-REAL harvest");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, "at least 2 elements") != NULL,
           "RANDOM_SEED rejects a statically short seed vector");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, "at most one of SIZE") != NULL,
           "RANDOM_SEED rejects conflicting optional arguments");
    expect(result.diagnostics != NULL &&
               strstr(result.diagnostics, "must be invoked by a CALL") != NULL,
           "random subroutines cannot be used as expression functions");
    f2c_result_free(&result);
}

static void test_explicit_external_override(void) {
    static const char source[] = "program custom_random\n"
                                 "  implicit none\n"
                                 "  integer :: value\n"
                                 "  external random_number\n"
                                 "  call random_number(value)\n"
                                 "end program custom_random\n"
                                 "subroutine random_number(value)\n"
                                 "  implicit none\n"
                                 "  integer, intent(out) :: value\n"
                                 "  value = 17\n"
                                 "end subroutine random_number\n";
    F2cResult result = transpile(source, "custom_random.f90");
    expect(result.error_count == 0U,
           "an explicitly declared external procedure overrides a same-named intrinsic");
    expect(result.code != NULL && strstr(result.code, "random_number(&value);") != NULL,
           "the override emits the project procedure call");
    expect(result.code != NULL && strstr(result.code, "F2C_RANDOM_DEFAULT_STATE") == NULL,
           "an external override does not pull random intrinsic support into generated C");
    f2c_result_free(&result);
}

int main(void) {
    test_random_intrinsic_lowering();
    test_random_intrinsic_diagnostics();
    test_explicit_external_override();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
