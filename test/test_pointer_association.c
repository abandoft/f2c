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
                                 "  integer, pointer :: remapped(:, :)\n"
                                 "  integer, pointer :: element\n"
                                 "  values(0:) => storage(0:8:2)\n"
                                 "  if (.not. associated(pointer=values, "
                                 "target=storage(0:8:2))) stop 1\n"
                                 "  nested => values(2:5:2)\n"
                                 "  remapped(0:2, -1:2) => storage\n"
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
    expect_contains(result.code, "const int64_t f2c_pointer_bound_lower_1",
                    "pointer lower bounds are captured in single-evaluation temporaries");
    expect_contains(result.code, "f2c_default_integer_bounds(",
                    "pointer metadata uses a warning-free checked narrowing boundary");
    expect_contains(result.code, "size_t f2c_pointer_remap_count = 1U;",
                    "rank remapping validates the requested pointer element count");
    expect_contains(result.code, "f2c_pointer_target_count < f2c_pointer_remap_count",
                    "rank remapping rejects targets that are too small");
    expect_contains(result.code, "ptrdiff_t f2c_pointer_remap_stride",
                    "rank remapping creates a column-major descriptor stride chain");
    expect_contains(result.code, "f2c_associated_array_target((const void *)(values)",
                    "ASSOCIATED lowers target sections through descriptor identity checks");
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

static void test_array_pointer_component_lowering(void) {
    static const char source[] =
        "program component_pointer\n"
        "  implicit none\n"
        "  type :: box\n"
        "    integer, pointer :: values(:)\n"
        "    integer, pointer :: matrix(:, :)\n"
        "  end type box\n"
        "  type(box) :: object, source\n"
        "  integer, target :: storage(-2:9), grid(2,3)\n"
        "  object%values(4:) => storage(0:6:2)\n"
        "  source%values(-1:) => storage(-1:4)\n"
        "  object%values => source%values(0:3:2)\n"
        "  if (.not. associated(object%values, source%values(0:3:2))) stop 1\n"
        "  call consume(object%values)\n"
        "  call clear(object%values)\n"
        "  object%values => source%values\n"
        "  object%matrix(0:1,-1:1) => grid\n"
        "  object%values = [1, 2]\n"
        "  nullify(object%matrix)\n"
        "contains\n"
        "  subroutine consume(values)\n"
        "    integer, intent(in) :: values(:)\n"
        "  end subroutine consume\n"
        "  subroutine clear(values)\n"
        "    integer, pointer, intent(inout) :: values(:)\n"
        "    nullify(values)\n"
        "  end subroutine clear\n"
        "end program component_pointer\n";
    F2cResult result = transpile(source, "component-pointer.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "array POINTER components complete semantic analysis and C17 lowering");
    expect_contains(result.code, "(object).values_lower_1 = (int32_t)f2c_pointer_bound_lower_1",
                    "component pointer bounds update the owning object's descriptor metadata");
    expect_contains(result.code, "(source).values_stride_1)}, (const bool[]){true}",
                    "ASSOCIATED compares component target sections with their dynamic stride");
    expect_contains(result.code, ".deallocatable = (object).values_deallocatable",
                    "component array actuals preserve pointer provenance in call descriptors");
    expect_contains(result.code, "(object).values = (int32_t *)f2c_call_descriptor_0.data;",
                    "pointer dummy calls return association changes to component storage");
    expect_contains(result.code, "f2c_component_values[f2c_component_linear++]",
                    "whole component array assignment uses overlap-safe element storage");
    expect_contains(result.code,
                    "(object).matrix_lower_2 = 1; (object).matrix_extent_2 = 0; "
                    "(object).matrix_stride_2 = 0;",
                    "NULLIFY clears every component descriptor dimension");
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
    static const char missing_lower_bound[] = "program missing_lower_bound\n"
                                              "  integer, target :: value(4)\n"
                                              "  integer, pointer :: pointer_value(:)\n"
                                              "  pointer_value(:) => value\n"
                                              "end program missing_lower_bound\n";
    static const char mixed_bounds[] = "program mixed_bounds\n"
                                       "  integer, target :: value(4)\n"
                                       "  integer, pointer :: pointer_value(:, :)\n"
                                       "  pointer_value(1:, 1:2) => value\n"
                                       "end program mixed_bounds\n";
    static const char pointer_stride[] = "program pointer_stride\n"
                                         "  integer, target :: value(4)\n"
                                         "  integer, pointer :: pointer_value(:)\n"
                                         "  pointer_value(1:4:2) => value\n"
                                         "end program pointer_stride\n";
    static const char null_with_bounds[] = "program null_with_bounds\n"
                                           "  integer, pointer :: pointer_value(:)\n"
                                           "  pointer_value(1:) => null()\n"
                                           "end program null_with_bounds\n";
    static const char scalar_remap_target[] = "program scalar_remap_target\n"
                                              "  integer, target :: value\n"
                                              "  integer, pointer :: pointer_value(:)\n"
                                              "  pointer_value(1:1) => value\n"
                                              "end program scalar_remap_target\n";
    static const char allocatable_without_target[] = "program allocatable_without_target\n"
                                                     "  integer, allocatable :: value(:)\n"
                                                     "  integer, pointer :: pointer_value(:)\n"
                                                     "  allocate(value(4))\n"
                                                     "  pointer_value => value\n"
                                                     "end program allocatable_without_target\n";
    static const char associated_vector_target[] = "program associated_vector_target\n"
                                                   "  integer, target :: value(4)\n"
                                                   "  integer, pointer :: pointer_value(:)\n"
                                                   "  integer :: indices(2)\n"
                                                   "  pointer_value => value(1:2)\n"
                                                   "  if (associated(pointer_value, "
                                                   "value(indices))) stop 1\n"
                                                   "end program associated_vector_target\n";
    static const char associated_rank_mismatch[] =
        "program associated_rank_mismatch\n"
        "  integer, target :: value(2, 2)\n"
        "  integer, pointer :: pointer_value(:)\n"
        "  if (associated(pointer_value, value)) stop 1\n"
        "end program associated_rank_mismatch\n";
    expect_failure(vector_subscript, "pointer-assignment target cannot have a vector subscript",
                   "vector-subscribed objects cannot become pointer targets");
    expect_failure(missing_target, "must designate a TARGET or POINTER object",
                   "an ordinary object cannot become a pointer target");
    expect_failure(rank_mismatch, "incompatible declared type, kind, or rank",
                   "pointer association enforces target rank");
    expect_failure(kind_mismatch, "incompatible declared type, kind, or rank",
                   "pointer association enforces target kind");
    expect_failure(character_length_mismatch, "CHARACTER lengths 3 and 4 are incompatible",
                   "a fixed-length CHARACTER pointer enforces target length");
    expect_failure(missing_lower_bound, "pointer lower bound must be a scalar INTEGER",
                   "a pointer bounds specification requires every lower bound");
    expect_failure(mixed_bounds, "cannot mix lower-bound specifications with bounds remapping",
                   "a pointer assignment cannot mix its two bounds forms");
    expect_failure(pointer_stride, "pointer bounds cannot specify a stride",
                   "pointer bounds do not accept array-section strides");
    expect_failure(null_with_bounds, "pointer bounds cannot be used when assigning NULL()",
                   "NULL pointer assignment rejects bounds syntax");
    expect_failure(scalar_remap_target, "rank-remapped pointer target must be an array",
                   "rank remapping requires an array target");
    expect_failure(allocatable_without_target, "must designate a TARGET or POINTER object",
                   "ALLOCATABLE alone does not satisfy pointer-target requirements");
    expect_failure(associated_vector_target, "ASSOCIATED target cannot have a vector subscript",
                   "ASSOCIATED rejects vector-subscribed target designators");
    expect_failure(associated_rank_mismatch,
                   "ASSOCIATED target must be a compatible TARGET or POINTER object",
                   "ASSOCIATED enforces target rank compatibility");
}

int main(void) {
    test_section_lowering();
    test_intent_out_initialization();
    test_array_pointer_component_lowering();
    test_invalid_targets();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
