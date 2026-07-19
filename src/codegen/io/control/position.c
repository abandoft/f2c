#include "codegen/io/control/private.h"

#include <stdlib.h>

int f2c_emit_position_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                int depth) {
    const F2cIoControl *unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const char *function_name;
    const char *statement_name;
    F2cEmittedIoStatus status;
    char *unit_value;
    if (statement->kind == F2C_STMT_REWIND) {
        function_name = "f2c_rewind_unit";
        statement_name = "REWIND";
    } else if (statement->kind == F2C_STMT_BACKSPACE) {
        function_name = "f2c_backspace_unit";
        statement_name = "BACKSPACE";
    } else if (statement->kind == F2C_STMT_ENDFILE) {
        function_name = "f2c_endfile_unit";
        statement_name = "ENDFILE";
    } else {
        return 0;
    }
    if (unit_control == NULL || unit_control->value == NULL)
        return 0;
    unit_value = f2c_io_emit_required_expression(unit, unit_control->value);
    if (unit_value == NULL || !f2c_io_emit_status_controls(unit, statement, &status)) {
        free(unit_value);
        return 0;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_io_ok = %s((int32_t)(%s));\n",
                      function_name, unit_value);
    f2c_io_emit_control_result(context, unit, statement, &status, statement_name, depth + 1);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(unit_value);
    f2c_io_free_status_controls(&status);
    return 1;
}
