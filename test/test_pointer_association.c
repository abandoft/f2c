#include "f2c/f2c.h"

#include <stdio.h>
#include <stdlib.h>
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

static void expect_failure(const char *source, const char *diagnostic, const char *message) {
    F2cResult result = transpile(source, "invalid-pointer-association.f90");
    expect(result.code == NULL && result.error_count != 0U, message);
    expect_contains(result.diagnostics, diagnostic, message);
    f2c_result_free(&result);
}

static void test_section_lowering(void) {
    static const char source[] = "program pointer_section\n"
                                 "  implicit none\n"
                                 "  integer, target :: storage(-2:9)\n"
                                 "  integer, pointer :: values(:)\n"
                                 "  integer, pointer :: nested(:)\n"
                                 "  integer, pointer :: element\n"
                                 "  values => storage(0:8:2)\n"
                                 "  nested => values(2:5:2)\n"
                                 "  element => storage(3)\n"
                                 "  nested(1) = element\n"
                                 "end program pointer_section\n";
    F2cResult result = transpile(source, "pointer-section.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "array sections and scalar elements complete the typed pointer-association pipeline");
    expect_contains(result.code, "ptrdiff_t values_stride_1 = 0;",
                    "array pointer storage retains a dynamic stride");
    expect_contains(result.code, "f2c_section_extent(f2c_pointer_first_1",
                    "section extent is computed from single-evaluation temporaries");
    expect_contains(result.code, "f2c_descriptor_stride_step((ptrdiff_t)(values_stride_1)",
                    "pointer-to-pointer sections compose their source and section strides");
    expect_contains(result.code, "element = &(storage[",
                    "a scalar pointer can associate with a target array element");
    expect_contains(result.code, "f2c_array_descriptor_offset(1U",
                    "subsequent pointer element access consumes dynamic descriptor metadata");
    f2c_result_free(&result);
}

static void test_intent_out_initialization(void) {
    static const char source[] = "subroutine clear_pointer(pointer_value)\n"
                                 "  integer, pointer, intent(out) :: pointer_value(:)\n"
                                 "end subroutine clear_pointer\n";
    F2cResult result = transpile(source, "pointer-intent-out.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "an INTENT(OUT) pointer dummy completes semantic lowering");
    expect_contains(result.code, "pointer_value = NULL;",
                    "INTENT(OUT) clears the incoming pointer association on procedure entry");
    expect_contains(result.code,
                    "pointer_value_lower_1 = 1; pointer_value_extent_1 = 0; "
                    "pointer_value_stride_1 = 0;",
                    "INTENT(OUT) clears all incoming pointer descriptor metadata");
    f2c_result_free(&result);
}

static void test_invalid_targets(void) {
    static const char vector_subscript[] = "program vector_target\n"
                                           "  integer, target :: target_value(4)\n"
                                           "  integer, pointer :: pointer_value(:)\n"
                                           "  integer :: indices(2)\n"
                                           "  pointer_value => target_value(indices)\n"
                                           "end program vector_target\n";
    static const char missing_target[] = "program missing_target\n"
                                         "  integer :: value(4)\n"
                                         "  integer, pointer :: pointer_value(:)\n"
                                         "  pointer_value => value(1:4)\n"
                                         "end program missing_target\n";
    static const char rank_mismatch[] = "program rank_mismatch\n"
                                        "  integer, target :: value(2, 2)\n"
                                        "  integer, pointer :: pointer_value(:)\n"
                                        "  pointer_value => value\n"
                                        "end program rank_mismatch\n";
    static const char kind_mismatch[] = "program kind_mismatch\n"
                                        "  integer(kind=8), target :: value(2)\n"
                                        "  integer, pointer :: pointer_value(:)\n"
                                        "  pointer_value => value\n"
                                        "end program kind_mismatch\n";
    static const char character_length_mismatch[] = "program character_length_mismatch\n"
                                                    "  character(len=4), target :: value\n"
                                                    "  character(len=3), pointer :: pointer_value\n"
                                                    "  pointer_value => value\n"
                                                    "end program character_length_mismatch\n";
    expect_failure(vector_subscript, "pointer-assignment target cannot have a vector subscript",
                   "vector-subscribed objects cannot become pointer targets");
    expect_failure(missing_target, "must designate a TARGET, POINTER, ALLOCATABLE object",
                   "an ordinary object cannot become a pointer target");
    expect_failure(rank_mismatch, "incompatible declared type, kind, or rank",
                   "pointer association enforces target rank");
    expect_failure(kind_mismatch, "incompatible declared type, kind, or rank",
                   "pointer association enforces target kind");
    expect_failure(character_length_mismatch, "CHARACTER lengths 3 and 4 are incompatible",
                   "a fixed-length CHARACTER pointer enforces target length");
}

int main(void) {
    test_section_lowering();
    test_intent_out_initialization();
    test_invalid_targets();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
