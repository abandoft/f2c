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

static void expect_override_rejected(const char *source, const char *name, const char *message) {
    F2cResult result = transpile(source, name);
    expect(result.code == NULL && result.error_count != 0U, message);
    expect_contains(result.diagnostics, "has an incompatible overriding interface",
                    "invalid override reports the binding interface contract");
    f2c_result_free(&result);
}

static void test_complete_valid_override(void) {
    static const char source[] =
        "module valid_override\n"
        "  type :: parent\n"
        "  contains\n"
        "    procedure, pass(self) :: describe => parent_describe\n"
        "  end type parent\n"
        "  type, extends(parent) :: child\n"
        "  contains\n"
        "    procedure, pass(self) :: describe => child_describe\n"
        "  end type child\n"
        "contains\n"
        "  function parent_describe(self, values, text) result(answer)\n"
        "    class(parent), intent(in) :: self\n"
        "    integer(kind=8), intent(in) :: values(2)\n"
        "    character(len=2+2), intent(in) :: text\n"
        "    character(len=4) :: answer\n"
        "    answer = text\n"
        "  end function parent_describe\n"
        "  pure function child_describe(self, values, text) result(answer)\n"
        "    class(child), intent(in) :: self\n"
        "    integer(kind=8), intent(in) :: values(2)\n"
        "    character(len=4), intent(in) :: text\n"
        "    character(len=2+2) :: answer\n"
        "    answer = text\n"
        "  end function child_describe\n"
        "end module valid_override\n";
    F2cResult result = transpile(source, "type-binding-valid-override.f90");
    expect(result.error_count == 0U && result.code != NULL,
           "a pure child may override an impure parent with otherwise identical characteristics");
    f2c_result_free(&result);
}

static void test_dummy_name_mismatch(void) {
    static const char source[] = "module dummy_name_mismatch\n"
                                 "  type :: parent\n"
                                 "  contains\n"
                                 "    procedure :: update => parent_update\n"
                                 "  end type parent\n"
                                 "  type, extends(parent) :: child\n"
                                 "  contains\n"
                                 "    procedure :: update => child_update\n"
                                 "  end type child\n"
                                 "contains\n"
                                 "  subroutine parent_update(self, value)\n"
                                 "    class(parent), intent(inout) :: self\n"
                                 "    integer, intent(in) :: value\n"
                                 "  end subroutine parent_update\n"
                                 "  subroutine child_update(self, replacement)\n"
                                 "    class(child), intent(inout) :: self\n"
                                 "    integer, intent(in) :: replacement\n"
                                 "  end subroutine child_update\n"
                                 "end module dummy_name_mismatch\n";
    expect_override_rejected(source, "type-binding-dummy-name-mismatch.f90",
                             "overrides with different dummy names are rejected");
}

static void test_shape_and_character_mismatch(void) {
    static const char shape_source[] = "module shape_mismatch\n"
                                       "  type :: parent\n"
                                       "  contains\n"
                                       "    procedure :: update => parent_update\n"
                                       "  end type parent\n"
                                       "  type, extends(parent) :: child\n"
                                       "  contains\n"
                                       "    procedure :: update => child_update\n"
                                       "  end type child\n"
                                       "contains\n"
                                       "  subroutine parent_update(self, values)\n"
                                       "    class(parent), intent(inout) :: self\n"
                                       "    integer, intent(in) :: values(2)\n"
                                       "  end subroutine parent_update\n"
                                       "  subroutine child_update(self, values)\n"
                                       "    class(child), intent(inout) :: self\n"
                                       "    integer, intent(in) :: values(3)\n"
                                       "  end subroutine child_update\n"
                                       "end module shape_mismatch\n";
    static const char character_source[] = "module character_mismatch\n"
                                           "  type :: parent\n"
                                           "  contains\n"
                                           "    procedure :: update => parent_update\n"
                                           "  end type parent\n"
                                           "  type, extends(parent) :: child\n"
                                           "  contains\n"
                                           "    procedure :: update => child_update\n"
                                           "  end type child\n"
                                           "contains\n"
                                           "  subroutine parent_update(self, text)\n"
                                           "    class(parent), intent(inout) :: self\n"
                                           "    character(len=4), intent(in) :: text\n"
                                           "  end subroutine parent_update\n"
                                           "  subroutine child_update(self, text)\n"
                                           "    class(child), intent(inout) :: self\n"
                                           "    character(len=8), intent(in) :: text\n"
                                           "  end subroutine child_update\n"
                                           "end module character_mismatch\n";
    expect_override_rejected(shape_source, "type-binding-shape-mismatch.f90",
                             "overrides with a different explicit shape are rejected");
    expect_override_rejected(character_source, "type-binding-character-mismatch.f90",
                             "overrides with a different character length are rejected");
}

static void test_result_ownership_mismatch(void) {
    static const char source[] = "module result_ownership_mismatch\n"
                                 "  type :: parent\n"
                                 "  contains\n"
                                 "    procedure :: values => parent_values\n"
                                 "  end type parent\n"
                                 "  type, extends(parent) :: child\n"
                                 "  contains\n"
                                 "    procedure :: values => child_values\n"
                                 "  end type child\n"
                                 "contains\n"
                                 "  function parent_values(self) result(output)\n"
                                 "    class(parent), intent(in) :: self\n"
                                 "    integer, allocatable :: output(:)\n"
                                 "  end function parent_values\n"
                                 "  function child_values(self) result(output)\n"
                                 "    class(child), intent(in) :: self\n"
                                 "    integer, pointer :: output(:)\n"
                                 "  end function child_values\n"
                                 "end module result_ownership_mismatch\n";
    expect_override_rejected(source, "type-binding-result-ownership-mismatch.f90",
                             "overrides with different function-result ownership are rejected");
}

static void test_purity_and_elemental_mismatch(void) {
    static const char pure_source[] = "module pure_mismatch\n"
                                      "  type :: parent\n"
                                      "  contains\n"
                                      "    procedure :: update => parent_update\n"
                                      "  end type parent\n"
                                      "  type, extends(parent) :: child\n"
                                      "  contains\n"
                                      "    procedure :: update => child_update\n"
                                      "  end type child\n"
                                      "contains\n"
                                      "  pure subroutine parent_update(self)\n"
                                      "    class(parent), intent(in) :: self\n"
                                      "  end subroutine parent_update\n"
                                      "  subroutine child_update(self)\n"
                                      "    class(child), intent(in) :: self\n"
                                      "  end subroutine child_update\n"
                                      "end module pure_mismatch\n";
    static const char elemental_source[] = "module elemental_mismatch\n"
                                           "  type :: parent\n"
                                           "  contains\n"
                                           "    procedure :: update => parent_update\n"
                                           "  end type parent\n"
                                           "  type, extends(parent) :: child\n"
                                           "  contains\n"
                                           "    procedure :: update => child_update\n"
                                           "  end type child\n"
                                           "contains\n"
                                           "  elemental subroutine parent_update(self)\n"
                                           "    class(parent), intent(in) :: self\n"
                                           "  end subroutine parent_update\n"
                                           "  subroutine child_update(self)\n"
                                           "    class(child), intent(in) :: self\n"
                                           "  end subroutine child_update\n"
                                           "end module elemental_mismatch\n";
    expect_override_rejected(pure_source, "type-binding-pure-mismatch.f90",
                             "an impure binding cannot override a pure binding");
    expect_override_rejected(elemental_source, "type-binding-elemental-mismatch.f90",
                             "ELEMENTAL and nonelemental bindings cannot override each other");
}

static void test_passed_object_contract(void) {
    static const char source[] = "module passed_object_mismatch\n"
                                 "  type :: parent\n"
                                 "  contains\n"
                                 "    procedure :: update => parent_update\n"
                                 "  end type parent\n"
                                 "  type, extends(parent) :: child\n"
                                 "  contains\n"
                                 "    procedure :: update => child_update\n"
                                 "  end type child\n"
                                 "contains\n"
                                 "  subroutine parent_update(self)\n"
                                 "    class(parent), intent(inout) :: self\n"
                                 "  end subroutine parent_update\n"
                                 "  subroutine child_update(self)\n"
                                 "    class(parent), intent(inout) :: self\n"
                                 "  end subroutine child_update\n"
                                 "end module passed_object_mismatch\n";
    F2cResult result = transpile(source, "type-binding-passed-object-mismatch.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "an overriding passed-object dummy must use the extending declared type");
    expect_contains(result.diagnostics,
                    "must be a scalar, nonpointer, nonallocatable CLASS('child')",
                    "passed-object diagnostics state the complete required contract");
    f2c_result_free(&result);
}

static void test_access_and_deferred_constraints(void) {
    static const char access_source[] = "module access_mismatch\n"
                                        "  type :: parent\n"
                                        "  contains\n"
                                        "    procedure, public :: update => parent_update\n"
                                        "  end type parent\n"
                                        "  type, extends(parent) :: child\n"
                                        "  contains\n"
                                        "    procedure, private :: update => child_update\n"
                                        "  end type child\n"
                                        "contains\n"
                                        "  subroutine parent_update(self)\n"
                                        "    class(parent), intent(inout) :: self\n"
                                        "  end subroutine parent_update\n"
                                        "  subroutine child_update(self)\n"
                                        "    class(child), intent(inout) :: self\n"
                                        "  end subroutine child_update\n"
                                        "end module access_mismatch\n";
    static const char default_access_source[] = "module default_access_mismatch\n"
                                                "  type :: parent\n"
                                                "  contains\n"
                                                "    procedure, public :: update => parent_update\n"
                                                "  end type parent\n"
                                                "  type, extends(parent) :: child\n"
                                                "  contains\n"
                                                "    private\n"
                                                "    procedure :: update => child_update\n"
                                                "  end type child\n"
                                                "contains\n"
                                                "  subroutine parent_update(self)\n"
                                                "    class(parent), intent(inout) :: self\n"
                                                "  end subroutine parent_update\n"
                                                "  subroutine child_update(self)\n"
                                                "    class(child), intent(inout) :: self\n"
                                                "  end subroutine child_update\n"
                                                "end module default_access_mismatch\n";
    static const char deferred_source[] =
        "module deferred_mismatch\n"
        "  type :: parent\n"
        "  contains\n"
        "    procedure, nopass :: update => parent_update\n"
        "  end type parent\n"
        "  abstract interface\n"
        "    subroutine child_update_interface()\n"
        "    end subroutine child_update_interface\n"
        "  end interface\n"
        "  type, abstract, extends(parent) :: child\n"
        "  contains\n"
        "    procedure(child_update_interface), deferred, nopass :: update\n"
        "  end type child\n"
        "contains\n"
        "  subroutine parent_update()\n"
        "  end subroutine parent_update\n"
        "end module deferred_mismatch\n";
    expect_override_rejected(access_source, "type-binding-access-mismatch.f90",
                             "a private binding cannot override a public binding");
    expect_override_rejected(default_access_source, "type-binding-default-access-mismatch.f90",
                             "the type-bound PRIVATE statement applies to following bindings");
    expect_override_rejected(deferred_source, "type-binding-deferred-mismatch.f90",
                             "a deferred binding cannot override a concrete binding");
}

static void test_binding_attribute_diagnostics(void) {
    static const char duplicate_source[] = "module duplicate_attribute\n"
                                           "  type :: item\n"
                                           "  contains\n"
                                           "    procedure, nopass, nopass :: update\n"
                                           "  end type item\n"
                                           "contains\n"
                                           "  subroutine update()\n"
                                           "  end subroutine update\n"
                                           "end module duplicate_attribute\n";
    static const char conflict_source[] =
        "module conflicting_attribute\n"
        "  type, abstract :: item\n"
        "  contains\n"
        "    procedure(update_interface), deferred, non_overridable :: update\n"
        "  end type item\n"
        "  abstract interface\n"
        "    subroutine update_interface(self)\n"
        "      import :: item\n"
        "      class(item), intent(inout) :: self\n"
        "    end subroutine update_interface\n"
        "  end interface\n"
        "end module conflicting_attribute\n";
    F2cResult result = transpile(duplicate_source, "type-binding-duplicate-attribute.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "duplicate binding attributes are rejected");
    expect_contains(result.diagnostics, "duplicate type-bound PROCEDURE attribute 'nopass'",
                    "duplicate binding attributes have a precise diagnostic");
    f2c_result_free(&result);
    result = transpile(conflict_source, "type-binding-conflicting-attribute.f90");
    expect(result.code == NULL && result.error_count != 0U,
           "conflicting binding attributes are rejected");
    expect_contains(result.diagnostics,
                    "conflicting type-bound PROCEDURE attribute 'non_overridable'",
                    "conflicting binding attributes have a precise diagnostic");
    f2c_result_free(&result);
}

int main(void) {
    test_complete_valid_override();
    test_dummy_name_mismatch();
    test_shape_and_character_mismatch();
    test_result_ownership_mismatch();
    test_purity_and_elemental_mismatch();
    test_passed_object_contract();
    test_access_and_deferred_constraints();
    test_binding_attribute_diagnostics();
    if (failures != 0)
        fprintf(stderr, "%d type-binding test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
