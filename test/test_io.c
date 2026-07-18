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
    static const char source[] = "program invalid_file_control\n"
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

static void test_file_control_codegen(void) {
    static const char source[] =
        "program file_control_codegen\n"
        "  implicit none\n"
        "  integer :: status, number, recl, nextrec\n"
        "  logical :: opened\n"
        "  character(32) :: message, name\n"
        "  open(12, file='sample.tmp', status='replace', access='sequential', "
        "action='readwrite', form='formatted', blank='zero', position='append', "
        "delim='quote', pad='no', iostat=status, iomsg=message, err=90)\n"
        "  backspace(12, iostat=status, iomsg=message, err=90)\n"
        "  endfile(12, iostat=status, iomsg=message, err=90)\n"
        "  inquire(unit=12, opened=opened, number=number, name=name, recl=recl, "
        "nextrec=nextrec, iostat=status, "
        "iomsg=message, err=90)\n"
        "  close(12, status='delete', iostat=status, iomsg=message, err=90)\n"
        "  stop\n"
        "90 error stop\n"
        "end program file_control_codegen\n";
    F2cOptions options = {"file_control_codegen.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U, "complete file controls lower without diagnostics");
    expect_contains(result.code, "f2c_open_unit_full",
                    "OPEN lowers every connection property through the file-unit model");
    expect_contains(result.code, "size_t expected_length = strlen(expected)",
                    "dynamic file-control options compare against bounded literal lengths");
    expect_contains(result.code, "f2c_backspace_unit",
                    "BACKSPACE lowers to sequential record positioning");
    expect_contains(result.code, "f2c_endfile_unit",
                    "ENDFILE lowers to portable physical truncation");
    expect_contains(result.code, "f2c_inquire_unit",
                    "INQUIRE lowers against live file-unit metadata");
    expect_contains(result.code, "f2c_assign_inquiry_character",
                    "INQUIRE character results preserve Fortran padding semantics");
    expect_contains(result.code, "f2c_close_unit_with_status",
                    "CLOSE forwards KEEP or DELETE disposition");
    expect_contains(result.code, "f2c_set_iomsg",
                    "file control statements define IOMSG together with IOSTAT");
    expect_contains(result.code, "if (!f2c_io_ok) goto f2c_label_90",
                    "file control failures retain ERR label control flow");
    f2c_result_free(&result);
}

static void test_print_semantics(void) {
    static const char source[] = "program invalid_print\n"
                                 "  implicit none\n"
                                 "  integer :: fmt, value\n"
                                 "  print *,\n"
                                 "  print fmt=*, value\n"
                                 "  print 1 + 2, value\n"
                                 "  print fmt, value\n"
                                 "end program invalid_print\n";
    F2cOptions options = {"invalid_print.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count >= 4U, "invalid PRINT forms produce independent hard errors");
    expect(result.code == NULL, "invalid PRINT forms suppress generated C");
    expect_contains(result.diagnostics, "malformed PRINT control or item-list syntax",
                    "PRINT syntax failures retain an I/O-specific diagnostic");
    expect_contains(result.diagnostics,
                    "PRINT FMT= INTEGER value must be a statement label or a variable defined by "
                    "ASSIGN",
                    "PRINT rejects arbitrary and unassigned INTEGER format expressions");
    expect(result.diagnostics == NULL ||
               strstr(result.diagnostics, "out of memory while parsing statement IR") == NULL,
           "malformed PRINT syntax is never misreported as allocation failure");
    f2c_result_free(&result);
}

static void test_print_codegen(void) {
    static const char source[] = "program print_codegen\n"
                                 "  implicit none\n"
                                 "  integer :: iterator\n"
                                 "  character(16) :: runtime_format\n"
                                 "  runtime_format = '(A,1X,I3)'\n"
                                 "  print '(A,1X,I3)', 'literal', 7\n"
                                 "  print 100, (iterator, iterator=1,3)\n"
                                 "  print runtime_format, 'runtime', 9\n"
                                 "100 format(3(I2,1X))\n"
                                 "end program print_codegen\n";
    F2cOptions options = {"print_codegen.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U,
           "literal, labeled, and runtime CHARACTER PRINT formats lower without diagnostics");
    expect_contains(result.code,
                    "f2c_format_initialize(&f2c_io_format, stdout, \"(A,1X,I3)\"",
                    "PRINT character literals use the shared formatted transfer engine");
    expect_contains(result.code, "f2c_format_initialize(&f2c_io_format, stdout, \"(3(i2,1x))\"",
                    "PRINT statement labels resolve through the shared format engine");
    expect_contains(result.code, "runtime_format, (size_t)(16)",
                    "runtime CHARACTER PRINT formats preserve their explicit Fortran length");
    expect_contains(result.code, "for (iterator = 1;",
                    "formatted PRINT implied-DO items lower through the structured item tree");
    f2c_result_free(&result);
}

int main(void) {
    test_file_control_semantics();
    test_file_control_codegen();
    test_print_semantics();
    test_print_codegen();
    if (failures != 0) {
        fprintf(stderr, "%d I/O semantic test(s) failed\n", failures);
        return 1;
    }
    puts("all I/O semantic tests passed");
    return 0;
}
