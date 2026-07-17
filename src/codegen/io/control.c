#include "codegen/io/private.h"

#include <stdlib.h>

int f2c_emit_print_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth) {
    size_t i;
    if (statement->io_item_count != statement->item_count)
        return 0;
    for (i = 0U; i < statement->io_item_count; ++i)
        f2c_io_emit_item(context, unit, "stdout", &statement->io_items[i], 0, NULL, 0,
                         F2C_DEFINED_IO_WRITE_FORMATTED, "6", "\"LISTDIRECTED\"", depth);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "fputc('\\n', stdout);\n");
    return 1;
}

int f2c_emit_open_statement(Context *context, Unit *unit, const F2cStatement *statement,
                            int depth) {
    const F2cIoControl *unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const F2cIoControl *file_control = f2c_io_control(statement, F2C_IO_CONTROL_FILE, (size_t)-1);
    const F2cIoControl *status_control =
        f2c_io_control(statement, F2C_IO_CONTROL_STATUS, (size_t)-1);
    const F2cIoControl *form_control = f2c_io_control(statement, F2C_IO_CONTROL_FORM, (size_t)-1);
    const F2cIoControl *iostat_control =
        f2c_io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    const F2cIoControl *err_control = f2c_io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    char *unit_value;
    char *file_value;
    char *file_length;
    char *status_value = NULL;
    char *status_length = NULL;
    char *form_value = NULL;
    char *form_length = NULL;
    char *iostat_value = NULL;
    char *err_label = NULL;
    if (unit_control == NULL || unit_control->value == NULL)
        return 0;
    unit_value = f2c_io_emit_required_expression(unit, unit_control->value);
    file_value = file_control != NULL && file_control->value != NULL
                     ? f2c_io_emit_required_expression(unit, file_control->value)
                     : f2c_strdup("\"\"");
    file_length = file_control != NULL && file_control->value != NULL
                      ? f2c_character_length_expression(unit, file_control->value)
                      : f2c_strdup("0U");
    if (status_control != NULL && status_control->value != NULL) {
        status_value = f2c_io_emit_required_expression(unit, status_control->value);
        status_length = f2c_character_length_expression(unit, status_control->value);
    }
    if (form_control != NULL && form_control->value != NULL) {
        form_value = f2c_io_emit_required_expression(unit, form_control->value);
        form_length = f2c_character_length_expression(unit, form_control->value);
    }
    if (iostat_control != NULL && iostat_control->value != NULL)
        iostat_value = f2c_io_emit_required_expression(unit, iostat_control->value);
    if (err_control != NULL && err_control->value != NULL)
        err_label = f2c_io_emit_required_expression(unit, err_control->value);
    if (unit_value == NULL || file_value == NULL || file_length == NULL ||
        (status_control != NULL && (status_value == NULL || status_length == NULL)) ||
        (form_control != NULL && (form_value == NULL || form_length == NULL)) ||
        (iostat_control != NULL && iostat_value == NULL) ||
        (err_control != NULL && err_label == NULL)) {
        free(unit_value);
        free(file_value);
        free(file_length);
        free(status_value);
        free(status_length);
        free(form_value);
        free(form_length);
        free(iostat_value);
        free(err_label);
        return 0;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_io_ok = f2c_open_unit((int32_t)(%s), %s, "
                      "(size_t)(%s), %s, (size_t)(%s), %s, (size_t)(%s));\n",
                      unit_value, file_value, file_length,
                      status_value != NULL ? status_value : "\"unknown\"",
                      status_length != NULL ? status_length : "7U",
                      form_value != NULL ? form_value : "\"formatted\"",
                      form_length != NULL ? form_length : "9U");
    if (iostat_value != NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_io_ok ? 0 : 1;\n", iostat_value);
    }
    if (err_label != NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (!f2c_io_ok) goto f2c_label_%s;\n", err_label);
    } else if (iostat_value == NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (!f2c_io_ok) { fputs(\"Fortran OPEN failed\\n\", stderr); "
                          "abort(); }\n");
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(unit_value);
    free(file_value);
    free(file_length);
    free(status_value);
    free(status_length);
    free(form_value);
    free(form_length);
    free(iostat_value);
    free(err_label);
    return 1;
}

int f2c_emit_rewind_statement(Context *context, Unit *unit, const F2cStatement *statement,
                              int depth) {
    const F2cIoControl *unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const F2cIoControl *iostat_control =
        f2c_io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    const F2cIoControl *err_control = f2c_io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    char *unit_value;
    char *iostat_value = NULL;
    char *err_label = NULL;
    if (unit_control == NULL || unit_control->value == NULL)
        return 0;
    unit_value = f2c_io_emit_required_expression(unit, unit_control->value);
    if (iostat_control != NULL && iostat_control->value != NULL)
        iostat_value = f2c_io_emit_required_expression(unit, iostat_control->value);
    if (err_control != NULL && err_control->value != NULL)
        err_label = f2c_io_emit_required_expression(unit, err_control->value);
    if (unit_value == NULL || (iostat_control != NULL && iostat_value == NULL) ||
        (err_control != NULL && err_label == NULL)) {
        free(unit_value);
        free(iostat_value);
        free(err_label);
        return 0;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_io_ok = f2c_rewind_unit((int32_t)(%s));\n",
                      unit_value);
    if (iostat_value != NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_io_ok ? 0 : 1;\n", iostat_value);
    }
    if (err_label != NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (!f2c_io_ok) goto f2c_label_%s;\n", err_label);
    } else if (iostat_value == NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (!f2c_io_ok) { fputs(\"Fortran REWIND failed\\n\", stderr); "
                          "abort(); }\n");
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(unit_value);
    free(iostat_value);
    free(err_label);
    return 1;
}

int f2c_emit_close_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth) {
    const F2cIoControl *unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const F2cIoControl *iostat_control =
        f2c_io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    const F2cIoControl *err_control = f2c_io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    char *unit_value;
    char *iostat_value = NULL;
    char *err_label = NULL;
    if (unit_control == NULL || unit_control->value == NULL)
        return 0;
    unit_value = f2c_io_emit_required_expression(unit, unit_control->value);
    if (iostat_control != NULL && iostat_control->value != NULL)
        iostat_value = f2c_io_emit_required_expression(unit, iostat_control->value);
    if (err_control != NULL && err_control->value != NULL)
        err_label = f2c_io_emit_required_expression(unit, err_control->value);
    if (unit_value == NULL || (iostat_control != NULL && iostat_value == NULL) ||
        (err_control != NULL && err_label == NULL)) {
        free(unit_value);
        free(iostat_value);
        free(err_label);
        return 0;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_io_ok = f2c_close_unit((int32_t)(%s));\n",
                      unit_value);
    if (iostat_value != NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_io_ok ? 0 : 1;\n", iostat_value);
    }
    if (err_label != NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (!f2c_io_ok) goto f2c_label_%s;\n", err_label);
    } else if (iostat_value == NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (!f2c_io_ok) { fputs(\"Fortran CLOSE failed\\n\", stderr); "
                          "abort(); }\n");
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(unit_value);
    free(iostat_value);
    free(err_label);
    return 1;
}
