#include "codegen/io/control/private.h"

int f2c_io_emit_transfer_controls(Context *context, Unit *unit, const F2cStatement *statement,
                                  const char *stream, const char *unit_number, const char *status,
                                  int depth) {
    static const F2cIoControlKind kinds[] = {F2C_IO_CONTROL_DECIMAL, F2C_IO_CONTROL_ROUND,
                                             F2C_IO_CONTROL_SIGN, F2C_IO_CONTROL_DELIM};
    F2cEmittedCharacterControl controls[4] = {{0}};
    size_t index;
    int success = 0;
    for (index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        if (!f2c_io_emit_character_control(unit, statement, kinds[index], "NULL", "0U",
                                           &controls[index]))
            goto cleanup;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "f2c_io_control_scope f2c_io_control_state = {0};\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (%s == F2C_IO_STATUS_OK && !f2c_io_enter_controls("
                      "&f2c_io_control_state, %s, (int32_t)(%s)",
                      status, stream, unit_number);
    for (index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index)
        f2c_buffer_printf(&context->output, ", %s, (size_t)(%s)", controls[index].pointer,
                          controls[index].length);
    f2c_buffer_printf(&context->output, ")) %s = F2C_IO_STATUS_RECORD;\n", status);
    success = 1;
cleanup:
    for (index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index)
        f2c_io_free_character_control(&controls[index]);
    return success;
}
