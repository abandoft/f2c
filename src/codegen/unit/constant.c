#include "codegen/unit/private.h"

#include "codegen/array/static_shape.h"

#include <stdint.h>
#include <stdlib.h>

int f2c_unit_emit_parameter_array(Context *context, Unit *unit, const Symbol *symbol) {
    size_t count;
    char *initializer;
    if (!f2c_static_array_element_count(unit, symbol, &count))
        goto unsupported;
    if (symbol->type == TYPE_CHARACTER) {
        int64_t length;
        if (!f2c_character_declaration_length(unit, symbol, &length) || length < 0 ||
            (uint64_t)length > SIZE_MAX || (length != 0 && count > SIZE_MAX / (size_t)length))
            goto unsupported;
        count *= (size_t)length;
    }
    initializer =
        count == 0U ? f2c_strdup("{0}") : f2c_unit_static_storage_initializer(unit, symbol);
    if (initializer == NULL)
        goto unsupported;
    f2c_unit_indent(&context->output, 1);
    f2c_buffer_printf(&context->output, "static const %s %s[%zu] = %s;\n",
                      f2c_symbol_c_type(symbol), f2c_symbol_c_name(unit, symbol),
                      count != 0U ? count : 1U, initializer);
    free(initializer);
    f2c_unit_indent(&context->output, 1);
    f2c_buffer_printf(&context->output, "(void)%s;\n", f2c_symbol_c_name(unit, symbol));
    return !context->output.failed;

unsupported:
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, &symbol->declaration_span, 1,
                             "constant array '%s' cannot be materialized as typed static C17 data",
                             symbol->name);
    return 0;
}
