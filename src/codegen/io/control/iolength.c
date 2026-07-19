#include "codegen/io/control/private.h"

#include <stdlib.h>

int f2c_emit_iolength_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                int depth) {
    const F2cIoControl *control = f2c_io_control(statement, F2C_IO_CONTROL_IOLENGTH, (size_t)-1);
    char *target;
    const char *target_type;
    const int target_kind =
        control != NULL && control->value != NULL && control->value->type_kind > 0
            ? control->value->type_kind
            : f2c_default_kind(TYPE_INTEGER);
    size_t item;
    if (context == NULL || unit == NULL || statement == NULL || control == NULL ||
        control->value == NULL || statement->io_item_count == 0U)
        return 0;
    target = f2c_io_emit_required_expression(unit, control->value);
    if (target == NULL)
        return 0;
    target_type = f2c_expression_c_type(control->value);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "f2c_io_stream f2c_iolength_stream;\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "f2c_stream_initialize_counter(&f2c_iolength_stream);\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "int f2c_iolength_status = F2C_IO_STATUS_OK;\n");
    for (item = 0U; item < statement->io_item_count; ++item) {
        if (!f2c_io_emit_unformatted_item(context, unit, &statement->io_items[item], 0,
                                          "&f2c_iolength_stream", "INT32_C(-1)",
                                          "f2c_iolength_status", depth + 1)) {
            free(target);
            return 0;
        }
    }
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(
        &context->output,
        "if (f2c_iolength_stream.error) f2c_io_abort_unhandled(F2C_IO_STATUS_OVERFLOW, "
        "\"INQUIRE(IOLENGTH)\");\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_iolength_status != F2C_IO_STATUS_OK) f2c_io_abort_unhandled("
                      "f2c_iolength_status, \"INQUIRE(IOLENGTH)\");\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "%s = (%s)f2c_inquiry_size_integer(f2c_iolength_stream.position, %d);\n",
                      target, target_type, target_kind);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(target);
    return 1;
}
