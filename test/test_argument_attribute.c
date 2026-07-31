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

static F2cResult transpile(const char *source) {
    F2cOptions options = {"argument_attribute.f90", F2C_SOURCE_FREE, 0};
    F2cInput input = {source, strlen(source), options};
    return f2c_transpile_project(&input, 1U);
}

static void expect_failure(const char *source, const char *fragment, const char *message) {
    F2cResult result = transpile(source);
    expect(result.code == NULL && result.error_count != 0U, message);
    expect(result.diagnostics != NULL && strstr(result.diagnostics, fragment) != NULL,
           "attribute rejection includes an actionable diagnostic");
    f2c_result_free(&result);
}

static void test_value_lowering(void) {
    static const char source[] = "module value_support\n"
                                 "  implicit none\n"
                                 "  type :: box\n"
                                 "    integer, allocatable :: data(:)\n"
                                 "  end type box\n"
                                 "contains\n"
                                 "  subroutine consume(number, text, object)\n"
                                 "    integer, value, optional :: number\n"
                                 "    character(4), value :: text\n"
                                 "    type(box), value :: object\n"
                                 "    if (present(number)) number = number + 1\n"
                                 "    text = 'x'\n"
                                 "    object%data(1) = 2\n"
                                 "  end subroutine consume\n"
                                 "end module value_support\n";
    F2cResult result = transpile(source);
    expect(result.code != NULL && result.error_count == 0U,
           "scalar intrinsic, CHARACTER, derived, and OPTIONAL VALUE dummies are accepted");
    expect(result.code != NULL && strstr(result.code, "f2c_value_storage_number") != NULL &&
               strstr(result.code, "f2c_value_argument_number != NULL") != NULL,
           "intrinsic VALUE dummies receive guarded local storage");
    expect(result.code != NULL && strstr(result.code, "f2c_value_length_text") != NULL &&
               strstr(result.code, "f2c_value_copy_text") != NULL,
           "CHARACTER VALUE dummies are copied with declared-length padding and truncation");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_clone_f2c_type_value_support_box") != NULL &&
               strstr(result.code, "f2c_destroy_f2c_type_value_support_box(object)") != NULL,
           "derived VALUE dummies use deep-copy ownership and deterministic destruction");
    f2c_result_free(&result);
}

static void test_standalone_attributes(void) {
    static const char source[] = "subroutine standalone(number, observable, stable)\n"
                                 "  implicit none\n"
                                 "  integer :: number, observable, stable\n"
                                 "  integer :: values\n"
                                 "  value :: number\n"
                                 "  target :: number\n"
                                 "  target :: values(4)\n"
                                 "  volatile :: observable\n"
                                 "  asynchronous :: stable\n"
                                 "  number = number + 1\n"
                                 "  observable = observable + 1\n"
                                 "  stable = stable + 1\n"
                                 "  values(1) = number\n"
                                 "end subroutine standalone\n";
    F2cResult result = transpile(source);
    expect(result.code != NULL && result.error_count == 0U,
           "standalone VALUE, TARGET, VOLATILE, and ASYNCHRONOUS statements are accepted");
    expect(result.code != NULL && strstr(result.code, "volatile int32_t *observable") != NULL,
           "VOLATILE dummy storage is represented by a volatile-qualified C pointer");
    expect(result.code != NULL && strstr(result.code, "F2C_RESTRICT stable") == NULL &&
               strstr(result.code, "F2C_RESTRICT observable") == NULL,
           "observable or alias-capable dummies are excluded from restrict-based optimization");
    f2c_result_free(&result);
}

static void test_invalid_combinations(void) {
    expect_failure("program p\ninteger, value :: x\nend\n", "must be a dummy argument",
                   "VALUE is rejected on a local variable");
    expect_failure("subroutine s(x)\ninteger, value :: x(:)\nend\n", "must be scalar",
                   "VALUE is rejected on an array dummy");
    expect_failure("subroutine s(x)\ninteger, value, allocatable :: x\nend\n",
                   "cannot be ALLOCATABLE or POINTER", "VALUE is rejected on an allocatable dummy");
    expect_failure("subroutine s(x)\ninteger, value, pointer :: x\nend\n",
                   "cannot be ALLOCATABLE or POINTER", "VALUE is rejected on a pointer dummy");
    expect_failure("subroutine s(x)\ninteger, value, intent(out) :: x\nend\n",
                   "cannot have INTENT(OUT)", "VALUE is rejected with INTENT(OUT)");
    expect_failure("subroutine s(x)\ninteger, value, volatile :: x\nend\n",
                   "cannot have the VOLATILE", "VALUE is rejected with VOLATILE");
    expect_failure("subroutine s(x)\ncharacter(*), value :: x\nend\n",
                   "must have a constant length",
                   "assumed-length CHARACTER VALUE is rejected before code generation");
    expect_failure("subroutine s(x)\ninteger, target, pointer :: x\nend\n",
                   "both POINTER and TARGET", "TARGET is rejected with POINTER");
    expect_failure("subroutine s(x)\ninteger, volatile, intent(in) :: x\nend\n",
                   "cannot have INTENT(IN)", "VOLATILE is rejected with INTENT(IN)");
    expect_failure("program p\ninteger, parameter, volatile :: x=1\nend\n",
                   "cannot be ASYNCHRONOUS or VOLATILE",
                   "VOLATILE is rejected on a named constant");
    expect_failure("module m\ntype t\ninteger, value :: x\nend type\nend module\n",
                   "derived-type component",
                   "dummy-only attributes are rejected on derived components");
    expect_failure("subroutine s(x)\ninteger :: x\nvalue :: x\nvalue :: x\nend\n",
                   "duplicate VALUE", "duplicate standalone attributes are rejected");
}

static void test_signature_characteristics(void) {
    static const char *const attributes[] = {"value", "target", "asynchronous", "volatile"};
    size_t attribute;
    for (attribute = 0U; attribute < sizeof(attributes) / sizeof(attributes[0]); ++attribute) {
        char caller[512];
        static const char definition[] = "subroutine consume(item)\n"
                                         "  integer :: item\n"
                                         "end subroutine consume\n";
        F2cInput inputs[2];
        F2cResult result;
        (void)snprintf(caller, sizeof(caller),
                       "program caller\n"
                       "  interface\n"
                       "    subroutine consume(item)\n"
                       "      integer, %s :: item\n"
                       "    end subroutine consume\n"
                       "  end interface\n"
                       "  integer :: item\n"
                       "  call consume(item)\n"
                       "end program caller\n",
                       attributes[attribute]);
        inputs[0] =
            (F2cInput){caller, strlen(caller), {"attribute_caller.f90", F2C_SOURCE_FREE, 0}};
        inputs[1] = (F2cInput){
            definition, sizeof(definition) - 1U, {"attribute_definition.f90", F2C_SOURCE_FREE, 0}};
        result = f2c_transpile_project(inputs, 2U);
        expect(result.code == NULL && result.error_count != 0U,
               "explicit interfaces reject missing argument characteristics");
        expect(result.diagnostics != NULL && strstr(result.diagnostics, "incompatible") != NULL,
               "argument characteristic mismatch identifies the incompatible interface");
        f2c_result_free(&result);
    }
}

int main(void) {
    test_value_lowering();
    test_standalone_attributes();
    test_invalid_combinations();
    test_signature_characteristics();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
