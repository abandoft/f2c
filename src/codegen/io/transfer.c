#include "codegen/io/private.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int character_record_format(const F2cIoControl *format) {
    const char *cursor;
    const char *text = format != NULL && format->value != NULL ? format->value->text : NULL;
    if (text == NULL || format->value->kind != F2C_EXPR_STRING_LITERAL ||
        (*text != '\'' && *text != '"'))
        return 0;
    for (cursor = text + 1; *cursor != '\0' && *cursor != *text; ++cursor) {
        if ((*cursor == 'a' || *cursor == 'A') &&
            (cursor == text + 1 || !isalpha((unsigned char)cursor[-1])))
            return 1;
    }
    return 0;
}

static char *unit_expression(Unit *unit, const F2cStatement *statement, int input,
                             int internal_file) {
    const F2cIoControl *control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    if (internal_file)
        return f2c_strdup("f2c_internal_unit");
    if (control == NULL || control->asterisk)
        return f2c_strdup(input ? "5" : "6");
    return control->value != NULL ? f2c_io_emit_required_expression(unit, control->value) : NULL;
}

static int prepare_internal_file(Unit *unit, const F2cIoControl *control, char **value,
                                 char **pointer, char **length, char **record_count) {
    int supported = 0;
    *value = f2c_emit_expression_ast(unit, control->value, &supported);
    *pointer = supported && *value != NULL
                   ? f2c_character_source_pointer(unit, control->value, *value)
                   : NULL;
    *length = f2c_character_length_expression(unit, control->value);
    *record_count = control->value->rank == 1U && control->value->symbol != NULL
                        ? f2c_symbol_element_count(unit, control->value->symbol)
                        : f2c_strdup("1U");
    return *value != NULL && *pointer != NULL && *length != NULL && *record_count != NULL;
}

static char *prepare_advance(Unit *unit, const F2cIoControl *control, char **value, char **pointer,
                             char **length) {
    Buffer expression = {0};
    if (control == NULL)
        return f2c_strdup("true");
    *value = f2c_io_emit_required_expression(unit, control->value);
    *pointer = *value != NULL ? f2c_character_source_pointer(unit, control->value, *value) : NULL;
    *length = f2c_character_length_expression(unit, control->value);
    if (*pointer == NULL || *length == NULL)
        return NULL;
    f2c_buffer_printf(&expression, "f2c_advance_enabled(%s, (size_t)(%s))", *pointer, *length);
    return f2c_buffer_take(&expression);
}

static void emit_list_transfer(Context *context, Unit *unit, const F2cStatement *statement,
                               const char *stream, const char *unit_number, int input,
                               const char *status, int depth) {
    const F2cIoControl *format = f2c_io_control(statement, F2C_IO_CONTROL_FMT, 1U);
    size_t index;
    if (statement->io_item_count == 1U && statement->io_items[0].implied_do) {
        f2c_io_emit_item(context, unit, stream, &statement->io_items[0], input, status, 0,
                         input ? F2C_DEFINED_IO_READ_FORMATTED : F2C_DEFINED_IO_WRITE_FORMATTED,
                         unit_number, "\"LISTDIRECTED\"", depth);
        return;
    }
    for (index = 0U; index < statement->io_item_count; ++index) {
        f2c_io_emit_item(context, unit, stream, &statement->io_items[index], input, status,
                         input && statement->io_item_count == 1U && character_record_format(format),
                         input ? F2C_DEFINED_IO_READ_FORMATTED : F2C_DEFINED_IO_WRITE_FORMATTED,
                         unit_number, "\"LISTDIRECTED\"", depth);
    }
}

static void emit_transfer_completion(Context *context, const F2cStatement *statement,
                                     const char *stream, int input, int formatted,
                                     int explicit_format, int namelist, const char *advance,
                                     int depth) {
    if (input && formatted && !namelist && !explicit_format &&
        !character_record_format(f2c_io_control(statement, F2C_IO_CONTROL_FMT, 1U))) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if ((%s) && f2c_child_io_depth == 0U) f2c_finish_read(%s);\n", advance,
                          stream);
    } else if (!input && formatted && !namelist && !explicit_format) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if ((%s) && f2c_child_io_depth == 0U) "
                          "(void)f2c_stream_putc('\\n', %s);\n",
                          advance, stream);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_io_status == F2C_IO_STATUS_OK && f2c_stream_error(%s)) "
                      "f2c_io_status = F2C_IO_STATUS_RECORD;\n",
                      stream);
}

static void emit_status_results(Context *context, const char *operation, const char *iostat,
                                const char *iomsg_pointer, const char *iomsg_length,
                                const char *end_label, const char *eor_label, const char *err_label,
                                int depth) {
    if (iostat != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = f2c_io_status_value(f2c_io_status);\n", iostat);
    }
    if (iomsg_pointer != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_set_iomsg(%s, (size_t)(%s), f2c_io_status);\n",
                          iomsg_pointer, iomsg_length);
    }
    if (end_label != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_io_status == EOF) goto f2c_label_%s;\n",
                          f2c_statement_label_canonical(end_label));
    }
    if (eor_label != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_io_status == -2) goto f2c_label_%s;\n",
                          f2c_statement_label_canonical(eor_label));
    }
    if (err_label != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_io_is_error(f2c_io_status)) goto f2c_label_%s;\n",
                          f2c_statement_label_canonical(err_label));
    }
    if (iostat == NULL) {
        if (end_label == NULL) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (f2c_io_status == EOF) f2c_io_abort_unhandled("
                              "f2c_io_status, \"%s\");\n",
                              operation);
        }
        if (eor_label == NULL) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (f2c_io_status == -2) f2c_io_abort_unhandled("
                              "f2c_io_status, \"%s\");\n",
                              operation);
        }
        if (err_label == NULL) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (f2c_io_is_error(f2c_io_status)) f2c_io_abort_unhandled("
                              "f2c_io_status, \"%s\");\n",
                              operation);
        }
    }
}

int f2c_emit_read_write_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int input, int depth) {
    const F2cIoControl *unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const F2cIoControl *record_control = f2c_io_control(statement, F2C_IO_CONTROL_REC, (size_t)-1);
    const F2cIoControl *format_control = f2c_io_control(statement, F2C_IO_CONTROL_FMT, 1U);
    const F2cIoControl *namelist_control =
        f2c_io_control(statement, F2C_IO_CONTROL_NML, (size_t)-1);
    const F2cIoControl *end_control =
        input ? f2c_io_control(statement, F2C_IO_CONTROL_END, (size_t)-1) : NULL;
    const F2cIoControl *eor_control =
        input ? f2c_io_control(statement, F2C_IO_CONTROL_EOR, (size_t)-1) : NULL;
    const F2cIoControl *err_control = f2c_io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    const F2cIoControl *iostat_control =
        f2c_io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    const F2cIoControl *iomsg_control = f2c_io_control(statement, F2C_IO_CONTROL_IOMSG, (size_t)-1);
    const F2cIoControl *size_control =
        input ? f2c_io_control(statement, F2C_IO_CONTROL_SIZE, (size_t)-1) : NULL;
    const F2cIoControl *advance_control =
        f2c_io_control(statement, F2C_IO_CONTROL_ADVANCE, (size_t)-1);
    F2cNamelistGroup *namelist_group = NULL;
    const int internal_file = unit_control != NULL && !unit_control->asterisk &&
                              unit_control->value != NULL &&
                              unit_control->value->type == TYPE_CHARACTER;
    const int formatted = format_control != NULL || namelist_control != NULL;
    const int explicit_format = format_control != NULL && !format_control->asterisk;
    const int list_directed = format_control != NULL && format_control->asterisk;
    char *unit_value = NULL;
    char *record_value = NULL;
    char *end_label = NULL;
    char *eor_label = NULL;
    char *err_label = NULL;
    char *iostat = NULL;
    char *iomsg_value = NULL;
    char *iomsg_pointer = NULL;
    char *iomsg_length = NULL;
    char *size_value = NULL;
    char *advance_value = NULL;
    char *advance_pointer = NULL;
    char *advance_length = NULL;
    char *advance_expression = NULL;
    char *internal_value = NULL;
    char *internal_pointer = NULL;
    char *internal_length = NULL;
    char *internal_record_count = NULL;
    size_t index;
    int result = 0;
    if (statement->io_controls == NULL || statement->control_count == 0U ||
        statement->io_item_count != statement->item_count)
        goto cleanup;
    unit_value = unit_expression(unit, statement, input, internal_file);
    if (unit_value == NULL)
        goto cleanup;
    if (record_control != NULL) {
        record_value = f2c_io_emit_required_expression(unit, record_control->value);
        if (record_value == NULL)
            goto cleanup;
    }
    if (internal_file &&
        !prepare_internal_file(unit, unit_control, &internal_value, &internal_pointer,
                               &internal_length, &internal_record_count))
        goto cleanup;
    if (end_control != NULL)
        end_label = f2c_io_emit_required_expression(unit, end_control->value);
    if (eor_control != NULL)
        eor_label = f2c_io_emit_required_expression(unit, eor_control->value);
    if (err_control != NULL)
        err_label = f2c_io_emit_required_expression(unit, err_control->value);
    if (iostat_control != NULL)
        iostat = f2c_io_emit_required_expression(unit, iostat_control->value);
    if (iomsg_control != NULL) {
        iomsg_value = f2c_io_emit_required_expression(unit, iomsg_control->value);
        iomsg_pointer = iomsg_value != NULL
                            ? f2c_character_source_pointer(unit, iomsg_control->value, iomsg_value)
                            : NULL;
        iomsg_length = f2c_character_length_expression(unit, iomsg_control->value);
    }
    if (size_control != NULL)
        size_value = f2c_io_emit_required_expression(unit, size_control->value);
    advance_expression =
        prepare_advance(unit, advance_control, &advance_value, &advance_pointer, &advance_length);
    if ((end_control != NULL && end_label == NULL) || (eor_control != NULL && eor_label == NULL) ||
        (err_control != NULL && err_label == NULL) || (iostat_control != NULL && iostat == NULL) ||
        (iomsg_control != NULL &&
         (iomsg_value == NULL || iomsg_pointer == NULL || iomsg_length == NULL)) ||
        (size_control != NULL && size_value == NULL) || advance_expression == NULL)
        goto cleanup;
    if (namelist_control != NULL && namelist_control->value != NULL)
        namelist_group = f2c_find_namelist(unit, namelist_control->value->text);

    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    ++depth;
    if (internal_file) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "f2c_io_stream f2c_internal_stream;\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (!f2c_stream_initialize_internal(&f2c_internal_stream, %s, "
                          "(size_t)(%s), (size_t)(%s), %s)) abort();\n",
                          internal_pointer, internal_length, internal_record_count,
                          input ? "true" : "false");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "const int32_t f2c_internal_unit = "
                                            "f2c_register_internal_unit(&f2c_internal_stream);\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "const int32_t f2c_io_unit = f2c_internal_unit;\n"
                                            "(void)f2c_io_unit;\n"
                                            "int f2c_io_status = F2C_IO_STATUS_OK;\n"
                                            "f2c_io_stream *f2c_io_file = &f2c_internal_stream;\n");
    } else {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const int32_t f2c_io_unit = (int32_t)(%s);\n",
                          unit_value);
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "(void)f2c_io_unit;\n");
        if (record_value != NULL) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "const int64_t f2c_io_record = (int64_t)(%s);\n",
                              record_value);
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "f2c_io_transfer f2c_io_transfer_state;\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "int f2c_io_status = f2c_transfer_begin(&f2c_io_transfer_state, "
                          "f2c_io_unit, %s, %s, %s, %s);\n",
                          input ? "true" : "false", formatted ? "true" : "false",
                          record_value != NULL ? "true" : "false",
                          record_value != NULL ? "f2c_io_record" : "INT64_C(0)");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "f2c_io_stream *f2c_io_file = f2c_io_status == F2C_IO_STATUS_OK ? "
                          "f2c_io_transfer_state.stream : NULL;\n");
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_io_status == F2C_IO_STATUS_OK) {\n");
    ++depth;
    if (namelist_group != NULL) {
        if (!f2c_io_emit_namelist(context, unit, "f2c_io_file", namelist_group, input,
                                  "f2c_io_unit", depth))
            goto cleanup;
    } else if (explicit_format) {
        if (!f2c_io_emit_formatted_transfer(context, unit, statement, format_control, "f2c_io_file",
                                            "f2c_io_unit", input, advance_expression, size_value,
                                            "f2c_io_status", depth))
            goto cleanup;
    } else if (!formatted) {
        for (index = 0U; index < statement->io_item_count; ++index)
            if (!f2c_io_emit_unformatted_item(context, unit, &statement->io_items[index], input,
                                              "f2c_io_file", "f2c_io_unit", "f2c_io_status", depth))
                goto cleanup;
    } else if (list_directed) {
        emit_list_transfer(context, unit, statement, "f2c_io_file", "f2c_io_unit", input,
                           "f2c_io_status", depth);
    }
    emit_transfer_completion(context, statement, "f2c_io_file", input, formatted, explicit_format,
                             namelist_group != NULL, advance_expression, depth);
    --depth;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    if (internal_file) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "f2c_unregister_internal_unit(f2c_internal_unit);\n");
    } else {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output,
                          "f2c_io_status = f2c_transfer_end(&f2c_io_transfer_state, "
                          "f2c_io_status);\n");
    }
    emit_status_results(context, input ? "READ" : "WRITE", iostat, iomsg_pointer, iomsg_length,
                        end_label, eor_label, err_label, depth);
    --depth;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    free(unit_value);
    free(record_value);
    free(end_label);
    free(eor_label);
    free(err_label);
    free(iostat);
    free(iomsg_value);
    free(iomsg_pointer);
    free(iomsg_length);
    free(size_value);
    free(advance_value);
    free(advance_pointer);
    free(advance_length);
    free(advance_expression);
    free(internal_value);
    free(internal_pointer);
    free(internal_length);
    free(internal_record_count);
    return result;
}
