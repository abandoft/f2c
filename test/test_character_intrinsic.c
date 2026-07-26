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
    char source[1280];
    F2cOptions options = {"character_intrinsic_negative.f90", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const int length = snprintf(source, sizeof(source),
                                "subroutine character_intrinsic_negative()\n"
                                "  implicit none\n"
                                "  character(len=4) :: result\n"
                                "  character(len=2) :: strings(2)\n"
                                "  integer :: code\n"
                                "%s\n"
                                "end subroutine character_intrinsic_negative\n",
                                body);
    expect(length > 0 && (size_t)length < sizeof(source), "negative fixture is bounded");
    result = f2c_transpile(source, (size_t)length, &options);
    expect(result.code == NULL && result.error_count != 0U, description);
    expect(result.diagnostics != NULL && strstr(result.diagnostics, message) != NULL, message);
    f2c_result_free(&result);
}

static void test_unit_length_substrings(void) {
    static const char source[] = "subroutine character_intrinsic_substrings(text, i, code)\n"
                                 "  implicit none\n"
                                 "  character(len=6) :: text\n"
                                 "  integer :: i, code\n"
                                 "  code = ichar(text(1:1))\n"
                                 "  code = ichar(text(i:i))\n"
                                 "  code = iachar(text(:1))\n"
                                 "  code = iachar(text(6:))\n"
                                 "end subroutine character_intrinsic_substrings\n";
    F2cOptions options = {"character_intrinsic_substrings.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "ICHAR and IACHAR accept statically unit-length substrings");
    f2c_result_free(&result);
}

static void test_type_and_length_diagnostics(void) {
    expect_diagnostic("  result = adjustl(1)", "ADJUSTL argument STRING must be CHARACTER",
                      "noncharacter adjustment arguments suppress generated code");
    expect_diagnostic("  code = ichar('AB')", "ICHAR argument C must have CHARACTER length one",
                      "ICHAR rejects statically known nonunit character lengths");
    expect_diagnostic("  code = index('abc', 'a', back=1)", "INDEX argument BACK must be LOGICAL",
                      "character search rejects nonlogical BACK arguments");
    expect_diagnostic("  result = repeat(strings, 2)", "REPEAT argument STRING must be scalar",
                      "REPEAT rejects array strings");
    expect_diagnostic("  result = trim(strings)", "TRIM argument STRING must be scalar",
                      "TRIM rejects array strings");
}

static void test_kind_and_value_diagnostics(void) {
    expect_diagnostic("  code = len_trim('a', kind=3)",
                      "KIND in LEN_TRIM must be a supported scalar INTEGER constant",
                      "unsupported result integer kinds suppress generated code");
    expect_diagnostic("  result = char(65, kind=4)",
                      "KIND in CHAR must be the supported default CHARACTER kind (1)",
                      "unsupported result character kinds suppress generated code");
    expect_diagnostic("  result = char(-1)",
                      "CHAR argument I must be between 0 and 255 for default CHARACTER",
                      "out-of-range default collating positions suppress generated code");
    expect_diagnostic("  result = repeat('a', -1)", "REPEAT argument NCOPIES must be nonnegative",
                      "negative repetition counts suppress generated code");
}

static void test_keyword_diagnostics(void) {
    expect_diagnostic("  code = scan(value='abc', set='a')", "SCAN has no argument named 'value'",
                      "unknown character intrinsic keywords suppress generated code");
    expect_diagnostic("  code = index(string='abc', string='a')",
                      "INDEX argument 'string' is specified more than once",
                      "duplicate character intrinsic keywords suppress generated code");
    expect_diagnostic("  code = verify(set='a', 'abc')",
                      "positional argument in VERIFY cannot follow a keyword argument",
                      "positional arguments after keywords suppress generated code");
    expect_diagnostic("  code = lge(string='a', string_b='b')",
                      "LGE has no argument named 'string'",
                      "unknown lexical comparison keywords suppress generated code");
    expect_diagnostic("  code = llt(string_a=1, string_b='b')",
                      "LLT argument STRING_A must be CHARACTER",
                      "noncharacter lexical comparison operands suppress generated code");
}

static void test_lexical_comparison_lowering(void) {
    static const char source[] =
        "subroutine lexical_comparison_lowering(left, right, values, result)\n"
        "  implicit none\n"
        "  character(len=4), intent(in) :: left, right, values(2)\n"
        "  logical, intent(out) :: result(6)\n"
        "  logical, parameter :: folded = lge(string_a='A', string_b='A ')\n"
        "  result(1) = lge(left, right)\n"
        "  result(2) = lgt(left, right)\n"
        "  result(3) = lle(left, right)\n"
        "  result(4) = llt(left, right)\n"
        "  result(5:6) = llt(values, 'Z')\n"
        "  if (.not. folded) error stop 1\n"
        "end subroutine lexical_comparison_lowering\n";
    F2cOptions options = {"lexical_comparison_lowering.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "lexical comparisons accept scalar and conformable elemental operands");
    expect(result.code != NULL && strstr(result.code, "f2c_character_compare(") != NULL &&
               strstr(result.code, "values[") != NULL,
           "lexical comparisons lower through the shared blank-padding comparator");
    f2c_result_free(&result);
}

int main(void) {
    test_unit_length_substrings();
    test_type_and_length_diagnostics();
    test_kind_and_value_diagnostics();
    test_keyword_diagnostics();
    test_lexical_comparison_lowering();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
