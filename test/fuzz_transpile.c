#include "f2c/f2c.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    F2cOptions options = {"libfuzzer-input", F2C_SOURCE_FREE, 0};
    F2cResult result;
    const F2cConfig config = {
        .structure_size = sizeof(F2cConfig),
        /* Small-input campaigns must also bound expansion work: a few input
         * bytes can otherwise request millions of static initializer elements.
         * Default production limits remain covered by normal resource tests. */
        .limits = {.max_ast_nodes = 65536U,
                   .max_constant_steps = 65536U,
                   .max_output_bytes = 4U * 1024U * 1024U}};
    const char *source;
    size_t source_size;

    if (size == 0U)
        return 0;
    options.source_form = (data[0] & UINT8_C(1)) != 0U ? F2C_SOURCE_FIXED : F2C_SOURCE_FREE;
    source = (const char *)(data + 1U);
    source_size = size - 1U;
    const F2cInput input = {source, source_size, options};
    result = f2c_transpile_project_config(&input, 1U, &config);
    if (result.diagnostics == NULL ||
        (result.error_count == 0U && (result.code == NULL || result.header == NULL))) {
        f2c_result_free(&result);
        abort();
    }
    f2c_result_free(&result);
    return 0;
}
