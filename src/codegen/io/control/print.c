#include "codegen/io/private.h"

int f2c_emit_print_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth) {
    const F2cIoControl *format_control;
    size_t index;
    if (!statement->io_syntax_valid || statement->io_item_count != statement->item_count)
        return 0;
    format_control = f2c_io_control(statement, F2C_IO_CONTROL_FMT, 0U);
    if (format_control == NULL)
        return 0;
    if (!format_control->asterisk)
        return f2c_io_emit_formatted_transfer(context, unit, statement, format_control,
                                              "f2c_unit_stream(6, false)", "6", 0, "true", NULL,
                                              NULL, depth);
    for (index = 0U; index < statement->io_item_count; ++index)
        f2c_io_emit_item(context, unit, "f2c_unit_stream(6, false)", &statement->io_items[index], 0,
                         NULL, 0, F2C_DEFINED_IO_WRITE_FORMATTED, "6", "\"LISTDIRECTED\"", depth);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "if (f2c_child_io_depth == 0U) "
                      "(void)f2c_stream_putc('\\n', f2c_unit_stream(6, false));\n");
    return 1;
}
