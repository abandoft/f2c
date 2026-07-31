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

static F2cConfig default_config(void) {
    F2cConfig config;
    memset(&config, 0, sizeof(config));
    config.structure_size = sizeof(config);
    return config;
}

static F2cResult transpile_config(const char *source, const char *source_name,
                                  const F2cConfig *config) {
    F2cInput input = {
        source,
        strlen(source),
        {source_name, F2C_SOURCE_FREE, 0},
    };
    return f2c_transpile_project_config(&input, 1U, config);
}

static void test_missing_non_intrinsic_module(void) {
    static const char unspecified[] = "program missing_unspecified\n"
                                      "  use absent_provider\n"
                                      "end program missing_unspecified\n";
    static const char non_intrinsic[] = "program missing_non_intrinsic\n"
                                        "  use, non_intrinsic :: absent_provider\n"
                                        "end program missing_non_intrinsic\n";
    F2cConfig config = default_config();
    F2cResult result = transpile_config(unspecified, "missing-unspecified.f90", &config);

    expect(result.error_count != 0U && result.code == NULL,
           "an unresolved unspecified USE statement is a hard error");
    expect_contains(result.diagnostics, "non-intrinsic module 'absent_provider' is not present",
                    "an unresolved unspecified USE diagnostic names the missing provider");
    f2c_result_free(&result);

    result = transpile_config(non_intrinsic, "missing-non-intrinsic.f90", &config);
    expect(result.error_count != 0U && result.code == NULL,
           "an unresolved explicit NON_INTRINSIC USE statement is a hard error");
    expect_contains(result.diagnostics, "declare it as an external module",
                    "the NON_INTRINSIC diagnostic explains the explicit-provider policy");
    f2c_result_free(&result);
}

static void test_external_module_allowlist(void) {
    static const char source[] = "program external_consumer\n"
                                 "  use external_provider\n"
                                 "end program external_consumer\n";
    static const char *const external_modules[] = {"External_Provider"};
    F2cConfig config = default_config();
    F2cResult result;

    config.external_module_names = external_modules;
    config.external_module_count = 1U;
    result = transpile_config(source, "external-consumer.f90", &config);
    expect(result.error_count == 0U && result.code != NULL,
           "an explicitly declared external module is accepted case-insensitively");
    f2c_result_free(&result);
}

static void test_intrinsic_module_policy(void) {
    static const char supported[] = "program environment_kinds\n"
                                    "  use, intrinsic :: iso_fortran_env, only: real64\n"
                                    "  real(kind=real64) :: value\n"
                                    "  value = 1.0d0\n"
                                    "end program environment_kinds\n";
    static const char unknown[] = "program unknown_intrinsic\n"
                                  "  use, intrinsic :: vendor_magic\n"
                                  "end program unknown_intrinsic\n";
    static const char unimplemented[] = "program unimplemented_intrinsic\n"
                                        "  use, intrinsic :: iso_c_binding\n"
                                        "end program unimplemented_intrinsic\n";
    F2cConfig config = default_config();
    F2cResult result = transpile_config(supported, "environment-kinds.f90", &config);

    expect(result.error_count == 0U && result.code != NULL,
           "the implemented ISO_FORTRAN_ENV subset is accepted as intrinsic");
    f2c_result_free(&result);

    result = transpile_config(unknown, "unknown-intrinsic.f90", &config);
    expect(result.error_count != 0U && result.code == NULL,
           "an unknown explicit intrinsic module is rejected");
    expect_contains(result.diagnostics, "intrinsic module 'vendor_magic' is not supported",
                    "an unknown intrinsic-module diagnostic names the module");
    f2c_result_free(&result);

    result = transpile_config(unimplemented, "unimplemented-intrinsic.f90", &config);
    expect(result.error_count != 0U && result.code == NULL,
           "a standard intrinsic module without an implementation is rejected");
    expect_contains(result.diagnostics, "intrinsic module 'iso_c_binding' is not supported",
                    "standard module availability reflects actual implementation coverage");
    f2c_result_free(&result);
}

static void test_project_module_provider(void) {
    static const char consumer[] = "program project_consumer\n"
                                   "  use, non_intrinsic :: project_provider, only: answer\n"
                                   "  implicit none\n"
                                   "  if (answer /= 42) error stop 1\n"
                                   "end program project_consumer\n";
    static const char provider[] = "module project_provider\n"
                                   "  implicit none\n"
                                   "  integer, parameter :: answer = 42\n"
                                   "end module project_provider\n";
    F2cInput inputs[] = {
        {consumer, sizeof(consumer) - 1U,
         {"project-consumer.f90", F2C_SOURCE_FREE, 0}},
        {provider, sizeof(provider) - 1U,
         {"project-provider.f90", F2C_SOURCE_FREE, 0}},
    };
    F2cResult result = f2c_transpile_project(inputs, sizeof(inputs) / sizeof(inputs[0]));

    expect(result.error_count == 0U && result.code != NULL,
           "a module provider later in the same request satisfies explicit NON_INTRINSIC USE");
    f2c_result_free(&result);
}

static void test_external_module_configuration(void) {
    static const char source[] = "program configuration\nend program configuration\n";
    static const char *const invalid_names[] = {"bad-name"};
    static const char *const duplicate_names[] = {"provider", "PROVIDER"};
    static const char *const two_names[] = {"first_provider", "second_provider"};
    F2cConfig config = default_config();
    F2cResult result;

    config.external_module_count = 1U;
    result = transpile_config(source, "null-external-list.f90", &config);
    expect(result.error_count != 0U && result.code == NULL,
           "a nonzero external-module count requires a name list");
    expect_contains(result.diagnostics, "external_module_names is null",
                    "a null external-module list has a precise configuration diagnostic");
    f2c_result_free(&result);

    config = default_config();
    config.external_module_names = invalid_names;
    config.external_module_count = 1U;
    result = transpile_config(source, "invalid-external-name.f90", &config);
    expect(result.error_count != 0U && result.code == NULL,
           "an invalid Fortran external-module name is rejected");
    expect_contains(result.diagnostics, "is not a valid Fortran name",
                    "invalid external-module names have a precise configuration diagnostic");
    f2c_result_free(&result);

    config = default_config();
    config.external_module_names = duplicate_names;
    config.external_module_count = 2U;
    result = transpile_config(source, "duplicate-external-name.f90", &config);
    expect(result.error_count != 0U && result.code == NULL,
           "case-insensitive duplicate external-module names are rejected");
    expect_contains(result.diagnostics, "'PROVIDER' is listed more than once",
                    "duplicate external-module diagnostics identify the repeated name");
    f2c_result_free(&result);

    config = default_config();
    config.limits.max_external_modules = 1U;
    config.external_module_names = two_names;
    config.external_module_count = 2U;
    result = transpile_config(source, "external-module-budget.f90", &config);
    expect(result.error_count != 0U && result.code == NULL,
           "external-module declarations obey the request resource budget");
    expect_contains(result.diagnostics, "external module limit of 1 exceeded",
                    "external-module budget diagnostics identify the configured limit");
    f2c_result_free(&result);
}

int main(void) {
    test_missing_non_intrinsic_module();
    test_external_module_allowlist();
    test_intrinsic_module_policy();
    test_project_module_provider();
    test_external_module_configuration();
    if (failures != 0) {
        fprintf(stderr, "%d module-resolution test(s) failed\n", failures);
        return 1;
    }
    puts("module-resolution tests passed");
    return 0;
}
