#include "codegen/io/private.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void f2c_io_indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

char *f2c_io_emit_item_expression(Unit *unit, const F2cIoItem *item) {
    int supported = 0;
    char *result = item->expression != NULL
                       ? f2c_emit_expression_ast(unit, item->expression, &supported)
                       : NULL;
    if (!supported || result == NULL) {
        free(result);
        return NULL;
    }
    return result;
}

char *f2c_io_emit_required_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

char *f2c_io_c_string_literal(const char *text, size_t length) {
    Buffer result = {0};
    size_t i;
    f2c_buffer_append(&result, "\"");
    for (i = 0U; i < length; ++i) {
        const unsigned char value = (unsigned char)text[i];
        if (value == '\\' || value == '"') {
            const char escape[2] = {'\\', (char)value};
            f2c_buffer_append_n(&result, escape, sizeof(escape));
        } else if (value == '\n') {
            f2c_buffer_append(&result, "\\n");
        } else if (value == '\r') {
            f2c_buffer_append(&result, "\\r");
        } else if (value == '\t') {
            f2c_buffer_append(&result, "\\t");
        } else if (isprint(value)) {
            const char character = (char)value;
            f2c_buffer_append_n(&result, &character, 1U);
        } else {
            f2c_buffer_printf(&result, "\\%03o", (unsigned)value);
        }
    }
    f2c_buffer_append(&result, "\"");
    return f2c_buffer_take(&result);
}

static char *unquote_fortran_string(const char *text, size_t *length_out) {
    Buffer result = {0};
    const size_t length = text != NULL ? strlen(text) : 0U;
    const char quote = length != 0U ? text[0] : '\0';
    size_t i;
    *length_out = 0U;
    if (length < 2U || (quote != '\'' && quote != '"') || text[length - 1U] != quote)
        return NULL;
    for (i = 1U; i + 1U < length; ++i) {
        if (text[i] == quote && i + 2U < length && text[i + 1U] == quote)
            ++i;
        f2c_buffer_append_n(&result, &text[i], 1U);
    }
    *length_out = result.length;
    return f2c_buffer_take(&result);
}

static char *labeled_format(Context *context, Unit *unit, const char *label, size_t *length_out) {
    size_t line_index;
    for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
        const char *cursor = f2c_trim(context->lines.items[line_index].text);
        const char *label_end = cursor;
        while (isdigit((unsigned char)*label_end))
            ++label_end;
        if (label_end == cursor || (size_t)(label_end - cursor) != strlen(label) ||
            strncmp(cursor, label, strlen(label)) != 0)
            continue;
        cursor = f2c_trim((char *)label_end);
        if (!f2c_starts_word(cursor, "format"))
            continue;
        cursor = f2c_trim((char *)cursor + strlen("format"));
        *length_out = strlen(cursor);
        return f2c_strdup(cursor);
    }
    return NULL;
}

static char *constant_format(Context *context, Unit *unit, const F2cIoControl *control,
                             size_t *length_out) {
    const F2cExpr *value = control != NULL ? control->value : NULL;
    *length_out = 0U;
    if (value == NULL)
        return NULL;
    if (value->kind == F2C_EXPR_STRING_LITERAL)
        return unquote_fortran_string(value->text, length_out);
    if (value->kind == F2C_EXPR_INTEGER_LITERAL)
        return labeled_format(context, unit, value->text, length_out);
    return NULL;
}

static F2cTypeBinding *find_type_binding(F2cDerivedType *derived, const char *name) {
    size_t index;
    if (derived == NULL || name == NULL)
        return NULL;
    for (index = 0U; index < derived->binding_count; ++index)
        if (strcmp(derived->bindings[index].name, name) == 0)
            return &derived->bindings[index];
    return find_type_binding(derived->parent, name);
}

F2cTypeBinding *f2c_io_defined_binding(F2cDerivedType *derived, F2cDefinedIoKind kind) {
    F2cDerivedType *owner;
    for (owner = derived; owner != NULL; owner = owner->parent) {
        const char *name = owner->defined_io_bindings[kind];
        if (name != NULL)
            return find_type_binding(derived, name);
    }
    return NULL;
}

static char *defined_io_designator(const char *value, F2cDerivedType *derived,
                                   const F2cTypeBinding *binding) {
    Buffer result = {0};
    F2cDerivedType *owner = derived;
    if (value == NULL || derived == NULL || binding == NULL || binding->storage_owner == NULL)
        return NULL;
    f2c_buffer_printf(&result, "(%s)", value);
    while (owner != NULL && owner != binding->storage_owner) {
        f2c_buffer_append(&result, ".parent");
        owner = owner->parent;
    }
    if (owner == NULL) {
        free(result.data);
        return NULL;
    }
    f2c_buffer_printf(&result, ".%s", binding->name);
    return f2c_buffer_take(&result);
}

int f2c_io_emit_defined_io_call(Context *context, const char *value, F2cDerivedType *derived,
                                F2cDefinedIoKind kind, const char *unit_number, const char *iotype,
                                const char *v_list, const char *v_list_count, const char *status,
                                int depth) {
    F2cTypeBinding *binding = f2c_io_defined_binding(derived, kind);
    char *callee;
    const int formatted =
        kind == F2C_DEFINED_IO_READ_FORMATTED || kind == F2C_DEFINED_IO_WRITE_FORMATTED;
    if (binding == NULL)
        return 0;
    callee = defined_io_designator(value, derived, binding);
    if (callee == NULL)
        return 0;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const int32_t f2c_dtio_unit = (int32_t)(%s); "
                      "int32_t f2c_dtio_iostat = 0; char f2c_dtio_iomsg[256]; "
                      "memset(f2c_dtio_iomsg, ' ', sizeof(f2c_dtio_iomsg));\n",
                      unit_number != NULL ? unit_number : "0");
    if (formatted) {
        f2c_io_indent(&context->output, depth + 1);
        if (v_list == NULL)
            f2c_buffer_append(&context->output, "const int32_t f2c_dtio_empty_v_list[1] = {0}; ");
        f2c_buffer_printf(&context->output,
                          "const int32_t *f2c_dtio_v_list = %s; "
                          "const size_t f2c_dtio_v_list_count = (size_t)(%s);\n",
                          v_list != NULL ? v_list : "f2c_dtio_empty_v_list",
                          v_list_count != NULL ? v_list_count : "0U");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "f2c_descriptor f2c_dtio_v_list_descriptor = {"
                          ".data = f2c_implicit_mutable_actual(f2c_dtio_v_list), "
                          ".element_size = sizeof(*f2c_dtio_v_list), .rank = 1U};\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "f2c_dtio_v_list_descriptor.lower[0] = 1; "
                                            "f2c_dtio_v_list_descriptor.extent[0] = "
                                            "(int64_t)f2c_dtio_v_list_count; "
                                            "f2c_dtio_v_list_descriptor.stride[0] = 1;\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "++f2c_child_io_depth;\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "%s((void *)&(%s), &f2c_dtio_unit, %s, "
                          "&f2c_dtio_v_list_descriptor, "
                          "&f2c_dtio_iostat, f2c_dtio_iomsg, strlen(%s), "
                          "sizeof(f2c_dtio_iomsg));\n",
                          callee, value, iotype != NULL ? iotype : "\"LISTDIRECTED\"",
                          iotype != NULL ? iotype : "\"LISTDIRECTED\"");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "--f2c_child_io_depth;\n");
    } else {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "++f2c_child_io_depth;\n");
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "%s((void *)&(%s), &f2c_dtio_unit, &f2c_dtio_iostat, "
                          "f2c_dtio_iomsg, sizeof(f2c_dtio_iomsg));\n",
                          callee, value);
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "--f2c_child_io_depth;\n");
    }
    if (status != NULL) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (f2c_dtio_iostat != 0) %s = 0;\n", status);
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(callee);
    return 1;
}

void f2c_io_emit_namelist_value(Context *context, Unit *unit, const char *file,
                                const Symbol *symbol, const char *value,
                                const char *character_length_override, int input, int depth);

void f2c_io_emit_item(Context *context, Unit *unit, const char *file, const F2cIoItem *item,
                      int input, const char *status, int record_input,
                      F2cDefinedIoKind defined_kind, const char *unit_number, const char *iotype,
                      int depth) {
    const F2cExpr *expression;
    Symbol *symbol;
    int simple_name;
    if (item == NULL)
        return;
    if (item->implied_do) {
        size_t i;
        char *variable = f2c_io_emit_required_expression(unit, item->iterator);
        char *start = f2c_io_emit_required_expression(unit, item->initial);
        char *finish = f2c_io_emit_required_expression(unit, item->limit);
        char *step = f2c_io_emit_required_expression(unit, item->step);
        if (variable != NULL && start != NULL && finish != NULL && step != NULL) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (%s = %s; ((%s) >= 0 ? %s <= %s : %s >= %s); %s += %s) "
                              "{\n",
                              variable, start, step, variable, finish, variable, finish, variable,
                              step);
            for (i = 0U; i < item->child_count; ++i)
                f2c_io_emit_item(context, unit, file, &item->children[i], input, status, 0,
                                 defined_kind, unit_number, iotype, depth + 1);
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        free(variable);
        free(start);
        free(finish);
        free(step);
        return;
    }
    expression = item->expression;
    symbol = expression != NULL ? expression->symbol : NULL;
    simple_name = expression != NULL && expression->kind == F2C_EXPR_NAME;
    if (simple_name && symbol != NULL && symbol->type == TYPE_DERIVED &&
        symbol->derived_type != NULL) {
        const char *name = f2c_symbol_c_name(unit, symbol);
        if (symbol->rank == 0U) {
            if (!input && iotype != NULL && strcmp(iotype, "\"LISTDIRECTED\"") == 0) {
                f2c_io_indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "fputc(' ', %s);\n", file);
            }
            if (!f2c_io_emit_defined_io_call(context, name, symbol->derived_type, defined_kind,
                                             unit_number, iotype, NULL, "0U", status, depth)) {
                f2c_io_indent(&context->output, depth);
                f2c_buffer_append(&context->output, "abort(); /* missing defined I/O */\n");
            }
        } else {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_dtio_index = 0U; f2c_dtio_index < %s; "
                              "++f2c_dtio_index) {\n",
                              count != NULL ? count : "0U");
            {
                Buffer value = {0};
                f2c_buffer_printf(&value, "%s[f2c_dtio_index]", name);
                if (!input && iotype != NULL && strcmp(iotype, "\"LISTDIRECTED\"") == 0) {
                    f2c_io_indent(&context->output, depth + 1);
                    f2c_buffer_printf(&context->output, "fputc(' ', %s);\n", file);
                }
                if (!f2c_io_emit_defined_io_call(context, value.data, symbol->derived_type,
                                                 defined_kind, unit_number, iotype, NULL, "0U",
                                                 status, depth + 1)) {
                    f2c_io_indent(&context->output, depth + 1);
                    f2c_buffer_append(&context->output, "abort(); /* missing defined I/O */\n");
                }
                free(value.data);
            }
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(count);
        }
        return;
    }
    if (input) {
        if (simple_name && symbol != NULL && symbol->rank != 0U) {
            char *element_count = f2c_symbol_element_count(unit, symbol);
            char *character_length =
                symbol->type == TYPE_CHARACTER ? f2c_symbol_character_length(unit, symbol) : NULL;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_io_index = 0U; f2c_io_index < %s; "
                              "++f2c_io_index) {\n",
                              element_count != NULL ? element_count : "0U");
            {
                Buffer value = {0};
                if (symbol->type == TYPE_CHARACTER)
                    f2c_buffer_printf(&value, "%s + f2c_io_index * (size_t)(%s)",
                                      f2c_symbol_c_name(unit, symbol),
                                      character_length != NULL ? character_length : "1U");
                else
                    f2c_buffer_printf(&value, "%s[f2c_io_index]", f2c_symbol_c_name(unit, symbol));
                f2c_io_emit_namelist_value(context, unit, file, symbol,
                                           value.data != NULL ? value.data
                                                              : f2c_symbol_c_name(unit, symbol),
                                           NULL, 1, depth + 1);
                free(value.data);
            }
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(element_count);
            free(character_length);
        } else if (expression != NULL && expression->type == TYPE_LOGICAL) {
            char *value = f2c_io_emit_item_expression(unit, item);
            if (value == NULL)
                return;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{ bool f2c_io_logical = false; ");
            if (status != NULL)
                f2c_buffer_printf(&context->output,
                                  "%s = f2c_read_bool(%s, &f2c_io_logical); if (%s > 0) %s = "
                                  "f2c_io_logical ? 1 : 0; }\n",
                                  status, file, status, value);
            else
                f2c_buffer_printf(&context->output,
                                  "if (f2c_read_bool(%s, &f2c_io_logical) > 0) %s = "
                                  "f2c_io_logical ? 1 : 0; }\n",
                                  file, value);
            free(value);
        } else if (simple_name && symbol != NULL && symbol->type == TYPE_CHARACTER &&
                   (symbol->argument || symbol->character_length != NULL || symbol->rank != 0U)) {
            f2c_io_indent(&context->output, depth);
            if (record_input && !symbol->argument) {
                if (status != NULL)
                    f2c_buffer_printf(
                        &context->output, "%s = f2c_read_record(%s, %s, sizeof(%s));\n", status,
                        file, f2c_symbol_c_name(unit, symbol), f2c_symbol_c_name(unit, symbol));
                else
                    f2c_buffer_printf(
                        &context->output, "(void)f2c_read_record(%s, %s, sizeof(%s));\n", file,
                        f2c_symbol_c_name(unit, symbol), f2c_symbol_c_name(unit, symbol));
            } else {
                char *length = f2c_symbol_character_length(unit, symbol);
                if (status != NULL)
                    f2c_buffer_printf(&context->output,
                                      "%s = f2c_read_character(%s, %s, (size_t)(%s));\n", status,
                                      file, f2c_symbol_c_name(unit, symbol),
                                      length != NULL ? length : "1U");
                else
                    f2c_buffer_printf(
                        &context->output, "(void)f2c_read_character(%s, %s, (size_t)(%s));\n", file,
                        f2c_symbol_c_name(unit, symbol), length != NULL ? length : "1U");
                free(length);
            }
        } else {
            char *value = f2c_io_emit_item_expression(unit, item);
            if (value == NULL)
                return;
            f2c_io_indent(&context->output, depth);
            if (status != NULL)
                f2c_buffer_printf(&context->output, "%s = F2C_READ(%s, &%s);\n", status, file,
                                  value);
            else
                f2c_buffer_printf(&context->output, "(void)F2C_READ(%s, &%s);\n", file, value);
            free(value);
        }
    } else {
        if (expression != NULL && expression->type == TYPE_LOGICAL && expression->rank == 0U) {
            char *value = f2c_io_emit_item_expression(unit, item);
            if (value != NULL) {
                f2c_io_indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "f2c_write_bool(%s, (%s) != 0);\n", file,
                                  value);
            }
            free(value);
        } else if (expression != NULL && expression->type == TYPE_CHARACTER &&
                   expression->rank == 0U) {
            char *value = f2c_io_emit_item_expression(unit, item);
            char *length = f2c_character_length_expression(unit, expression);
            char *pointer =
                value != NULL ? f2c_character_source_pointer(unit, expression, value) : NULL;
            if (value != NULL && length != NULL && pointer != NULL) {
                f2c_io_indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "f2c_write_character(%s, %s, (size_t)(%s));\n",
                                  file, pointer, length);
            }
            free(value);
            free(length);
            free(pointer);
        } else if (simple_name && symbol != NULL && symbol->rank != 0U) {
            char *element_count = f2c_symbol_element_count(unit, symbol);
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{\n");
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "size_t f2c_io_index; for (f2c_io_index = 0; f2c_io_index < %s; "
                              "++f2c_io_index) {\n",
                              element_count);
            f2c_io_indent(&context->output, depth + 2);
            if (symbol->type == TYPE_LOGICAL)
                f2c_buffer_printf(&context->output, "f2c_write_bool(%s, %s[f2c_io_index] != 0);\n",
                                  file, f2c_symbol_c_name(unit, symbol));
            else
                f2c_buffer_printf(&context->output, "F2C_WRITE(%s, (%s[f2c_io_index]));\n", file,
                                  f2c_symbol_c_name(unit, symbol));
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "}\n");
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(element_count);
        } else {
            char *value;
            if (simple_name && symbol != NULL && symbol->type == TYPE_CHARACTER &&
                (symbol->argument || symbol->character_length != NULL || symbol->rank != 0U))
                value = f2c_strdup(f2c_symbol_c_name(unit, symbol));
            else
                value = f2c_io_emit_item_expression(unit, item);
            if (value == NULL)
                return;
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "F2C_WRITE(%s, (%s));\n", file, value);
            free(value);
        }
    }
}

const F2cIoControl *f2c_io_control(const F2cStatement *statement, F2cIoControlKind kind,
                                   size_t positional) {
    size_t i;
    for (i = 0U; i < statement->control_count; ++i) {
        const F2cIoControl *control = &statement->io_controls[i];
        if (control->kind == kind)
            return control;
        if (control->kind == F2C_IO_CONTROL_POSITIONAL && i == positional)
            return control;
    }
    return NULL;
}

static int io_character_record_format(const F2cIoControl *format) {
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

static char *io_file_expression(Unit *unit, const F2cStatement *statement, int input) {
    const F2cIoControl *control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    char *unit_value;
    Buffer result = {0};
    if (control == NULL)
        return f2c_strdup(input ? "stdin" : "stdout");
    if (control->asterisk) {
        unit_value = f2c_strdup(input ? "stdin" : "stdout");
    } else if (control->value != NULL && control->value->type == TYPE_CHARACTER) {
        unit_value = f2c_strdup("f2c_internal_file");
    } else {
        char *translated = f2c_io_emit_required_expression(unit, control->value);
        if (translated == NULL)
            return NULL;
        f2c_buffer_printf(&result, "f2c_unit_file((int32_t)(%s), %s)", translated,
                          input ? "true" : "false");
        free(translated);
        unit_value = f2c_buffer_take(&result);
    }
    return unit_value;
}

static char *io_unit_number_expression(Unit *unit, const F2cStatement *statement, int input,
                                       int internal_file) {
    const F2cIoControl *control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    if (internal_file)
        return f2c_strdup("f2c_internal_unit");
    if (control == NULL || control->asterisk)
        return f2c_strdup(input ? "5" : "6");
    return control->value != NULL ? f2c_io_emit_required_expression(unit, control->value) : NULL;
}

int f2c_emit_read_write_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  int input, int depth) {
    char *file;
    char *unit_number;
    const F2cIoControl *end_control = NULL;
    const F2cIoControl *err_control = NULL;
    const F2cIoControl *iostat_control = NULL;
    const F2cIoControl *eor_control = NULL;
    const F2cIoControl *iomsg_control = NULL;
    const F2cIoControl *size_control = NULL;
    const F2cIoControl *advance_control = NULL;
    const F2cIoControl *format_control = NULL;
    const F2cIoControl *namelist_control = NULL;
    F2cNamelistGroup *namelist_group = NULL;
    char *end_label = NULL;
    char *err_label = NULL;
    char *iostat = NULL;
    char *eor_label = NULL;
    char *iomsg_value = NULL;
    char *iomsg_pointer = NULL;
    char *iomsg_length = NULL;
    char *size_value = NULL;
    char *advance_value = NULL;
    char *advance_pointer = NULL;
    char *advance_length = NULL;
    char *advance_expression = NULL;
    const F2cIoControl *unit_control;
    const int internal_file =
        (unit_control = f2c_io_control(statement, F2C_IO_CONTROL_UNIT, 0U)) != NULL &&
        !unit_control->asterisk && unit_control->value != NULL &&
        unit_control->value->type == TYPE_CHARACTER;
    char *internal_value = NULL;
    char *internal_pointer = NULL;
    char *internal_length = NULL;
    char *internal_record_count = NULL;
    char *format_text = NULL;
    char *format_literal = NULL;
    char *format_value = NULL;
    char *format_pointer = NULL;
    char *format_length_expression = NULL;
    size_t format_length = 0U;
    char constant_format_length[64];
    int formatted = 0;
    int list_directed = 0;
    int needs_status = 0;
    size_t i;
    if (statement->io_controls == NULL || statement->control_count == 0U ||
        statement->io_item_count != statement->item_count)
        return 0;
    file = io_file_expression(unit, statement, input);
    if (file == NULL)
        return 0;
    unit_number = io_unit_number_expression(unit, statement, input, internal_file);
    if (unit_number == NULL) {
        free(file);
        return 0;
    }
    if (internal_file) {
        int supported = 0;
        internal_value = f2c_emit_expression_ast(unit, unit_control->value, &supported);
        internal_pointer =
            supported && internal_value != NULL
                ? f2c_character_source_pointer(unit, unit_control->value, internal_value)
                : NULL;
        internal_length = f2c_character_length_expression(unit, unit_control->value);
        internal_record_count =
            unit_control->value->rank == 1U && unit_control->value->symbol != NULL
                ? f2c_symbol_element_count(unit, unit_control->value->symbol)
                : f2c_strdup("1U");
        if (internal_value == NULL || internal_pointer == NULL || internal_length == NULL ||
            internal_record_count == NULL) {
            free(file);
            free(internal_value);
            free(internal_pointer);
            free(internal_length);
            free(internal_record_count);
            return 0;
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        ++depth;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "FILE *f2c_internal_file = tmpfile();\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "if (f2c_internal_file == NULL) abort();\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(
            &context->output,
            "const int32_t f2c_internal_unit = f2c_register_internal_unit(f2c_internal_file);\n");
        if (input) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_internal_record = 0U; f2c_internal_record < "
                              "(size_t)(%s); ++f2c_internal_record) {\n",
                              internal_record_count);
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if ((size_t)(%s) != 0U) (void)fwrite(%s + "
                              "f2c_internal_record * (size_t)(%s), 1U, (size_t)(%s), "
                              "f2c_internal_file);\n",
                              internal_length, internal_pointer, internal_length, internal_length);
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "fputc('\\n', f2c_internal_file);\n");
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "rewind(f2c_internal_file);\n");
        }
    }
    end_control = input ? f2c_io_control(statement, F2C_IO_CONTROL_END, (size_t)-1) : NULL;
    eor_control = input ? f2c_io_control(statement, F2C_IO_CONTROL_EOR, (size_t)-1) : NULL;
    err_control = f2c_io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    iostat_control = f2c_io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    iomsg_control = f2c_io_control(statement, F2C_IO_CONTROL_IOMSG, (size_t)-1);
    size_control = input ? f2c_io_control(statement, F2C_IO_CONTROL_SIZE, (size_t)-1) : NULL;
    advance_control = f2c_io_control(statement, F2C_IO_CONTROL_ADVANCE, (size_t)-1);
    if (input)
        end_label =
            end_control != NULL ? f2c_io_emit_required_expression(unit, end_control->value) : NULL;
    if (input)
        eor_label =
            eor_control != NULL ? f2c_io_emit_required_expression(unit, eor_control->value) : NULL;
    err_label =
        err_control != NULL ? f2c_io_emit_required_expression(unit, err_control->value) : NULL;
    iostat = iostat_control != NULL ? f2c_io_emit_required_expression(unit, iostat_control->value)
                                    : NULL;
    if (iomsg_control != NULL && iomsg_control->value != NULL) {
        iomsg_value = f2c_io_emit_required_expression(unit, iomsg_control->value);
        iomsg_pointer = iomsg_value != NULL
                            ? f2c_character_source_pointer(unit, iomsg_control->value, iomsg_value)
                            : NULL;
        iomsg_length = f2c_character_length_expression(unit, iomsg_control->value);
    }
    if (size_control != NULL)
        size_value = f2c_io_emit_required_expression(unit, size_control->value);
    if (advance_control != NULL && advance_control->value != NULL) {
        Buffer advance = {0};
        advance_value = f2c_io_emit_required_expression(unit, advance_control->value);
        advance_pointer =
            advance_value != NULL
                ? f2c_character_source_pointer(unit, advance_control->value, advance_value)
                : NULL;
        advance_length = f2c_character_length_expression(unit, advance_control->value);
        if (advance_pointer != NULL && advance_length != NULL)
            f2c_buffer_printf(&advance, "f2c_advance_enabled(%s, (size_t)(%s))", advance_pointer,
                              advance_length);
        advance_expression = f2c_buffer_take(&advance);
    } else {
        advance_expression = f2c_strdup("true");
    }
    if (advance_expression == NULL || (eor_control != NULL && eor_label == NULL) ||
        (iomsg_control != NULL &&
         (iomsg_value == NULL || iomsg_pointer == NULL || iomsg_length == NULL)) ||
        (size_control != NULL && size_value == NULL)) {
        free(file);
        free(eor_label);
        free(iomsg_value);
        free(iomsg_pointer);
        free(iomsg_length);
        free(size_value);
        free(advance_value);
        free(advance_pointer);
        free(advance_length);
        free(advance_expression);
        return 0;
    }
    needs_status = end_label != NULL || eor_label != NULL || err_label != NULL || iostat != NULL ||
                   iomsg_pointer != NULL;
    format_control = f2c_io_control(statement, F2C_IO_CONTROL_FMT, 1U);
    formatted = format_control != NULL && !format_control->asterisk;
    list_directed = format_control != NULL && format_control->asterisk;
    namelist_control = f2c_io_control(statement, F2C_IO_CONTROL_NML, (size_t)-1);
    if (namelist_control != NULL && namelist_control->value != NULL)
        namelist_group = f2c_find_namelist(unit, namelist_control->value->text);
    if (needs_status) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        ++depth;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "int f2c_io_status = 1;\n");
    }
    if (formatted) {
        format_text = constant_format(context, unit, format_control, &format_length);
        if (format_text != NULL) {
            format_literal = f2c_io_c_string_literal(format_text, format_length);
        } else if (format_control->value != NULL && format_control->value->type == TYPE_CHARACTER) {
            format_value = f2c_io_emit_required_expression(unit, format_control->value);
            format_pointer =
                format_value != NULL
                    ? f2c_character_source_pointer(unit, format_control->value, format_value)
                    : NULL;
            format_length_expression = f2c_character_length_expression(unit, format_control->value);
        }
        if ((format_literal == NULL &&
             (format_pointer == NULL || format_length_expression == NULL)) ||
            (format_literal != NULL && format_length == 0U)) {
            f2c_diagnostic(context, statement->line, 1,
                           "FORMAT label or CHARACTER expression could not be resolved");
            free(file);
            free(format_text);
            free(format_literal);
            free(format_value);
            free(format_pointer);
            free(format_length_expression);
            return 0;
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        ++depth;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "f2c_format_state f2c_io_format;\n");
        f2c_io_indent(&context->output, depth);
        (void)snprintf(constant_format_length, sizeof(constant_format_length), "%zuU",
                       format_length);
        f2c_buffer_printf(
            &context->output, "f2c_format_initialize(&f2c_io_format, %s, %s, (size_t)(%s), %s);\n",
            file, format_literal != NULL ? format_literal : format_pointer,
            format_literal != NULL ? constant_format_length : format_length_expression,
            input ? "true" : "false");
    }
    if (namelist_group != NULL) {
        if (!f2c_io_emit_namelist(context, unit, file, namelist_group, input, unit_number, depth)) {
            free(file);
            return 0;
        }
    } else if (formatted) {
        for (i = 0U; i < statement->io_item_count; ++i)
            f2c_io_emit_formatted_item(context, unit, &statement->io_items[i], input, unit_number,
                                       depth);
    } else if (statement->io_item_count == 1U && statement->io_items[0].implied_do) {
        f2c_io_emit_item(context, unit, file, &statement->io_items[0], input,
                         input && needs_status ? "f2c_io_status" : NULL, 0,
                         input ? (list_directed ? F2C_DEFINED_IO_READ_FORMATTED
                                                : F2C_DEFINED_IO_READ_UNFORMATTED)
                               : (list_directed ? F2C_DEFINED_IO_WRITE_FORMATTED
                                                : F2C_DEFINED_IO_WRITE_UNFORMATTED),
                         unit_number, list_directed ? "\"LISTDIRECTED\"" : NULL, depth);
    } else {
        for (i = 0U; i < statement->io_item_count; ++i)
            f2c_io_emit_item(context, unit, file, &statement->io_items[i], input,
                             input && needs_status ? "f2c_io_status" : NULL,
                             input && statement->io_item_count == 1U &&
                                 io_character_record_format(format_control),
                             input ? (list_directed ? F2C_DEFINED_IO_READ_FORMATTED
                                                    : F2C_DEFINED_IO_READ_UNFORMATTED)
                                   : (list_directed ? F2C_DEFINED_IO_WRITE_FORMATTED
                                                    : F2C_DEFINED_IO_WRITE_UNFORMATTED),
                             unit_number, list_directed ? "\"LISTDIRECTED\"" : NULL, depth);
    }
    if (formatted) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_format_finish(&f2c_io_format, %s);\n",
                          advance_expression);
        if (size_value != NULL) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = (int32_t)f2c_io_format.transferred;\n",
                              size_value);
        }
        if (needs_status) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "f2c_io_status = f2c_io_format.status;\n");
        }
        --depth;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    if (input) {
        if (namelist_group == NULL && !formatted && !io_character_record_format(format_control)) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if ((%s) && f2c_child_io_depth == 0U) f2c_finish_read(%s);\n",
                              advance_expression, file);
        }
    } else if (namelist_group == NULL && !formatted) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if ((%s) && f2c_child_io_depth == 0U) fputc('\\n', %s);\n",
                          advance_expression, file);
    }
    if (!input && needs_status) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (ferror(%s)) f2c_io_status = 0;\n", file);
    }
    if (iostat != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "%s = f2c_io_status == EOF ? -1 : (f2c_io_status == -2 ? -2 : "
                          "(f2c_io_status <= 0 ? 1 : 0));\n",
                          iostat);
    }
    if (iomsg_pointer != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_set_iomsg(%s, (size_t)(%s), f2c_io_status);\n",
                          iomsg_pointer, iomsg_length);
    }
    if (end_label != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_io_status == EOF) goto f2c_label_%s;\n",
                          end_label);
    }
    if (eor_label != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_io_status == -2) goto f2c_label_%s;\n",
                          eor_label);
    }
    if (err_label != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_io_status == 0) goto f2c_label_%s;\n",
                          err_label);
    }
    if (needs_status) {
        --depth;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    if (internal_file) {
        if (!input) {
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "rewind(f2c_internal_file);\n");
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{ int f2c_internal_character = EOF;\n");
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_internal_record = 0U; f2c_internal_record < "
                              "(size_t)(%s); ++f2c_internal_record) {\n",
                              internal_record_count);
            f2c_io_indent(&context->output, depth + 2);
            f2c_buffer_append(&context->output, "size_t f2c_internal_index = 0U;\n");
            f2c_io_indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output,
                              "char *f2c_internal_destination = %s + f2c_internal_record * "
                              "(size_t)(%s);\n",
                              internal_pointer, internal_length);
            f2c_io_indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output,
                              "while (f2c_internal_index < (size_t)(%s) && "
                              "(f2c_internal_character = fgetc(f2c_internal_file)) != EOF && "
                              "f2c_internal_character != '\\n' && f2c_internal_character != '\\r') "
                              "f2c_internal_destination[f2c_internal_index++] = "
                              "(char)f2c_internal_character;\n",
                              internal_length);
            f2c_io_indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output,
                              "if (f2c_internal_index < (size_t)(%s)) "
                              "memset(f2c_internal_destination + f2c_internal_index, ' ', "
                              "(size_t)(%s) - f2c_internal_index);\n",
                              internal_length, internal_length);
            f2c_io_indent(&context->output, depth + 2);
            f2c_buffer_append(&context->output,
                              "if (f2c_internal_character == '\\r') { int f2c_internal_next = "
                              "fgetc(f2c_internal_file); if (f2c_internal_next != '\\n' && "
                              "f2c_internal_next != EOF) (void)ungetc(f2c_internal_next, "
                              "f2c_internal_file); }\n");
            f2c_io_indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "}\n");
            f2c_io_indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "f2c_unregister_internal_unit(f2c_internal_unit);\n");
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "fclose(f2c_internal_file);\n");
        --depth;
        f2c_io_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    free(file);
    free(unit_number);
    free(end_label);
    free(err_label);
    free(iostat);
    free(eor_label);
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
    free(format_text);
    free(format_literal);
    free(format_value);
    free(format_pointer);
    free(format_length_expression);
    return 1;
}
