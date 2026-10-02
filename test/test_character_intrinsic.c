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
    expect_diagnostic("  result = strings(1)(1:3)", "exceeds declared length 2",
                      "array-element substring uses the parent element length");
    expect_diagnostic("  result = result(1:2:1)", "cannot have a stride",
                      "substring ranges do not accept array-section strides");
    expect_diagnostic("  result = result(1)", "requires a lower:upper range",
                      "character indexing without a colon is not a substring");
    expect_diagnostic("  result = strings(1)(0:1)", "lower bound must be at least one",
                      "nonempty out-of-bounds array-element substrings are rejected");
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

static void test_array_dynamic_component_constraints(void) {
    static const char *const selections[] = {"records%allocated", "records%pointed",
                                             "records%allocated(:2)", "records%child%allocated",
                                             "records%child%pointed"};
    size_t selection;
    for (selection = 0U; selection < sizeof(selections) / sizeof(selections[0]); ++selection) {
        char source[2048];
        F2cOptions options = {"array_dynamic_component.f90", F2C_SOURCE_FREE, 0};
        F2cResult result;
        const int length = snprintf(source, sizeof(source),
                                    "subroutine invalid_component()\n"
                                    "implicit none\n"
                                    "type :: leaf_t\n"
                                    " character(:), allocatable :: allocated\n"
                                    " character(:), pointer :: pointed\n"
                                    "end type\n"
                                    "type :: record_t\n"
                                    " character(:), allocatable :: allocated\n"
                                    " character(:), pointer :: pointed\n"
                                    " type(leaf_t) :: child\n"
                                    "end type\n"
                                    "type(record_t) :: records(2)\n"
                                    "%s = 'ab'\n"
                                    "end subroutine\n",
                                    selections[selection]);
        expect(length > 0 && (size_t)length < sizeof(source), "component fixture is bounded");
        result = f2c_transpile(source, (size_t)length, &options);
        expect(result.code == NULL && result.error_count != 0U,
               "dynamic component after an array part-reference suppresses generated code");
        expect(result.diagnostics != NULL &&
                   strstr(result.diagnostics, "ALLOCATABLE or POINTER component") != NULL,
               "dynamic component constraint is diagnosed by typed designator validation");
        f2c_result_free(&result);
    }
}

static void test_vector_substring_out_constraints(void) {
    static const char *const intents[] = {"out", "inout"};
    size_t intent;
    for (intent = 0U; intent < sizeof(intents) / sizeof(intents[0]); ++intent) {
        char source[768];
        F2cOptions options = {"vector_substring_out.f90", F2C_SOURCE_FREE, 0};
        F2cResult result;
        const int length = snprintf(source, sizeof(source),
                                    "program invalid_actual\n"
                                    "implicit none\n"
                                    "character(8) :: records(3)\n"
                                    "call edit(records([3,1,3])(2:4))\n"
                                    "contains\n"
                                    "subroutine edit(values)\n"
                                    "character(*), intent(%s) :: values(:)\n"
                                    "values = 'abc'\n"
                                    "end subroutine\n"
                                    "end program\n",
                                    intents[intent]);
        expect(length > 0 && (size_t)length < sizeof(source), "vector actual fixture is bounded");
        result = f2c_transpile(source, (size_t)length, &options);
        expect(result.code == NULL && result.error_count != 0U,
               "vector-subscripted substring cannot be associated with mutable dummy");
        expect(result.diagnostics != NULL && strstr(result.diagnostics, "vector") != NULL,
               "mutable vector-subscript actual produces a semantic diagnostic");
        f2c_result_free(&result);
    }
}

static void test_target_actual_requires_affine_storage(void) {
    static const char source[] = "program target_actual\n"
                                 "implicit none\n"
                                 "character(8), target :: records(2)\n"
                                 "call retain(records(:)(2:4))\n"
                                 "contains\n"
                                 "subroutine retain(values)\n"
                                 "character(*), target, intent(inout) :: values(:)\n"
                                 "character(:), pointer :: view\n"
                                 "view => values(1)\n"
                                 "view = 'abc'\n"
                                 "end subroutine\n"
                                 "end program\n";
    F2cOptions options = {"target_actual.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code == NULL && result.error_count != 0U,
           "unsupported persistent TARGET view cannot silently become a dangling copy");
    expect(result.diagnostics != NULL && strstr(result.diagnostics, "byte-strided") != NULL,
           "persistent TARGET view reports the remaining descriptor model limitation");
    f2c_result_free(&result);
}

static void test_character_array_allocation_guards(void) {
    static const char source[] = "program character_array_allocation_guards\n"
                                 "  implicit none\n"
                                 "  character(4) :: values(2), adjusted(2)\n"
                                 "  character(-1) :: empty(2)\n"
                                 "  values = [' A  ', '  B ']\n"
                                 "  adjusted = adjustl(values)\n"
                                 "  empty = adjustl(empty)\n"
                                 "end program\n";
    F2cOptions options = {"character_array_allocation_guards.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, sizeof(source) - 1U, &options);
    expect(result.code != NULL && result.error_count == 0U,
           "positive and zero-length character array operations reach code generation");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_size_multiply_checked(f2c_element_count, "
                                   "f2c_element_length)") != NULL,
           "character elemental storage products are checked for overflow");
    expect(result.code != NULL &&
               strstr(result.code, "const size_t f2c_element_bytes = "
                                   "f2c_element_count * f2c_element_length;") != NULL,
           "fortified compilers can relate allocation bytes to iteration bounds");
    expect(result.code != NULL &&
               strstr(result.code, "if (f2c_element_linear >= f2c_element_count) abort();") != NULL,
           "character snapshot writes explicitly respect the allocated element count");
    expect(result.code != NULL &&
               strstr(result.code, "malloc(f2c_element_count == 0U || "
                                   "f2c_element_length == 0U ? 1U : "
                                   "f2c_element_bytes)") != NULL &&
               strstr(result.code, "malloc(f2c_element_bytes == 0U") == NULL,
           "placeholder allocation remains visibly tied to empty shape or character length");
    f2c_result_free(&result);
}

int main(void) {
    test_unit_length_substrings();
    test_type_and_length_diagnostics();
    test_kind_and_value_diagnostics();
    test_keyword_diagnostics();
    test_lexical_comparison_lowering();
    test_array_dynamic_component_constraints();
    test_vector_substring_out_constraints();
    test_target_actual_requires_affine_storage();
    test_character_array_allocation_guards();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
