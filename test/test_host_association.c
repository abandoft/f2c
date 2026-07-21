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

static void test_dynamic_descriptor_capture_diagnostic(void) {
    static const char source[] = "program host_allocatable\n"
                                 "  implicit none(type, external)\n"
                                 "  integer, allocatable :: values(:)\n"
                                 "  allocate(values(1))\n"
                                 "  call assign_value()\n"
                                 "contains\n"
                                 "  subroutine assign_value()\n"
                                 "    values(1) = 42\n"
                                 "  end subroutine assign_value\n"
                                 "end program host_allocatable\n";
    F2cResult result = transpile(source, "host-allocatable.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "an unsupported local dynamic-descriptor capture is rejected before code generation");
    expect_contains(result.diagnostics,
                    "host association of local dynamic descriptor 'values' is not yet supported",
                    "the allocatable capture diagnostic names the unsupported entity");
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
    test_procedure_pointer_capture_diagnostic();
    test_dynamic_descriptor_capture_diagnostic();
    test_capturing_procedure_value_diagnostic();
    if (failures != 0) {
        fprintf(stderr, "%d host-association test(s) failed\n", failures);
        return 1;
    }
    puts("host-association tests passed");
    return 0;
}
