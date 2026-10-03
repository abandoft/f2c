#include "codegen/io/private.h"

#include "codegen/names.h"

#include <stdlib.h>

void f2c_io_append_symbol_element(Buffer *output, Unit *unit, const Symbol *symbol,
                                  const char *index) {
    if (symbol->volatile_entity)
        f2c_buffer_printf(output, "((volatile %s *)%s)[%s]", f2c_symbol_c_type(symbol),
                          f2c_symbol_c_name(unit, symbol), index);
    else
        f2c_buffer_printf(output, "%s[%s]", f2c_symbol_c_name(unit, symbol), index);
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
