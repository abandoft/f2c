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

static void test_explicit_array_result(void) {
    static const char source[] = "module explicit_result_case\n"
                                 "contains\n"
                                 "  function values(n) result(output)\n"
                                 "    integer, intent(in) :: n\n"
                                 "    integer :: output(-1:n-2)\n"
                                 "    integer :: index\n"
                                 "    do index = -1, n - 2\n"
                                 "      output(index) = index\n"
                                 "    end do\n"
                                 "  end function values\n"
                                 "end module explicit_result_case\n"
                                 "program use_explicit_result\n"
                                 "  use explicit_result_case\n"
                                 "  integer :: output(3)\n"
                                 "  output = values(3)\n"
                                 "end program use_explicit_result\n";
    F2cResult result = transpile(source, "explicit-result.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "explicit-shape array results complete typed lowering");
    expect_contains(result.code, "f2c_descriptor f2c_module_explicit_result_case_values(",
                    "array functions expose the unified descriptor result ABI");
    expect_contains(result.code, "f2c_result_descriptor.deallocatable = true;",
                    "explicit-shape result storage transfers ownership to the caller");
    expect_contains(result.code, "f2c_descriptor_bridge_valid(",
                    "callers validate array result descriptors before consuming storage");
    expect_contains(result.code, "free(f2c_array_assignment_function_",
                    "owned result temporaries are released after their value is consumed");
    f2c_result_free(&result);
}

static void test_character_and_derived_results(void) {
    static const char source[] = "module managed_result_case\n"
                                 "  type :: item\n"
                                 "    integer, allocatable :: payload(:)\n"
                                 "  end type item\n"
                                 "contains\n"
                                 "  function words() result(output)\n"
                                 "    character(len=3) :: output(2)\n"
                                 "    output = ['one', 'two']\n"
                                 "  end function words\n"
                                 "  function items() result(output)\n"
                                 "    type(item) :: output(2)\n"
                                 "    allocate(output(1)%payload(1), output(2)%payload(2))\n"
                                 "  end function items\n"
                                 "end module managed_result_case\n"
                                 "program use_managed_result\n"
                                 "  use managed_result_case\n"
                                 "  character(len=4) :: text(2)\n"
                                 "  type(item) :: values(2)\n"
                                 "  text = words()\n"
                                 "  values = items()\n"
                                 "end program use_managed_result\n";
    F2cResult result = transpile(source, "managed-result.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "character and derived array results complete managed lowering");
    expect_contains(result.code, "f2c_result_descriptor.character_length =",
                    "character result descriptors retain their element length");
    expect_contains(result.code, "f2c_clone_f2c_type_managed_result_case_item",
                    "derived result assignment performs ownership-safe deep copies");
    expect_contains(result.code, "f2c_destroy_array_",
                    "derived result temporaries run component destruction");
    f2c_result_free(&result);
}

static void test_pointer_result_view(void) {
    static const char source[] = "module pointer_result_case\n"
                                 "  integer, target :: storage(5) = [1, 2, 3, 4, 5]\n"
                                 "contains\n"
                                 "  function selected() result(output)\n"
                                 "    integer, pointer :: output(:)\n"
                                 "    output => storage(5:1:-2)\n"
                                 "  end function selected\n"
                                 "end module pointer_result_case\n"
                                 "program use_pointer_result\n"
                                 "  use pointer_result_case\n"
                                 "  integer :: output(3)\n"
                                 "  output = selected()\n"
                                 "end program use_pointer_result\n";
    F2cResult result = transpile(source, "pointer-result.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "pointer-valued array results complete descriptor lowering");
    expect_contains(result.code, "f2c_descriptor_stride_step(",
                    "pointer result descriptors retain noncontiguous section strides");
    expect_contains(result.code, "f2c_descriptor_linear_offset(",
                    "nonowning pointer results are copied through descriptor offsets");
    expect_contains(result.code, "f2c_result_descriptor.deallocatable = output_deallocatable;",
                    "pointer result descriptors preserve allocation provenance");
    expect(result.code != NULL && strstr(result.code, "= (int32_t[5])") == NULL,
           "module array initialization uses a strict C17 brace initializer");
    f2c_result_free(&result);
}

static void test_result_contract_mismatch(void) {
    static const char source[] = "program mismatch\n"
                                 "  interface\n"
                                 "    function values() result(output)\n"
                                 "      integer :: output(2)\n"
                                 "    end function values\n"
                                 "  end interface\n"
                                 "  integer :: output(2)\n"
                                 "  output = values()\n"
                                 "end program mismatch\n"
                                 "function values() result(output)\n"
                                 "  integer :: output(3)\n"
                                 "  output = [1, 2, 3]\n"
                                 "end function values\n";
    F2cResult result = transpile(source, "result-contract-mismatch.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "incompatible explicit result contracts are rejected");
    expect_contains(result.diagnostics, "incompatible result type, kind, shape, length",
                    "result contract diagnostics identify shape and ownership attributes");
    f2c_result_free(&result);
}

int main(void) {
    test_explicit_array_result();
    test_character_and_derived_results();
    test_pointer_result_view();
    test_result_contract_mismatch();
    if (failures != 0)
        fprintf(stderr, "%d function-result test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
