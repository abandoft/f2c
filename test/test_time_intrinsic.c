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

static void test_time_intrinsic_lowering(void) {
    static const char source[] = "program clock_values\n"
                                 "  implicit none\n"
                                 "  integer(kind=8) :: count, maximum, values(9)\n"
                                 "  real(kind=8) :: rate, elapsed\n"
                                 "  character(len=8) :: date\n"
                                 "  character(len=10) :: time\n"
                                 "  character(len=5) :: zone\n"
                                 "  call date_and_time(date, time, zone, values)\n"
                                 "  call date_and_time(time=time, values=values)\n"
                                 "  call date_and_time()\n"
                                 "  call system_clock(count, rate, maximum)\n"
                                 "  call system_clock()\n"
                                 "  call cpu_time(elapsed)\n"
                                 "end program clock_values\n";
    F2cResult result = transpile(source, "clock_values.f90");
    expect(result.error_count == 0U, "DATE_AND_TIME, SYSTEM_CLOCK, and CPU_TIME calls translate");
    expect(result.code != NULL && strstr(result.code, "f2c_date_and_time") != NULL &&
               strstr(result.code, "f2c_system_clock_count") != NULL &&
               strstr(result.code, "f2c_cpu_time") != NULL,
           "time intrinsic calls use independent generated C17 support");
    expect(result.code != NULL && strstr(result.code, "f2c_date_values_") != NULL &&
               strstr(result.code, "< 8U) abort()") != NULL,
           "DATE_AND_TIME validates and converts the VALUES array through typed storage");
    expect(result.code != NULL && strstr(result.code, "void system_clock(") == NULL,
           "time intrinsics are not emitted as unresolved external procedures");
    f2c_result_free(&result);
}

static void test_time_intrinsic_diagnostics(void) {
    static const char source[] = "program invalid_clocks\n"
                                 "  implicit none\n"
                                 "  integer :: scalar, short_values(7)\n"
                                 "  integer(kind=1) :: narrow_values(8)\n"
                                 "  real :: real_values(8), clock_value\n"
                                 "  character(len=8) :: text\n"
                                 "  call date_and_time(date=scalar)\n"
                                 "  call date_and_time(values=real_values)\n"
                                 "  call date_and_time(values=narrow_values)\n"
                                 "  call date_and_time(values=short_values)\n"
                                 "  call system_clock(count=clock_value)\n"
                                 "  call system_clock(count_rate=text)\n"
                                 "  call system_clock(count_max=1)\n"
                                 "  call cpu_time(scalar)\n"
                                 "  call cpu_time(1.0)\n"
                                 "  clock_value = cpu_time(clock_value)\n"
                                 "end program invalid_clocks\n";
    F2cResult result = transpile(source, "invalid_clocks.f90");
    expect(result.code == NULL && result.error_count >= 9U,
           "invalid time intrinsic contracts suppress generated C");
    expect(result.diagnostics != NULL &&
               strstr(result.diagnostics, "DATE must be a scalar CHARACTER") != NULL,
           "DATE_AND_TIME validates character result arguments");
    expect(result.diagnostics != NULL &&
               strstr(result.diagnostics, "decimal range of at least four") != NULL,
           "DATE_AND_TIME validates the VALUES kind and rank contract");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, "at least 8 elements") != NULL,
           "DATE_AND_TIME rejects a statically short VALUES array");
    expect(result.diagnostics != NULL &&
               strstr(result.diagnostics, "COUNT_RATE must be a scalar INTEGER or REAL") != NULL,
           "SYSTEM_CLOCK validates COUNT_RATE independently");
    expect(result.diagnostics != NULL &&
               strstr(result.diagnostics, "TIME must be a scalar REAL") != NULL &&
               strstr(result.diagnostics, "TIME must be definable") != NULL,
           "CPU_TIME requires a definable scalar REAL target");
    expect(result.diagnostics != NULL &&
               strstr(result.diagnostics, "must be invoked by a CALL") != NULL,
           "time intrinsic subroutines cannot be used as expression functions");
    f2c_result_free(&result);
}

static void test_explicit_external_override(void) {
    static const char source[] = "program custom_clock\n"
                                 "  implicit none\n"
                                 "  integer :: value\n"
                                 "  external system_clock\n"
                                 "  call system_clock(value)\n"
                                 "end program custom_clock\n"
                                 "subroutine system_clock(value)\n"
                                 "  implicit none\n"
                                 "  integer, intent(out) :: value\n"
                                 "  value = 17\n"
                                 "end subroutine system_clock\n";
    F2cResult result = transpile(source, "custom_clock.f90");
    expect(result.error_count == 0U,
           "an explicitly declared external procedure overrides a time intrinsic");
    expect(result.code != NULL && strstr(result.code, "system_clock(&value);") != NULL,
           "the explicit override emits the project procedure call");
    expect(result.code != NULL && strstr(result.code, "f2c_system_clock_count") == NULL,
           "an external override does not pull time intrinsic support into generated C");
    f2c_result_free(&result);
}

int main(void) {
    test_time_intrinsic_lowering();
    test_time_intrinsic_diagnostics();
    test_explicit_external_override();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
