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
    test_dependency_constraints();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
