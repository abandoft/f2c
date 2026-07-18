#include "codegen/io/control/private.h"

#include <stdlib.h>

typedef struct F2cEmittedOpenControls {
    F2cEmittedCharacterControl file;
    F2cEmittedCharacterControl status;
    F2cEmittedCharacterControl access;
    F2cEmittedCharacterControl action;
    F2cEmittedCharacterControl form;
    F2cEmittedCharacterControl blank;
    F2cEmittedCharacterControl position;
    F2cEmittedCharacterControl delim;
    F2cEmittedCharacterControl pad;
} F2cEmittedOpenControls;

static void free_open_controls(F2cEmittedOpenControls *controls) {
    f2c_io_free_character_control(&controls->file);
    f2c_io_free_character_control(&controls->status);
    f2c_io_free_character_control(&controls->access);
    f2c_io_free_character_control(&controls->action);
    f2c_io_free_character_control(&controls->form);
    f2c_io_free_character_control(&controls->blank);
    f2c_io_free_character_control(&controls->position);
    f2c_io_free_character_control(&controls->delim);
    f2c_io_free_character_control(&controls->pad);
}

static int emit_open_controls(Unit *unit, const F2cStatement *statement,
                              F2cEmittedOpenControls *controls) {
    return f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_FILE, "\"\"", "0U",
                                         &controls->file) &&
           f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_STATUS, "\"unknown\"",
                                         "7U", &controls->status) &&
           f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_ACCESS, "\"sequential\"",
                                         "10U", &controls->access) &&
           f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_ACTION, "\"readwrite\"",
                                         "9U", &controls->action) &&
           f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_FORM, "\"\"", "0U",
                                         &controls->form) &&
           f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_BLANK, "\"null\"", "4U",
                                         &controls->blank) &&
           f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_POSITION, "\"asis\"", "4U",
                                         &controls->position) &&
           f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_DELIM, "\"none\"", "4U",
                                         &controls->delim) &&
           f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_PAD, "\"yes\"", "3U",
                                         &controls->pad);
}

int f2c_emit_open_statement(Context *context, Unit *unit, const F2cStatement *statement,
                            int depth) {
    const F2cIoControl *unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const F2cIoControl *recl_control = f2c_io_control(statement, F2C_IO_CONTROL_RECL, (size_t)-1);
    F2cEmittedOpenControls controls = {0};
    F2cEmittedIoStatus status;
    char *unit_value = NULL;
    char *recl_value = NULL;
    int emitted = 0;
    if (unit_control == NULL || unit_control->value == NULL)
        goto cleanup;
    unit_value = f2c_io_emit_required_expression(unit, unit_control->value);
    recl_value = recl_control != NULL && recl_control->value != NULL
                     ? f2c_io_emit_required_expression(unit, recl_control->value)
                     : f2c_strdup("0");
    if (unit_value == NULL || recl_value == NULL ||
        !emit_open_controls(unit, statement, &controls) ||
        !f2c_io_emit_status_controls(unit, statement, &status))
        goto cleanup;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_io_ok = f2c_open_unit_full((int32_t)(%s), %s, (size_t)(%s), "
                      "%s, (size_t)(%s), %s, (size_t)(%s), %s, (size_t)(%s), %s, (size_t)(%s), "
                      "(int32_t)(%s), %s, (size_t)(%s), %s, (size_t)(%s), %s, (size_t)(%s), %s, "
                      "(size_t)(%s));\n",
                      unit_value, controls.file.pointer, controls.file.length,
                      controls.status.pointer, controls.status.length, controls.access.pointer,
                      controls.access.length, controls.action.pointer, controls.action.length,
                      controls.form.pointer, controls.form.length, recl_value,
                      controls.blank.pointer, controls.blank.length, controls.position.pointer,
                      controls.position.length, controls.delim.pointer, controls.delim.length,
                      controls.pad.pointer, controls.pad.length);
    f2c_io_emit_control_result(context, &status, "OPEN", depth + 1);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    emitted = 1;
    f2c_io_free_status_controls(&status);

cleanup:
    free(unit_value);
    free(recl_value);
    free_open_controls(&controls);
    return emitted;
}

int f2c_emit_close_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth) {
    const F2cIoControl *unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    F2cEmittedCharacterControl close_status = {0};
    F2cEmittedIoStatus status;
    char *unit_value;
    if (unit_control == NULL || unit_control->value == NULL)
        return 0;
    unit_value = f2c_io_emit_required_expression(unit, unit_control->value);
    if (unit_value == NULL ||
        !f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_STATUS, "\"keep\"", "4U",
                                       &close_status) ||
        !f2c_io_emit_status_controls(unit, statement, &status)) {
        free(unit_value);
        f2c_io_free_character_control(&close_status);
        return 0;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_io_ok = f2c_close_unit_with_status((int32_t)(%s), %s, "
                      "(size_t)(%s));\n",
                      unit_value, close_status.pointer, close_status.length);
    f2c_io_emit_control_result(context, &status, "CLOSE", depth + 1);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(unit_value);
    f2c_io_free_character_control(&close_status);
    f2c_io_free_status_controls(&status);
    return 1;
}
