#include "f2c/f2c.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void expect_contains(const char *text, const char *needle, const char *message) {
    expect(text != NULL && strstr(text, needle) != NULL, message);
}

static void test_file_control_semantics(void) {
    static const char source[] =
        "program invalid_file_control\n"
        "  implicit none\n"
        "  integer :: unit, status\n"
        "  logical :: flag\n"
        "  real :: value\n"
        "  character(32) :: message\n"
        "  open(unit=unit, access='direct')\n"
        "  open(unit=unit, access='sequential', recl=16)\n"
        "  open(unit=unit, file='bad.tmp', status='scratch')\n"
        "  open(unit=unit, status='invalid')\n"
        "  close(unit=unit, status='erase')\n"
        "  inquire(unit=unit, file='bad.tmp', opened=flag)\n"
        "  inquire(opened=flag)\n"
        "  inquire(unit=unit, exist=status, number=flag, name=value)\n"
        "  backspace(unit='bad')\n"
        "  endfile(unit=unit, iomsg=status)\n"
        "end program invalid_file_control\n";
    F2cOptions options = {"invalid_file_control.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count >= 12U, "invalid file controls produce independent hard errors");
    expect(result.code == NULL, "invalid file controls suppress generated C");
    expect_contains(result.diagnostics, "OPEN ACCESS='DIRECT' requires RECL=",
                    "direct access requires a record length");
    expect_contains(result.diagnostics, "OPEN RECL= is valid only with ACCESS='DIRECT'",
                    "sequential access rejects a record length");
    expect_contains(result.diagnostics, "OPEN STATUS='SCRATCH' cannot specify FILE=",
                    "scratch files reject an external file name");
    expect_contains(result.diagnostics, "OPEN STATUS= has invalid value 'invalid'",
                    "OPEN rejects invalid constant STATUS values");
    expect_contains(result.diagnostics, "CLOSE STATUS= has invalid value 'erase'",
                    "CLOSE rejects invalid constant STATUS values");
    expect_contains(result.diagnostics, "INQUIRE requires exactly one of UNIT= or FILE=",
                    "INQUIRE requires one and only one target selector");
    expect_contains(result.diagnostics,
                    "INQUIRE EXIST= must be a definable scalar LOGICAL variable",
                    "INQUIRE validates logical result designators");
    expect_contains(result.diagnostics,
                    "INQUIRE NUMBER= must be a definable scalar INTEGER variable",
                    "INQUIRE validates integer result designators");
    expect_contains(result.diagnostics,
                    "INQUIRE NAME= must be a definable scalar CHARACTER variable",
                    "INQUIRE validates character result designators");
    expect_contains(result.diagnostics, "BACKSPACE UNIT= must be a scalar INTEGER",
                    "BACKSPACE validates its external unit");
    expect_contains(result.diagnostics,
                    "ENDFILE IOMSG= must be a definable scalar CHARACTER variable",
                    "ENDFILE validates its error message destination");
    f2c_result_free(&result);
}

int main(void) {
    test_file_control_semantics();
    if (failures != 0) {
        fprintf(stderr, "%d I/O semantic test(s) failed\n", failures);
        return 1;
    }
    puts("all I/O semantic tests passed");
    return 0;
}
