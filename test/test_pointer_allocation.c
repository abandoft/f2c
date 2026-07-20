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
    F2cResult result = transpile(source, "invalid-pointer-allocation.f90");
    expect(result.code == NULL && result.error_count != 0U, message);
    expect_contains(result.diagnostics, diagnostic, message);
    f2c_result_free(&result);
}

static void test_allocation_lowering(void) {
    static const char source[] = "module pointer_storage\n"
                                 "  type :: box\n"
                                 "    integer, pointer :: values(:)\n"
                                 "  end type box\n"
                                 "contains\n"
                                 "  subroutine resize(values, status, message)\n"
                                 "    integer, pointer, intent(inout) :: values(:)\n"
                                 "    integer, intent(out) :: status\n"
                                 "    character(len=*), intent(out) :: message\n"
                                 "    allocate(values(-2:2), stat=status, errmsg=message)\n"
                                 "  end subroutine resize\n"
                                 "end module pointer_storage\n"
                                 "program pointer_owner\n"
                                 "  use pointer_storage\n"
                                 "  integer, pointer :: values(:), alias(:)\n"
                                 "  type(box) :: object\n"
                                 "  integer :: status\n"
                                 "  character(len=32) :: message\n"
                                 "  call resize(values, status, message)\n"
                                 "  alias => values\n"
                                 "  deallocate(alias, stat=status, errmsg=message)\n"
                                 "  allocate(object%values(3))\n"
                                 "  deallocate(object%values)\n"
                                 "end program pointer_owner\n";
    F2cResult result = transpile(source, "pointer-allocation.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "POINTER allocation completes semantic analysis and C17 lowering");
    expect_contains(result.code, "bool values_deallocatable = false;",
                    "a data pointer separately tracks whether its target can be deallocated");
    expect_contains(result.code, "bool f2c_alloc_ok = true;",
                    "allocating an associated pointer creates a new target instead of failing");
    expect_contains(result.code, "values_deallocatable = true;",
                    "successful pointer allocation records a deallocatable target");
    expect_contains(result.code, "values_stride_1 = 1;",
                    "pointer allocation initializes a contiguous descriptor stride");
    expect_contains(result.code, "f2c_descriptor_values->deallocatable = values_deallocatable;",
                    "pointer dummy descriptors return target ownership state to the caller");
    expect_contains(result.code, "alias_deallocatable = values_deallocatable;",
                    "whole-target pointer aliases inherit deallocation capability");
    expect_contains(result.code, "f2c_allocation_object->values_deallocatable",
                    "derived pointer components retain deallocation metadata");
    expect_contains(result.code, "f2c_store_message(message, (size_t)(32)",
                    "allocation failure paths populate a Fortran ERRMSG variable");
    f2c_result_free(&result);
}

static void test_nonallocated_targets_are_not_freed(void) {
    static const char source[] = "program target_release\n"
                                 "  integer, target :: ordinary\n"
                                 "  integer, allocatable, target :: dynamic\n"
                                 "  integer, pointer :: pointer_value\n"
                                 "  integer :: status\n"
                                 "  character(len=32) :: message\n"
                                 "  pointer_value => ordinary\n"
                                 "  deallocate(pointer_value, stat=status, errmsg=message)\n"
                                 "  allocate(dynamic)\n"
                                 "  pointer_value => dynamic\n"
                                 "  deallocate(pointer_value, stat=status, errmsg=message)\n"
                                 "end program target_release\n";
    F2cResult result = transpile(source, "pointer-release.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "invalid pointer target deallocation remains a runtime error condition");
    expect_contains(result.code, "pointer_value_deallocatable = false;",
                    "association with an ordinary TARGET cannot transfer allocation ownership");
    expect_contains(result.code, "pointer_value != NULL && pointer_value_deallocatable",
                    "DEALLOCATE checks target provenance before calling free");
    f2c_result_free(&result);
}

static void test_invalid_controls(void) {
    static const char missing_stat[] = "program missing_stat\n"
                                       "  integer, pointer :: value\n"
                                       "  character(len=16) :: message\n"
                                       "  allocate(value, errmsg=message)\n"
                                       "end program missing_stat\n";
    static const char invalid_message[] = "program invalid_message\n"
                                          "  integer, pointer :: value\n"
                                          "  integer :: status, message\n"
                                          "  allocate(value, stat=status, errmsg=message)\n"
                                          "end program invalid_message\n";
    static const char ordinary_object[] = "program ordinary_object\n"
                                          "  integer :: value\n"
                                          "  allocate(value)\n"
                                          "end program ordinary_object\n";
    expect_failure(missing_stat, "ERRMSG= in ALLOCATE requires STAT=",
                   "ERRMSG without STAT is rejected before code generation");
    expect_failure(invalid_message, "ERRMSG= in ALLOCATE must be a definable scalar CHARACTER",
                   "ERRMSG enforces scalar default-character storage");
    expect_failure(ordinary_object, "is neither ALLOCATABLE nor POINTER",
                   "ALLOCATE still rejects ordinary data objects");
}

int main(void) {
    test_allocation_lowering();
    test_nonallocated_targets_are_not_freed();
    test_invalid_controls();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
