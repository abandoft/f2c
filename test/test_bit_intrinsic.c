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

static void expect_diagnostic(const char *body, const char *message, const char *description) {
    char source[1024];
    F2cOptions options = {"bit_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "subroutine bit_negative()\n"
                                "  implicit none\n"
                                "  integer :: value\n"
                                "  integer(8) :: wide\n"
                                "  integer :: short(2), long(3)\n"
                                "  logical :: flag\n"
                                "%s\n"
                                "end subroutine bit_negative\n",
                                body);
    expect(length > 0 && (size_t)length < sizeof(source), "negative fixture is bounded");
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U, description);
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_valid_contracts(void) {
    static const char source[] = "subroutine bit_contracts(i8, i16, i32, i64, positions, flags)\n"
                                 "  implicit none\n"
                                 "  integer(1), intent(inout) :: i8\n"
                                 "  integer(2), intent(inout) :: i16\n"
                                 "  integer(4), intent(inout) :: i32\n"
                                 "  integer(8), intent(inout) :: i64\n"
                                 "  integer, intent(in) :: positions(4)\n"
                                 "  logical, intent(out) :: flags(4)\n"
                                 "  integer :: targets(4)\n"
                                 "  i8 = not(i8)\n"
                                 "  i16 = ibset(i16, 3)\n"
                                 "  i32 = ieor(i32, int(7, 4))\n"
                                 "  i64 = ishftc(i64, -3, bit_size(i64))\n"
                                 "  flags = btest(i32, positions)\n"
                                 "  call mvbits(i32, positions, 1, targets, positions)\n"
                                 "end subroutine bit_contracts\n";
    F2cOptions options = {"bit_contracts.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.error_count == 0U && result.code != NULL,
           "valid bit intrinsic type, kind, keyword, and elemental contracts are accepted");
    f2c_result_free(&result);
}

static void test_type_and_kind_diagnostics(void) {
    expect_diagnostic("  value = iand(1.0, 2)", "IAND argument i must be INTEGER",
                      "noninteger bit operands suppress generated code");
    expect_diagnostic("  wide = ior(wide, 1)",
                      "IOR arguments I and J must have the same INTEGER kind",
                      "mixed integer kinds suppress generated code");
    expect_diagnostic("  flag = btest(1, pos=1.0)", "BTEST argument pos must be INTEGER",
                      "noninteger bit positions suppress generated code");
}

static void test_constant_range_diagnostics(void) {
    expect_diagnostic("  value = ibset(0, 32)", "IBSET argument POS must be between 0 and 31",
                      "out-of-range bit positions suppress generated code");
    expect_diagnostic("  value = ibits(0, 31, 2)",
                      "IBITS requires POS + LEN to be at most BIT_SIZE(I) (32)",
                      "out-of-range bit slices suppress generated code");
    expect_diagnostic("  value = ishft(1, 33)", "ISHFT argument SHIFT must be between -32 and 32",
                      "out-of-range logical shifts suppress generated code");
    expect_diagnostic("  value = ishftc(1, 2, 1)", "ISHFTC argument SHIFT must be between -1 and 1",
                      "out-of-range circular shifts suppress generated code");
    expect_diagnostic("  value = ishftc(1, 0, 0)",
                      "ISHFTC argument SIZE must be between 1 and BIT_SIZE(I) (32)",
                      "invalid circular field sizes suppress generated code");
}

static void test_keyword_diagnostics(void) {
    expect_diagnostic("  value = ibset(value=0, pos=1)", "IBSET has no argument named 'value'",
                      "unknown bit intrinsic keywords suppress generated code");
    expect_diagnostic("  value = ibset(i=0, i=1)", "IBSET argument 'i' is specified more than once",
                      "duplicate bit intrinsic keywords suppress generated code");
    expect_diagnostic("  value = ibset(pos=1, 0)",
                      "positional argument in IBSET cannot follow a keyword argument",
                      "positional arguments after keywords suppress generated code");
    expect_diagnostic("  value = ibset(pos=1, j=0)", "IBSET requires argument i",
                      "missing required bit intrinsic keywords suppress generated code");
}

static void test_mvbits_diagnostics(void) {
    expect_diagnostic("  call mvbits(1.0, 0, 1, value, 0)", "MVBITS argument from must be INTEGER",
                      "noninteger MVBITS sources suppress generated code");
    expect_diagnostic("  call mvbits(wide, 0, 1, value, 0)",
                      "MVBITS arguments FROM and TO must have the same INTEGER kind",
                      "mixed MVBITS integer kinds suppress generated code");
    expect_diagnostic("  call mvbits(value, 0, 1, value + 1, 0)",
                      "MVBITS argument TO must be definable",
                      "nondefinable MVBITS targets suppress generated code");
    expect_diagnostic("  call mvbits(value, 31, 2, value, 0)",
                      "MVBITS requires FROMPOS + LEN to be at most BIT_SIZE(FROM) (32)",
                      "invalid MVBITS source ranges suppress generated code");
    expect_diagnostic("  call mvbits(value, 0, 2, value, 31)",
                      "MVBITS requires TOPOS + LEN to be at most BIT_SIZE(TO) (32)",
                      "invalid MVBITS target ranges suppress generated code");
    expect_diagnostic("  call mvbits(short, 0, 1, long, 0)",
                      "MVBITS has nonconformable extent in dimension 1",
                      "nonconformable MVBITS arrays suppress generated code");
    expect_diagnostic("  call mvbits(short, 0, 1, value, 0)",
                      "MVBITS argument TO must be an array conformable with every array input",
                      "scalar MVBITS targets are rejected for array invocations");
    expect_diagnostic("  call mvbits(from=value, frompos=0, len=1, to=value, where=0)",
                      "MVBITS has no argument named 'where'",
                      "unknown MVBITS keywords suppress generated code");
}

int main(void) {
    test_valid_contracts();
    test_type_and_kind_diagnostics();
    test_constant_range_diagnostics();
    test_keyword_diagnostics();
    test_mvbits_diagnostics();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
