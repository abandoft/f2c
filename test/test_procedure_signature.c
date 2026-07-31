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

static F2cResult transpile(const char *source, const char *name) {
    F2cOptions options = {name, F2C_SOURCE_FREE, 0};
    return f2c_transpile(source, strlen(source), &options);
}

static void expect_incompatible(const char *source, const char *name, const char *message) {
    F2cResult result = transpile(source, name);
    expect(result.code == NULL && result.error_count != 0U, message);
    expect_contains(result.diagnostics, "has an incompatible explicit interface",
                    "procedure signature mismatch reports the explicit-interface contract");
    f2c_result_free(&result);
}

static void test_parameter_kind_mismatch(void) {
    static const char source[] = "subroutine parameter_kind_mismatch()\n"
                                 "  abstract interface\n"
                                 "    subroutine expected(item)\n"
                                 "      integer(kind=4), intent(in) :: item\n"
                                 "    end subroutine expected\n"
                                 "  end interface\n"
                                 "  call accept(actual)\n"
                                 "contains\n"
                                 "  subroutine accept(operation)\n"
                                 "    procedure(expected) :: operation\n"
                                 "  end subroutine accept\n"
                                 "  subroutine actual(item)\n"
                                 "    integer(kind=8), intent(in) :: item\n"
                                 "  end subroutine actual\n"
                                 "end subroutine parameter_kind_mismatch\n";
    expect_incompatible(source, "procedure-parameter-kind-mismatch.f90",
                        "procedure actuals with a different dummy kind are rejected");
}

static void test_result_kind_mismatch(void) {
    static const char source[] = "subroutine result_kind_mismatch()\n"
                                 "  abstract interface\n"
                                 "    integer(kind=4) function expected() result(answer)\n"
                                 "    end function expected\n"
                                 "  end interface\n"
                                 "  call accept(actual)\n"
                                 "contains\n"
                                 "  subroutine accept(operation)\n"
                                 "    procedure(expected) :: operation\n"
                                 "  end subroutine accept\n"
                                 "  integer(kind=8) function actual() result(answer)\n"
                                 "    answer = 0\n"
                                 "  end function actual\n"
                                 "end subroutine result_kind_mismatch\n";
    expect_incompatible(source, "procedure-result-kind-mismatch.f90",
                        "procedure actuals with a different result kind are rejected");
}

static void test_explicit_shape_mismatch(void) {
    static const char source[] = "subroutine explicit_shape_mismatch()\n"
                                 "  abstract interface\n"
                                 "    subroutine expected(items)\n"
                                 "      integer, intent(in) :: items(2)\n"
                                 "    end subroutine expected\n"
                                 "  end interface\n"
                                 "  call accept(actual)\n"
                                 "contains\n"
                                 "  subroutine accept(operation)\n"
                                 "    procedure(expected) :: operation\n"
                                 "  end subroutine accept\n"
                                 "  subroutine actual(items)\n"
                                 "    integer, intent(in) :: items(3)\n"
                                 "  end subroutine actual\n"
                                 "end subroutine explicit_shape_mismatch\n";
    expect_incompatible(source, "procedure-shape-mismatch.f90",
                        "procedure actuals with a different explicit shape are rejected");
}

static void test_character_length_mismatch(void) {
    static const char source[] = "subroutine character_length_mismatch()\n"
                                 "  abstract interface\n"
                                 "    subroutine expected(text)\n"
                                 "      character(len=4), intent(in) :: text\n"
                                 "    end subroutine expected\n"
                                 "  end interface\n"
                                 "  call accept(actual)\n"
                                 "contains\n"
                                 "  subroutine accept(operation)\n"
                                 "    procedure(expected) :: operation\n"
                                 "  end subroutine accept\n"
                                 "  subroutine actual(text)\n"
                                 "    character(len=8), intent(in) :: text\n"
                                 "  end subroutine actual\n"
                                 "end subroutine character_length_mismatch\n";
    expect_incompatible(source, "procedure-character-length-mismatch.f90",
                        "procedure actuals with a different character length are rejected");
}

static void test_result_ownership_mismatch(void) {
    static const char source[] = "subroutine result_ownership_mismatch()\n"
                                 "  abstract interface\n"
                                 "    function expected() result(items)\n"
                                 "      integer, allocatable :: items(:)\n"
                                 "    end function expected\n"
                                 "  end interface\n"
                                 "  call accept(actual)\n"
                                 "contains\n"
                                 "  subroutine accept(operation)\n"
                                 "    procedure(expected) :: operation\n"
                                 "  end subroutine accept\n"
                                 "  function actual() result(items)\n"
                                 "    integer, pointer :: items(:)\n"
                                 "  end function actual\n"
                                 "end subroutine result_ownership_mismatch\n";
    expect_incompatible(source, "procedure-result-ownership-mismatch.f90",
                        "procedure actuals with different result ownership are rejected");
}

static void test_equivalent_complete_signature(void) {
    static const char source[] = "subroutine equivalent_complete_signature()\n"
                                 "  abstract interface\n"
                                 "    subroutine expected(items, text)\n"
                                 "      integer(kind=8), intent(in) :: items(2)\n"
                                 "      character(len=2+2), intent(in) :: text\n"
                                 "    end subroutine expected\n"
                                 "  end interface\n"
                                 "  call accept(actual)\n"
                                 "contains\n"
                                 "  subroutine accept(operation)\n"
                                 "    procedure(expected) :: operation\n"
                                 "  end subroutine accept\n"
                                 "  subroutine actual(items, text)\n"
                                 "    integer(kind=8), intent(in) :: items(2)\n"
                                 "    character(len=4), intent(in) :: text\n"
                                 "  end subroutine actual\n"
                                 "end subroutine equivalent_complete_signature\n";
    F2cResult result = transpile(source, "procedure-equivalent-complete-signature.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "semantically equivalent complete procedure signatures are accepted");
    f2c_result_free(&result);
}

int main(void) {
    test_parameter_kind_mismatch();
    test_result_kind_mismatch();
    test_explicit_shape_mismatch();
    test_character_length_mismatch();
    test_result_ownership_mismatch();
    test_equivalent_complete_signature();
    if (failures != 0)
        fprintf(stderr, "%d procedure-signature test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
