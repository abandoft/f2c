#include "f2c/f2c.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *source, int accepted, const char *needle) {
    const F2cOptions options = {"derived-initialization.f90", F2C_SOURCE_FREE, 0};
    F2cResult result = f2c_transpile(source, strlen(source), &options);
    if ((accepted && (result.code == NULL || result.error_count != 0U)) ||
        (!accepted && (result.code != NULL || result.error_count == 0U)) ||
        (needle != NULL &&
         (result.diagnostics == NULL || strstr(result.diagnostics, needle) == NULL))) {
        fprintf(stderr, "FAIL: derived initialization contract\n%s\n",
                result.diagnostics != NULL ? result.diagnostics : "");
        ++failures;
    }
    f2c_result_free(&result);
}

static void check_expansion_budget(void) {
    static const char source[] = "module bounded_defaults\n"
                                 " type :: large\n"
                                 "  integer :: values(1000000000) = 1\n"
                                 " end type\n"
                                 "end module\n";
    F2cInput input = {source, sizeof(source) - 1U, {"bounded-defaults.f90", F2C_SOURCE_FREE, 0}};
    F2cConfig config = {0};
    F2cResult result;
    config.structure_size = sizeof(config);
    config.limits.max_output_bytes = 65536U;
    result = f2c_transpile_project_config(&input, 1U, &config);
    if (result.code != NULL || result.error_count == 0U || result.diagnostics == NULL ||
        strstr(result.diagnostics, "generated output limit") == NULL) {
        fprintf(stderr, "FAIL: oversized default expansion must fail within the output budget\n");
        ++failures;
    }
    f2c_result_free(&result);
    result = f2c_transpile_project(&input, 1U);
    if (result.code != NULL || result.error_count == 0U || result.diagnostics == NULL ||
        strstr(result.diagnostics, "generated output limit") == NULL) {
        fprintf(stderr,
                "FAIL: default budget must bound actual initializer bytes before expansion\n");
        ++failures;
    }
    f2c_result_free(&result);
}

static void check_expansion_work_budget(void) {
    static const char source[] = "module bounded_defaults\n"
                                 " type :: large\n"
                                 "  integer :: values(1000000) = 1\n"
                                 " end type\n"
                                 "end module\n";
    F2cInput input = {source, sizeof(source) - 1U, {"bounded-defaults.f90", F2C_SOURCE_FREE, 0}};
    F2cConfig config = {.structure_size = sizeof(config)};
    F2cResult result;
    config.limits.max_constant_steps = 64U;
    result = f2c_transpile_project_config(&input, 1U, &config);
    if (result.code != NULL || result.error_count == 0U || result.diagnostics == NULL ||
        strstr(result.diagnostics, "constant-evaluation step limit") == NULL) {
        fprintf(stderr, "FAIL: derived broadcast work must fit the constant budget\n");
        ++failures;
    }
    f2c_result_free(&result);
    {
        static const char characters[] = "module bounded_characters\n"
                                         " type :: large\n"
                                         "  character :: values(1000000) = 'a'\n"
                                         " end type\n"
                                         "end module\n";
        input.source = characters;
        input.length = sizeof(characters) - 1U;
        result = f2c_transpile_project_config(&input, 1U, &config);
        if (result.code != NULL || result.error_count == 0U || result.diagnostics == NULL ||
            strstr(result.diagnostics, "constant-evaluation step limit") == NULL) {
            fprintf(stderr, "FAIL: character-component expansion must fit the work budget\n");
            ++failures;
        }
        f2c_result_free(&result);
    }
}

int main(void) {
    check_expansion_budget();
    check_expansion_work_budget();
    check("module legal_recursive_type\n"
          " type :: node\n"
          "  integer :: value = 2\n"
          "  type(node), pointer :: next => null()\n"
          " end type\n"
          "end module\n",
          1, NULL);
    check("module invalid_recursive_type\n"
          " type :: node\n"
          "  type(node) :: next\n"
          " end type\n"
          "end module\n",
          0, "recursive nonpointer, nonallocatable storage cycle");
    check("module omitted_components\n"
          " type :: leaf\n"
          "  integer :: value = 3\n"
          " end type\n"
          " type :: item\n"
          "  type(leaf) :: nested\n"
          "  integer, allocatable :: payload(:)\n"
          " end type\n"
          "end module\n"
          "program defaults\n"
          " use omitted_components\n"
          " type(item) :: value\n"
          " value = item()\n"
          "end program\n",
          1, NULL);
    check("program required_component\n"
          " type :: item\n"
          "  integer :: required\n"
          " end type\n"
          " type(item) :: value\n"
          " value = item()\n"
          "end program\n",
          0, "does not initialize component 'required'");
    return failures != 0;
}
