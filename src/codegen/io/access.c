#include "codegen/io/private.h"

#include "codegen/names.h"
#include "codegen/storage/private.h"

#include <stdlib.h>

void f2c_io_append_symbol_element(Buffer *output, Unit *unit, const Symbol *symbol,
                                  const char *index) {
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(symbol);
    char *element = f2c_storage_linear_element(unit, &reference, index);
    if (element == NULL)
        output->failed = 1;
    else
        f2c_buffer_append(output, element);
    free(element);
}

void f2c_io_emit_qualified_input(Context *context, Unit *unit, const char *file, const char *value,
                                 const char *c_type, const char *status, int depth) {
    char *input_name = f2c_codegen_local_name(unit, "f2c_qualified_input");
    char *read_name = f2c_codegen_local_name(unit, "f2c_qualified_read");
    if (input_name == NULL || read_name == NULL) {
        free(input_name);
        free(read_name);
        context->output.failed = 1;
        return;
    }
    /* Conversion writes ordinary temporary storage. Only a successful value
     * transfer performs the scoped qualified definition of the target. */
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "{ %s %s = {0}; int %s = F2C_READ(%s, &%s); ", c_type,
                      input_name, read_name, file, input_name);
    if (status != NULL)
        f2c_buffer_printf(&context->output, "%s = f2c_list_read_status(%s); ", status, read_name);
    f2c_buffer_printf(&context->output, "if (%s == 1) %s = %s; }\n", read_name, value, input_name);
    free(input_name);
    free(read_name);
}
