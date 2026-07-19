#include "codegen/io/control/private.h"

#include <stdlib.h>
#include <string.h>

void f2c_io_free_character_control(F2cEmittedCharacterControl *control) {
    if (control == NULL)
        return;
    free(control->expression);
    free(control->pointer);
    free(control->length);
    memset(control, 0, sizeof(*control));
}

int f2c_io_emit_character_control(Unit *unit, const F2cStatement *statement, F2cIoControlKind kind,
                                  const char *default_pointer, const char *default_length,
                                  F2cEmittedCharacterControl *result) {
    const F2cIoControl *control = f2c_io_control(statement, kind, (size_t)-1);
    memset(result, 0, sizeof(*result));
    if (control == NULL) {
        if (default_pointer == NULL || default_length == NULL)
            return 0;
        result->pointer = f2c_strdup(default_pointer);
        result->length = f2c_strdup(default_length);
    } else if (control->value != NULL) {
        result->expression = f2c_io_emit_required_expression(unit, control->value);
        result->pointer =
            result->expression != NULL
                ? f2c_character_source_pointer(unit, control->value, result->expression)
                : NULL;
        result->length = f2c_character_length_expression(unit, control->value);
    }
    if (result->pointer != NULL && result->length != NULL)
        return 1;
    f2c_io_free_character_control(result);
    return 0;
}

int f2c_io_emit_status_controls(Unit *unit, const F2cStatement *statement,
                                F2cEmittedIoStatus *result) {
    const F2cIoControl *iostat = f2c_io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    const F2cIoControl *iomsg = f2c_io_control(statement, F2C_IO_CONTROL_IOMSG, (size_t)-1);
    const F2cIoControl *err = f2c_io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    memset(result, 0, sizeof(*result));
    if (iostat != NULL && iostat->value != NULL)
        result->iostat = f2c_io_emit_required_expression(unit, iostat->value);
    if (iomsg != NULL && !f2c_io_emit_character_control(unit, statement, F2C_IO_CONTROL_IOMSG, NULL,
                                                        NULL, &result->iomsg))
        goto failure;
    if (err != NULL && err->value != NULL)
        result->err_label = f2c_io_emit_required_expression(unit, err->value);
    if ((iostat == NULL || result->iostat != NULL) && (err == NULL || result->err_label != NULL))
        return 1;

failure:
    f2c_io_free_status_controls(result);
    return 0;
}

void f2c_io_free_status_controls(F2cEmittedIoStatus *status) {
    if (status == NULL)
        return;
    free(status->iostat);
    f2c_io_free_character_control(&status->iomsg);
    free(status->err_label);
    memset(status, 0, sizeof(*status));
}

void f2c_io_emit_control_result(Context *context, const F2cEmittedIoStatus *status,
                                const char *operation, int depth) {
    if (status->iostat != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = f2c_io_ok ? 0 : 1;\n", status->iostat);
    }
    if (status->iomsg.pointer != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_set_iomsg(%s, (size_t)(%s), f2c_io_ok ? 1 : 0);\n",
                          status->iomsg.pointer, status->iomsg.length);
    }
    if (status->err_label != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (!f2c_io_ok) goto f2c_label_%s;\n",
                          f2c_statement_label_canonical(status->err_label));
    } else if (status->iostat == NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (!f2c_io_ok) { fputs(\"Fortran %s failed\\n\", stderr); "
                          "abort(); }\n",
                          operation);
    }
}
