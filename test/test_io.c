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
                                 "  print '(I)', value\n"
                                 "  print 999, value\n"
                                 "  print 100, value\n"
                                 "100 format(I)\n"
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
    expect_contains(result.diagnostics,
                    "invalid constant PRINT FORMAT: invalid edit descriptor field",
                    "constant CHARACTER formats are parsed and validated before code generation");
    expect_contains(result.diagnostics,
                    "PRINT FORMAT label 999 does not identify a valid FORMAT statement",
                    "missing FORMAT statement labels are rejected during semantic binding");
    expect_contains(result.diagnostics,
                    "PRINT FORMAT label 100 does not identify a valid FORMAT statement",
                    "references to malformed FORMAT statements are rejected");
    expect_contains(result.diagnostics,
                    "invalid FORMAT specification: invalid edit descriptor field",
                    "malformed labeled FORMAT statements retain their own syntax diagnostic");
    expect(result.diagnostics == NULL ||
               strstr(result.diagnostics, "out of memory while parsing statement IR") == NULL,
           "malformed PRINT syntax is never misreported as allocation failure");
    f2c_result_free(&result);
}

static void test_print_codegen(void) {
    static const char source[] = "program print_codegen\n"
                                 "  implicit none\n"
                                 "  integer :: iterator, assigned_format\n"
                                 "  character(16) :: runtime_format\n"
                                 "  runtime_format = '(A,1X,I3)'\n"
                                 "  print '(A,1X,I3)', 'literal', 7\n"
                                 "  print 100, (iterator, iterator=1,3)\n"
                                 "  print runtime_format, 'runtime', 9\n"
                                 "  assign 200 to assigned_format\n"
                                 "  print assigned_format, 4\n"
                                 "100 format(3(I2,1X))\n"
                                 "200 format('assigned',1X,I2)\n"
                                 "end program print_codegen\n";
    F2cOptions options = {"print_codegen.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U,
           "literal, labeled, and runtime CHARACTER PRINT formats lower without diagnostics");
    expect_contains(result.code, "static const f2c_format_instruction f2c_io_format_program_6[]",
                    "PRINT character literals lower to immutable FORMAT programs");
    expect_contains(result.code,
                    "f2c_format_initialize_program(&f2c_io_format, "
                    "f2c_unit_stream(6, false), "
                    "f2c_io_format_program_6",
                    "PRINT character literals bypass runtime FORMAT text parsing");
    expect_contains(result.code,
                    "f2c_format_initialize_program(&f2c_io_format, "
                    "f2c_unit_stream(6, false), "
                    "f2c_io_format_program_7",
                    "PRINT statement labels use their bound structured FORMAT program");
    expect_contains(result.code, "runtime_format, (size_t)(16)",
                    "runtime CHARACTER PRINT formats preserve their explicit Fortran length");
    expect_contains(result.code, "for (iterator = 1;",
                    "formatted PRINT implied-DO items lower through the structured item tree");
    expect_contains(result.code, "switch ((int32_t)(assigned_format))",
                    "assigned FORMAT variables lower to an explicit runtime selector");
    expect_contains(result.code, "case 200: f2c_io_format_program = f2c_io_assigned_format_10_200",
                    "assigned FORMAT selectors reference only resolved immutable programs");
    f2c_result_free(&result);
}

static void test_internal_file_constraints(void) {
    static const char source[] = "program invalid_internal_file\n"
                                 "  implicit none\n"
                                 "  character(8) :: record\n"
                                 "  integer :: value, status, transferred\n"
                                 "  read(record, '(I2)', advance='no', size=transferred, "
                                 "iostat=status) value\n"
                                 "  write(record, '(I2)', advance='no', iostat=status) value\n"
                                 "  read(record, iostat=status) value\n"
                                 "end program invalid_internal_file\n";
    F2cOptions options = {"invalid_internal_file.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count >= 3U,
           "invalid internal-file controls produce independent hard errors");
    expect(result.code == NULL, "invalid internal-file controls suppress generated C");
    expect_contains(result.diagnostics, "internal-file READ cannot use ADVANCE=, EOR=, or SIZE=",
                    "internal READ rejects nonadvancing controls");
    expect_contains(result.diagnostics, "internal-file WRITE cannot use ADVANCE=, EOR=, or SIZE=",
                    "internal WRITE rejects nonadvancing controls");
    expect_contains(result.diagnostics, "internal-file READ must be formatted",
                    "internal READ rejects unformatted transfer");
    f2c_result_free(&result);
}

static void test_internal_file_codegen(void) {
    static const char source[] = "program internal_file_codegen\n"
                                 "  implicit none\n"
                                 "  character(8) :: records(2)\n"
                                 "  integer :: value\n"
                                 "  value = 17\n"
                                 "  write(records, '(I3/I3)') value, value\n"
                                 "  read(records, '(I3)') value\n"
                                 "end program internal_file_codegen\n";
    F2cOptions options = {"internal_file_codegen.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U, "valid internal-file transfers lower without diagnostics");
    expect_contains(result.code, "f2c_io_stream f2c_internal_stream",
                    "internal files lower to a memory record stream");
    expect_contains(result.code, "f2c_stream_initialize_internal(&f2c_internal_stream, records",
                    "internal CHARACTER arrays bind directly to their record storage");
    expect(result.code == NULL || strstr(result.code, "FILE *f2c_internal_file") == NULL,
           "internal transfers never allocate a temporary libc file");
    f2c_result_free(&result);
}

static void test_record_transfer_constraints(void) {
    static const char source[] = "program invalid_record_transfer\n"
                                 "  implicit none\n"
                                 "  integer :: value, status\n"
                                 "  character(8) :: record\n"
                                 "  namelist /values/ value\n"
                                 "  read(10, rec=0, iostat=status) value\n"
                                 "  read(record, '(I2)', rec=1, iostat=status) value\n"
                                 "  read(10, *, rec=1, iostat=status) value\n"
                                 "  write(10, nml=values, rec=1, iostat=status)\n"
                                 "  read(10, rec=1, end=90, iostat=status) value\n"
                                 "  read(10, advance='no', iostat=status) value\n"
                                 "90 continue\n"
                                 "end program invalid_record_transfer\n";
    F2cOptions options = {"invalid_record_transfer.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count >= 6U,
           "invalid direct and unformatted transfer combinations produce hard errors");
    expect(result.code == NULL, "invalid record transfers suppress generated C");
    expect_contains(result.diagnostics, "READ REC= must be positive",
                    "constant direct record numbers must be positive");
    expect_contains(result.diagnostics, "internal-file READ cannot specify REC=",
                    "internal files reject direct record selectors");
    expect_contains(result.diagnostics, "direct-access READ cannot use list-directed formatting",
                    "direct access rejects list-directed formatting");
    expect_contains(result.diagnostics, "direct-access WRITE cannot use NAMELIST",
                    "direct access rejects NAMELIST transfers");
    expect_contains(result.diagnostics,
                    "direct-access READ cannot use END=, ADVANCE=, EOR=, or SIZE=",
                    "direct access rejects sequential record conditions");
    expect_contains(result.diagnostics, "unformatted READ cannot use ADVANCE=, EOR=, or SIZE=",
                    "unformatted transfers reject formatted nonadvancing controls");
    f2c_result_free(&result);
    {
        static const char dynamic_source[] = "program invalid_dynamic_unformatted\n"
                                             "  implicit none\n"
                                             "  type :: payload\n"
                                             "    integer, allocatable :: values(:)\n"
                                             "  end type payload\n"
                                             "  type(payload) :: object\n"
                                             "  write(10) object\n"
                                             "end program invalid_dynamic_unformatted\n";
        F2cResult dynamic = f2c_transpile(dynamic_source, sizeof(dynamic_source) - 1U, &options);
        expect(dynamic.error_count != 0U,
               "default unformatted I/O rejects derived objects with dynamic components");
        expect_contains(dynamic.diagnostics, "with dynamic components requires defined I/O",
                        "dynamic derived storage never falls through to an aborting emitter");
        f2c_result_free(&dynamic);
    }
    {
        static const char dynamic_source[] = "program invalid_dynamic_formatted\n"
                                             "  implicit none\n"
                                             "  type :: payload\n"
                                             "    integer, pointer :: values(:)\n"
                                             "  end type payload\n"
                                             "  type(payload) :: object\n"
                                             "  write(*, *) object\n"
                                             "end program invalid_dynamic_formatted\n";
        F2cResult dynamic = f2c_transpile(dynamic_source, sizeof(dynamic_source) - 1U, &options);
        expect(dynamic.error_count != 0U,
               "default formatted I/O rejects derived objects with dynamic components");
        expect_contains(dynamic.diagnostics,
                        "formatted WRITE of derived type 'payload' with dynamic components "
                        "requires defined I/O",
                        "dynamic formatted derived I/O never reaches a runtime placeholder");
        f2c_result_free(&dynamic);
    }
}

static void test_record_transfer_codegen(void) {
    static const char source[] =
        "program record_transfer_codegen\n"
        "  implicit none\n"
        "  integer :: status, value, values(3)\n"
        "  character(32) :: message\n"
        "  open(31, file='records.tmp', status='replace', access='direct', recl=32, &\n"
        "       form='unformatted', iostat=status, iomsg=message)\n"
        "  write(31, rec=2, iostat=status, iomsg=message) values\n"
        "  read(31, rec=2, iostat=status, iomsg=message) values\n"
        "  close(31, status='delete')\n"
        "  open(32, status='scratch', form='unformatted', iostat=status)\n"
        "  write(32, iostat=status) value\n"
        "  rewind(32)\n"
        "  read(32, iostat=status) value\n"
        "  close(32)\n"
        "end program record_transfer_codegen\n";
    F2cOptions options = {"record_transfer_codegen.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U,
           "direct and sequential unformatted transfers lower without diagnostics");
    expect_contains(result.code, "f2c_transfer_begin(&f2c_io_transfer_state",
                    "record transfers validate the live unit before touching data");
    expect_contains(result.code, "f2c_stream_initialize_external_record",
                    "direct records use a bounded stream view");
    expect_contains(result.code, "f2c_record_write_u64",
                    "sequential unformatted transfers emit explicit record framing");
    expect_contains(result.code, "f2c_binary_write(f2c_io_file",
                    "unformatted output uses binary rather than list-directed serialization");
    expect_contains(result.code, "f2c_transfer_end(&f2c_io_transfer_state",
                    "every external transfer closes its record before status branching");
    f2c_result_free(&result);
}

static void test_iolength_semantics(void) {
    static const char source[] = "program invalid_iolength\n"
                                 "  implicit none\n"
                                 "  type :: dynamic_payload\n"
                                 "    integer, allocatable :: values(:)\n"
                                 "  end type dynamic_payload\n"
                                 "  integer :: length, value\n"
                                 "  real :: wrong_type\n"
                                 "  type(dynamic_payload) :: dynamic\n"
                                 "  inquire(iolength=wrong_type) value\n"
                                 "  inquire(iolength=1) value\n"
                                 "  inquire(iolength=length, unit=10) value\n"
                                 "  inquire(iolength=length)\n"
                                 "  inquire(iolength=length) (value, value=1, 3, 0)\n"
                                 "  inquire(iolength=length) dynamic\n"
                                 "end program invalid_iolength\n";
    static const char defined_source[] = "module defined_iolength_type\n"
                                         "  implicit none\n"
                                         "  type :: payload\n"
                                         "    integer :: value\n"
                                         "  contains\n"
                                         "    procedure :: write_unformatted => write_payload\n"
                                         "    generic :: write(unformatted) => write_unformatted\n"
                                         "  end type payload\n"
                                         "contains\n"
                                         "  subroutine write_payload(dtv, unit, iostat, iomsg)\n"
                                         "    class(payload), intent(in) :: dtv\n"
                                         "    integer, intent(in) :: unit\n"
                                         "    integer, intent(out) :: iostat\n"
                                         "    character(*), intent(inout) :: iomsg\n"
                                         "    iostat = 0\n"
                                         "  end subroutine write_payload\n"
                                         "end module defined_iolength_type\n"
                                         "program invalid_defined_iolength\n"
                                         "  use defined_iolength_type\n"
                                         "  implicit none\n"
                                         "  integer :: length\n"
                                         "  type(payload) :: value\n"
                                         "  inquire(iolength=length) value\n"
                                         "end program invalid_defined_iolength\n";
    F2cOptions options = {"invalid_iolength.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count >= 5U, "invalid IOLENGTH forms produce independent hard errors");
    expect(result.code == NULL, "invalid IOLENGTH forms suppress generated C");
    expect_contains(result.diagnostics,
                    "INQUIRE IOLENGTH= must be a definable scalar INTEGER variable",
                    "IOLENGTH validates its result variable type and definability");
    expect_contains(result.diagnostics,
                    "INQUIRE(IOLENGTH=) cannot contain other inquiry specifiers",
                    "IOLENGTH is kept distinct from file and unit inquiries");
    expect_contains(result.diagnostics, "INQUIRE(IOLENGTH=) requires a nonempty output list",
                    "IOLENGTH requires an output-item list");
    expect_contains(result.diagnostics, "I/O implied-DO step cannot be zero",
                    "IOLENGTH rejects a constant zero implied-DO step before code generation");
    expect_contains(result.diagnostics,
                    "INQUIRE(IOLENGTH=) output item of derived type 'dynamic_payload' has "
                    "pointer, allocatable, or procedure-pointer subcomponents",
                    "IOLENGTH rejects processor representations with dynamic components");
    f2c_result_free(&result);

    options.source_name = "invalid_defined_iolength.f90";
    result = f2c_transpile(defined_source, sizeof(defined_source) - 1U, &options);
    expect(result.error_count != 0U,
           "IOLENGTH rejects derived output items that require defined unformatted I/O");
    expect_contains(result.diagnostics, "requires defined unformatted I/O",
                    "IOLENGTH reports the defined-I/O restriction explicitly");
    f2c_result_free(&result);
}

static void test_iolength_codegen(void) {
    static const char source[] =
        "program iolength_codegen\n"
        "  implicit none\n"
        "  type :: payload\n"
        "    integer :: identifier\n"
        "    character(3) :: name\n"
        "  end type payload\n"
        "  integer :: length, iterator, values(3)\n"
        "  integer(kind=8) :: length8\n"
        "  complex :: value\n"
        "  type(payload) :: object\n"
        "  inquire(iolength=length) values(3:1:-1), (iterator * 2, iterator=1, 3), &\n"
        "       value, object, side_effect()\n"
        "  inquire(iolength=length8) values\n"
        "contains\n"
        "  integer function side_effect()\n"
        "    side_effect = 42\n"
        "  end function side_effect\n"
        "end program iolength_codegen\n";
    F2cOptions options = {"iolength_codegen.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U,
           "IOLENGTH lowers arrays, implied-DO items, complex values, and derived values");
    expect_contains(result.code, "f2c_stream_initialize_counter(&f2c_iolength_stream)",
                    "IOLENGTH uses the same unformatted transfer representation in count mode");
    expect_contains(result.code, "f2c_unformatted_extent_1",
                    "IOLENGTH expands array sections in scalar element order");
    expect_contains(result.code, "while (f2c_iolength_status == F2C_IO_STATUS_OK",
                    "IOLENGTH implied-DO bounds are captured once in a checked loop");
    expect_contains(result.code, "f2c_complex_float f2c_unformatted_value = (",
                    "complex output expressions are captured before extracting both parts");
    expect_contains(result.code, "f2c_inquiry_size_integer(f2c_iolength_stream.position, 4)",
                    "IOLENGTH checks the result against the target INTEGER kind");
    expect_contains(result.code, "f2c_inquiry_size_integer(f2c_iolength_stream.position, 8)",
                    "IOLENGTH preserves a nondefault result INTEGER kind");
    f2c_result_free(&result);
}

int main(void) {
    test_file_control_semantics();
    test_file_control_codegen();
    test_print_semantics();
    test_print_codegen();
    test_internal_file_constraints();
    test_internal_file_codegen();
    test_record_transfer_constraints();
    test_record_transfer_codegen();
    test_iolength_semantics();
    test_iolength_codegen();
    if (failures != 0) {
        fprintf(stderr, "%d I/O semantic test(s) failed\n", failures);
        return 1;
    }
    puts("all I/O semantic tests passed");
    return 0;
}
