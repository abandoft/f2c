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
    F2cResult result = transpile(source, "invalid-allocation-model.f90");
    expect(result.code == NULL && result.error_count != 0U, message);
    expect_contains(result.diagnostics, diagnostic, message);
    f2c_result_free(&result);
}

static void test_expression_model_lowering(void) {
    static const char source[] = "program allocation_expression\n"
                                 "  integer, allocatable :: a(:), b(:), c(:), d(:)\n"
                                 "  integer :: values(5)\n"
                                 "  values = [1, 2, 3, 4, 5]\n"
                                 "  allocate(a, b, source=values + 1)\n"
                                 "  allocate(c, source=values(5:1:-2))\n"
                                 "  allocate(d, source=pack(values, values > 2))\n"
                                 "end program allocation_expression\n";
    F2cResult result = transpile(source, "allocation-expression.f90");
    expect(result.code != NULL && result.error_count == 0U,
           "array expressions and multiple allocation objects lower through typed IR");
    expect_contains(result.code, "f2c_transform_allocate_model_values",
                    "elemental and section SOURCE values use a statement-owned materialization");
    expect_contains(result.code, "f2c_allocate_model_4_extent_1",
                    "the allocation model snapshots shape before allocating any target");
    expect_contains(result.code, "bool f2c_alloc_statement_ok",
                    "multiple targets share one transactional statement state");
    expect(result.code == NULL ||
               strstr(result.code, "currently requires a whole named array") == NULL,
           "no legacy whole-name restriction leaks into generated C");
    f2c_result_free(&result);
}

static void test_scalar_single_evaluation(void) {
    static const char source[] = "program allocation_scalar\n"
                                 "  integer, allocatable :: a(:), b(:)\n"
                                 "  integer :: calls\n"
                                 "  calls = 0\n"
                                 "  allocate(a(2), b(3), source=next_value())\n"
                                 "contains\n"
                                 "  integer function next_value()\n"
                                 "    calls = calls + 1\n"
                                 "    next_value = 7\n"
                                 "  end function next_value\n"
                                 "end program allocation_scalar\n";
    F2cResult result = transpile(source, "allocation-scalar.f90");
    const char *call;
    expect(result.code != NULL && result.error_count == 0U,
           "a scalar SOURCE may initialize multiple explicitly shaped targets");
    call =
        result.code != NULL ? strstr(result.code, "allocation_scalar__next_value(&calls)") : NULL;
    expect(call != NULL && strstr(call + 1, "allocation_scalar__next_value(&calls)") == NULL,
           "a scalar SOURCE expression is evaluated exactly once per ALLOCATE statement");
    expect_contains(result.code, "_scalar = (int32_t)",
                    "the single scalar value is reused for every target and element");
    f2c_result_free(&result);
}

static void test_runtime_shape_guard(void) {
    static const char source[] =
        "program allocation_shape_guard\n"
        "  integer, allocatable :: target(:), source(:)\n"
        "  integer :: requested, status\n"
        "  character(len=32) :: message\n"
        "  requested = 2\n"
        "  allocate(source(5))\n"
        "  allocate(target(requested), source=source, stat=status, errmsg=message)\n"
        "end program allocation_shape_guard\n";
    F2cResult result = transpile(source, "allocation-shape-guard.f90");
    expect(result.code != NULL && result.error_count == 0U,
           "dynamic explicit shapes retain a runtime SOURCE conformance guard");
    expect_contains(result.code, "if (f2c_alloc_extent_1 != (size_t)(f2c_allocate_model_",
                    "shape mismatch is detected before storage is committed");
    expect_contains(result.code, "f2c_store_message(message, (size_t)(32), \"allocation failed\")",
                    "a guarded mismatch is reported through STAT and ERRMSG");
    f2c_result_free(&result);
}

static void test_unavailable_source_guard(void) {
    static const char source[] = "program unavailable_source\n"
                                 "  integer, allocatable :: source, target\n"
                                 "  integer :: status\n"
                                 "  character(len=32) :: message\n"
                                 "  allocate(target, source=source, stat=status, errmsg=message)\n"
                                 "end program unavailable_source\n";
    F2cResult result = transpile(source, "unavailable-source.f90");
    expect(result.code != NULL && result.error_count == 0U,
           "an unallocated scalar SOURCE lowers to a recoverable runtime condition");
    expect_contains(result.code, "_available = true && source != NULL;",
                    "model availability is tested before scalar dereference");
    expect_contains(result.code, "_available) {",
                    "all model evaluation is dominated by the availability guard");
    expect_contains(result.code, "SOURCE/MOLD object is not allocated or associated",
                    "the unavailable model has a precise failure status");
    f2c_result_free(&result);
}

static void test_descriptor_scalar_source(void) {
    static const char source[] = "program scalar_model\n"
                                 "integer,allocatable :: a,b(:)\n"
                                 "interface\nfunction number() result(value)\n"
                                 "integer,allocatable :: value\nend function\nend interface\n"
                                 "allocate(a,b(3),source=number())\nend program\n";
    F2cResult result = transpile(source, "descriptor-scalar-source.f90");
    expect(result.code != NULL && result.error_count == 0U,
           "descriptor scalar SOURCE lowers through the common result materializer");
    expect_contains(result.code, "_descriptor = number();",
                    "the model captures one descriptor rather than casting it to a scalar");
    expect_contains(result.code, "_scalar = (int32_t)((*f2c_array_allocate_function_",
                    "SOURCE snapshots the descriptor value once before initializing targets");
    expect_contains(result.code, "free(f2c_array_allocate_function_",
                    "owned SOURCE storage participates in statement cleanup");
    f2c_result_free(&result);
}

static void test_complex_source_assignment(void) {
    static const char source[] =
        "program complex_model\n"
        "complex(kind=8),allocatable :: a,b(:),c(:)\n"
        "interface\nfunction value() result(z)\n"
        "complex(kind=8),allocatable :: z\nend function\nend interface\n"
        "allocate(a,b(3),source=value())\nallocate(c,source=b)\nend program\n";
    F2cResult result = transpile(source, "complex-source-assignment.f90");
    expect(result.code != NULL && result.error_count == 0U,
           "compatible complex SOURCE models support scalar and array consumers");
    expect_contains(result.code, "_scalar = (*f2c_array_allocate_function_",
                    "complex scalar model snapshots do not cast aggregate complex values");
    expect(result.code != NULL &&
               strstr(result.code, "= (f2c_complex_double)f2c_allocate_model_") == NULL &&
               strstr(result.code, "= (f2c_complex_double)f2c_transform_allocate_model_") == NULL,
           "complex destination elements use portable same-type assignment");
    f2c_result_free(&result);
}

static void test_pointer_mold_metadata(void) {
    static const char source[] =
        "program pointer_mold\n"
        "character(len=:),allocatable :: a(:)\n"
        "interface\nfunction words() result(value)\n"
        "character(len=:),pointer :: value(:)\nend function\nend interface\n"
        "allocate(a,mold=words())\nend program\n";
    F2cResult result = transpile(source, "pointer-mold-metadata.f90");
    expect(result.code != NULL && result.error_count == 0U,
           "pointer MOLD metadata is planned after binding external result characteristics");
    expect_contains(result.code, "_descriptor = words();",
                    "MOLD obtains pointer descriptor metadata in one call");
    expect(result.code != NULL &&
               strstr(result.code, "f2c_descriptor_read_record(f2c_array_allocate_function_") ==
                   NULL &&
               strstr(result.code, "free(f2c_array_allocate_function_") == NULL,
           "MOLD never reads or releases borrowed character result targets");
    expect_contains(result.code, ".character_length);",
                    "the allocation uses the returned dynamic character length");
    f2c_result_free(&result);
}

static void test_dependency_constraints(void) {
    static const char source_dependency[] = "program source_dependency\n"
                                            "  integer, allocatable :: a(:), b(:)\n"
                                            "  allocate(a, b, source=a)\n"
                                            "end program source_dependency\n";
    static const char bound_dependency[] = "program bound_dependency\n"
                                           "  integer, allocatable :: a(:), b(:)\n"
                                           "  allocate(a(3), b(size(a)))\n"
                                           "end program bound_dependency\n";
    static const char distinct_components[] = "program distinct_components\n"
                                              "  type :: box\n"
                                              "    integer, allocatable :: value(:)\n"
                                              "  end type box\n"
                                              "  type(box) :: left, right\n"
                                              "  allocate(right%value(2))\n"
                                              "  allocate(left%value, source=right%value)\n"
                                              "end program distinct_components\n";
    F2cResult result;
    expect_failure(source_dependency, "SOURCE= expression in ALLOCATE must not depend on",
                   "SOURCE cannot reference an object allocated by the same statement");
    expect_failure(bound_dependency, "bound expression in ALLOCATE must not depend on",
                   "a later bound cannot inspect an object allocated by the same statement");
    result = transpile(distinct_components, "distinct-components.f90");
    expect(result.code != NULL && result.error_count == 0U,
           "same-named components of distinct parent objects are not conflated");
    f2c_result_free(&result);
}

int main(void) {
    test_expression_model_lowering();
    test_scalar_single_evaluation();
    test_runtime_shape_guard();
    test_unavailable_source_guard();
    test_descriptor_scalar_source();
    test_complex_source_assignment();
    test_pointer_mold_metadata();
    test_dependency_constraints();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
