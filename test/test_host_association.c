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

static void test_scalar_capture_lowering(void) {
    static const char source[] = "program host_capture\n"
                                 "  implicit none(type, external)\n"
                                 "  integer :: counter\n"
                                 "  counter = 3\n"
                                 "  if (bump() /= 4) error stop 1\n"
                                 "contains\n"
                                 "  integer function bump() result(value)\n"
                                 "    counter = counter + 1\n"
                                 "    value = counter\n"
                                 "  end function bump\n"
                                 "end program host_capture\n";
    F2cResult result = transpile(source, "host-capture.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "a scalar host entity completes typed capture lowering");
    expect_contains(result.code, "host_capture__bump(int32_t *counter)",
                    "an internal procedure receives a typed hidden capture parameter");
    expect_contains(result.code, "host_capture__bump(&counter)",
                    "an internal call forwards the host entity by reference");
    f2c_result_free(&result);
}

static void test_automatic_array_portability(void) {
    static const char source[] = "subroutine automatic_extent(limit, total)\n"
                                 "  implicit none(type, external)\n"
                                 "  integer, intent(in) :: limit\n"
                                 "  integer, intent(out) :: total\n"
                                 "  integer :: scratch(limit)\n"
                                 "  scratch = limit\n"
                                 "  total = sum(scratch)\n"
                                 "  if (total < 0) return\n"
                                 "end subroutine automatic_extent\n";
    F2cResult result = transpile(source, "automatic-extent.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "an automatic array completes portable storage lowering");
    expect_contains(result.code, "size_t f2c_auto_extent_scratch_1 = 0U;",
                    "automatic array bounds are captured in entry metadata");
    expect_contains(result.code,
                    "if (!f2c_size_multiply(f2c_auto_count_scratch, "
                    "f2c_auto_extent_scratch_1, &f2c_auto_count_scratch)) abort();",
                    "automatic array element counts reject overflow");
    expect_contains(result.code, "int32_t *scratch = NULL;",
                    "automatic arrays use a C17 pointer instead of a variable-length array");
    expect_contains(result.code, "scratch = (int32_t *)calloc(",
                    "automatic array storage is initialized through a portable allocation");
    expect_contains(result.code, "free(scratch); scratch = NULL;",
                    "automatic array storage is released on every procedure exit");
    expect(result.code == NULL || strstr(result.code, "scratch[F2C_MAX") == NULL,
           "automatic array lowering emits no VLA declaration");
    f2c_result_free(&result);
}

static void test_procedure_pointer_capture_diagnostic(void) {
    static const char source[] = "program host_procedure_pointer\n"
                                 "  implicit none(type, external)\n"
                                 "  abstract interface\n"
                                 "    integer function operation_interface(value) result(answer)\n"
                                 "      integer, intent(in) :: value\n"
                                 "    end function operation_interface\n"
                                 "  end interface\n"
                                 "  procedure(operation_interface), pointer :: operation\n"
                                 "  operation => implementation\n"
                                 "  call invoke()\n"
                                 "contains\n"
                                 "  subroutine invoke()\n"
                                 "    if (operation(41) /= 42) error stop 1\n"
                                 "  end subroutine invoke\n"
                                 "  integer function implementation(value) result(answer)\n"
                                 "    integer, intent(in) :: value\n"
                                 "    answer = value + 1\n"
                                 "  end function implementation\n"
                                 "end program host_procedure_pointer\n";
    F2cResult result = transpile(source, "host-procedure-pointer.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "an unsupported procedure-pointer capture is rejected before code generation");
    expect_contains(result.diagnostics,
                    "host association of procedure pointer 'operation' requires closure-aware "
                    "procedure values",
                    "the procedure-pointer capture diagnostic names the missing ABI capability");
    f2c_result_free(&result);
}

static void test_dynamic_descriptor_capture_lowering(void) {
    static const char source[] = "program host_allocatable\n"
                                 "  implicit none(type, external)\n"
                                 "  integer, allocatable :: values(:), output(:)\n"
                                 "  integer :: count\n"
                                 "  allocate(values(-1:1))\n"
                                 "  call resize()\n"
                                 "  count = combine(resize_and_count(), resize_and_count()) + "
                                 "resize_and_count()\n"
                                 "  output = make_values()\n"
                                 "contains\n"
                                 "  subroutine resize()\n"
                                 "    deallocate(values)\n"
                                 "    allocate(values(2:3))\n"
                                 "    values = [4, 5]\n"
                                 "  end subroutine resize\n"
                                 "  integer function resize_and_count() result(answer)\n"
                                 "    deallocate(values)\n"
                                 "    allocate(values(4:6))\n"
                                 "    values = [7, 8, 9]\n"
                                 "    answer = size(values)\n"
                                 "  end function resize_and_count\n"
                                 "  integer function combine(first, second) result(answer)\n"
                                 "    integer, intent(in) :: first, second\n"
                                 "    answer = first + second\n"
                                 "  end function combine\n"
                                 "  function make_values() result(answer)\n"
                                 "    integer, allocatable :: answer(:)\n"
                                 "    values = values + 1\n"
                                 "    allocate(answer(2))\n"
                                 "    answer = [10, 11]\n"
                                 "  end function make_values\n"
                                 "end program host_allocatable\n";
    F2cResult result = transpile(source, "host-allocatable.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "a local allocatable completes dynamic host-capture lowering");
    expect_contains(result.code, "f2c_descriptor f2c_host_call_descriptor_0 = {",
                    "a statement call materializes a scoped host descriptor");
    expect_contains(result.code, "host_allocatable__resize(&f2c_host_call_descriptor_0);",
                    "the internal subroutine receives the scoped descriptor");
    expect_contains(result.code, "values = (int32_t *)f2c_host_call_descriptor_0.data;",
                    "the statement bridge writes reallocated storage back to the host");
    expect_contains(result.code, "f2c_expression_result_",
                    "a function result is materialized before descriptor writeback");
    expect_contains(result.code, "f2c_ordered_value_",
                    "multiple side-effecting function references are explicitly sequenced");
    expect_contains(result.code, "f2c_ordered_argument_",
                    "side-effecting function arguments are materialized in source order");
    expect_contains(result.code, "f2c_expression_descriptor_result_",
                    "managed allocatable function results have descriptor result storage");
    f2c_result_free(&result);
}

static void test_dynamic_character_and_pointer_capture_lowering(void) {
    static const char source[] = "program host_dynamic_objects\n"
                                 "  implicit none(type, external)\n"
                                 "  integer, target :: first(2), second(3)\n"
                                 "  integer, pointer :: view(:)\n"
                                 "  character(:), allocatable :: text\n"
                                 "  view => first\n"
                                 "  text = 'old'\n"
                                 "  call replace()\n"
                                 "contains\n"
                                 "  subroutine replace()\n"
                                 "    view => second\n"
                                 "    deallocate(text)\n"
                                 "    allocate(character(len=6) :: text)\n"
                                 "    text = 'modern'\n"
                                 "  end subroutine replace\n"
                                 "end program host_dynamic_objects\n";
    F2cResult result = transpile(source, "host-dynamic-objects.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "pointer and deferred CHARACTER host entities complete descriptor lowering");
    expect_contains(result.code, ".deallocatable = view_deallocatable",
                    "pointer allocation provenance enters the capture descriptor");
    expect_contains(result.code, "view_stride_1 = f2c_host_call_descriptor_",
                    "pointer association stride metadata is written back");
    expect_contains(result.code, "f2c_char_len_text = f2c_host_call_descriptor_",
                    "deferred CHARACTER length is written back");
    expect(result.code == NULL || strstr(result.code, ".lower = {}") == NULL,
           "rank-zero descriptor bridges remain strict C17");
    f2c_result_free(&result);
}

static void test_capturing_procedure_value_diagnostic(void) {
    static const char source[] = "program capturing_procedure_value\n"
                                 "  implicit none(type, external)\n"
                                 "  abstract interface\n"
                                 "    integer function operation_interface(value) result(answer)\n"
                                 "      integer, intent(in) :: value\n"
                                 "    end function operation_interface\n"
                                 "  end interface\n"
                                 "  procedure(operation_interface), pointer :: operation\n"
                                 "  integer :: offset\n"
                                 "  offset = 1\n"
                                 "  operation => add_offset\n"
                                 "contains\n"
                                 "  integer function add_offset(value) result(answer)\n"
                                 "    integer, intent(in) :: value\n"
                                 "    answer = value + offset\n"
                                 "  end function add_offset\n"
                                 "end program capturing_procedure_value\n";
    F2cResult result = transpile(source, "capturing-procedure-value.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "an internal procedure with captures cannot escape through a plain function pointer");
    expect_contains(result.diagnostics,
                    "internal procedure 'add_offset' captures host entities and cannot be used "
                    "as a procedure value until closure-aware procedure values are implemented",
                    "the escaping procedure diagnostic identifies the required closure ABI");
    f2c_result_free(&result);
}

int main(void) {
    test_scalar_capture_lowering();
    test_automatic_array_portability();
    test_procedure_pointer_capture_diagnostic();
    test_dynamic_descriptor_capture_lowering();
    test_dynamic_character_and_pointer_capture_lowering();
    test_capturing_procedure_value_diagnostic();
    if (failures != 0) {
        fprintf(stderr, "%d host-association test(s) failed\n", failures);
        return 1;
    }
    puts("host-association tests passed");
    return 0;
}
