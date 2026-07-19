#include "codegen/io/control/private.h"

#include <stdlib.h>

static const char *inquiry_result_field(F2cIoControlKind kind) {
    static const char *const fields[F2C_IO_CONTROL_READWRITE + 1U] = {
        [F2C_IO_CONTROL_EXIST] = "exist",
        [F2C_IO_CONTROL_OPENED] = "opened",
        [F2C_IO_CONTROL_NUMBER] = "number",
        [F2C_IO_CONTROL_NAMED] = "named",
        [F2C_IO_CONTROL_NAME] = "name",
        [F2C_IO_CONTROL_ACCESS] = "access",
        [F2C_IO_CONTROL_SEQUENTIAL] = "sequential",
        [F2C_IO_CONTROL_DIRECT] = "direct",
        [F2C_IO_CONTROL_FORM] = "form",
        [F2C_IO_CONTROL_FORMATTED] = "formatted",
        [F2C_IO_CONTROL_UNFORMATTED] = "unformatted",
        [F2C_IO_CONTROL_RECL] = "recl",
        [F2C_IO_CONTROL_NEXTREC] = "nextrec",
        [F2C_IO_CONTROL_BLANK] = "blank",
        [F2C_IO_CONTROL_POSITION] = "position",
        [F2C_IO_CONTROL_ACTION] = "action",
        [F2C_IO_CONTROL_READ] = "read",
        [F2C_IO_CONTROL_WRITE] = "write",
        [F2C_IO_CONTROL_READWRITE] = "readwrite",
        [F2C_IO_CONTROL_DELIM] = "delim",
        [F2C_IO_CONTROL_PAD] = "pad",
    };
    return (size_t)kind < sizeof(fields) / sizeof(fields[0]) ? fields[kind] : NULL;
}

static int logical_inquiry_result(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_EXIST || kind == F2C_IO_CONTROL_OPENED ||
           kind == F2C_IO_CONTROL_NAMED;
}

static int integer_inquiry_result(F2cIoControlKind kind) {
    return kind == F2C_IO_CONTROL_NUMBER || kind == F2C_IO_CONTROL_RECL ||
           kind == F2C_IO_CONTROL_NEXTREC;
}

static int emit_inquiry_results(Context *context, Unit *unit, const F2cStatement *statement,
                                int depth) {
    size_t index;
    for (index = 0U; index < statement->control_count; ++index) {
        const F2cIoControl *control = &statement->io_controls[index];
        const char *field = inquiry_result_field(control->kind);
        char *destination;
        if (field == NULL || control->value == NULL)
            continue;
        destination = f2c_io_emit_required_expression(unit, control->value);
        if (destination == NULL)
            return 0;
        f2c_io_indent(&context->output, depth);
        if (logical_inquiry_result(control->kind) || integer_inquiry_result(control->kind)) {
            f2c_buffer_printf(&context->output, "%s = f2c_inquiry_result.%s;\n", destination,
                              field);
        } else {
            char *pointer = f2c_character_source_pointer(unit, control->value, destination);
            char *length = f2c_character_length_expression(unit, control->value);
            if (pointer == NULL || length == NULL) {
                free(destination);
                free(pointer);
                free(length);
                return 0;
            }
            f2c_buffer_printf(&context->output,
                              "f2c_assign_inquiry_character(%s, (size_t)(%s), "
                              "f2c_inquiry_result.%s);\n",
                              pointer, length, field);
            free(pointer);
            free(length);
        }
        free(destination);
    }
    return 1;
}

int f2c_emit_inquire_statement(Context *context, Unit *unit, const F2cStatement *statement,
                               int depth) {
    if (f2c_io_control(statement, F2C_IO_CONTROL_IOLENGTH, (size_t)-1) != NULL)
        return f2c_emit_iolength_statement(context, unit, statement, depth);
    const F2cIoControl *unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, (size_t)-1);
    const F2cIoControl *file_control = f2c_io_control(statement, F2C_IO_CONTROL_FILE, (size_t)-1);
    F2cEmittedCharacterControl file = {0};
    F2cEmittedIoStatus status;
    char *unit_value = NULL;
    if (unit_control != NULL && unit_control->value != NULL)
        unit_value = f2c_io_emit_required_expression(unit, unit_control->value);
    if (file_control != NULL &&
        !f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_FILE, NULL, NULL, &file))
        goto failure;
    if ((unit_control != NULL && unit_value == NULL) ||
        !f2c_io_emit_status_controls(unit, statement, &status))
        goto failure;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "f2c_inquiry f2c_inquiry_result;\n");
    f2c_io_indent(&context->output, depth + 1);
    if (unit_value != NULL) {
        f2c_buffer_printf(&context->output,
                          "const bool f2c_io_ok = f2c_inquire_unit((int32_t)(%s), "
                          "&f2c_inquiry_result);\n",
                          unit_value);
    } else if (file.pointer != NULL) {
        f2c_buffer_printf(&context->output,
                          "const bool f2c_io_ok = f2c_inquire_file(%s, (size_t)(%s), "
                          "&f2c_inquiry_result);\n",
                          file.pointer, file.length);
    } else {
        goto failure_after_output;
    }
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_io_ok) {\n");
    if (!emit_inquiry_results(context, unit, statement, depth + 2))
        goto failure_after_output;
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "f2c_inquiry_dispose(&f2c_inquiry_result);\n");
    f2c_io_emit_control_result(context, &status, "INQUIRE", depth + 1);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(unit_value);
    f2c_io_free_character_control(&file);
    f2c_io_free_status_controls(&status);
    return 1;

failure_after_output:
    f2c_io_free_status_controls(&status);
failure:
    free(unit_value);
    f2c_io_free_character_control(&file);
    return 0;
}
