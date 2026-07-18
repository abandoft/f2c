#include "codegen/io/private.h"

int f2c_emit_print_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth) {
    size_t index;
    if (statement->io_item_count != statement->item_count)
        return 0;
    for (index = 0U; index < statement->io_item_count; ++index)
        f2c_io_emit_item(context, unit, "stdout", &statement->io_items[index], 0, NULL, 0,
                         F2C_DEFINED_IO_WRITE_FORMATTED, "6", "\"LISTDIRECTED\"", depth);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "fputc('\\n', stdout);\n");
    return 1;
}
