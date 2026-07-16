#include "internal/f2c.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static char *emit_item_expression(Unit *unit, const F2cIoItem *item) {
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

static char *emit_required_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

static char *c_string_literal(const char *text, size_t length) {
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

static F2cTypeBinding *defined_io_binding(F2cDerivedType *derived, F2cDefinedIoKind kind) {
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

static int emit_defined_io_call(Context *context, const char *value, F2cDerivedType *derived,
                                F2cDefinedIoKind kind, const char *unit_number, const char *iotype,
                                const char *v_list, const char *v_list_count, const char *status,
                                int depth) {
    F2cTypeBinding *binding = defined_io_binding(derived, kind);
    char *callee;
    const int formatted =
        kind == F2C_DEFINED_IO_READ_FORMATTED || kind == F2C_DEFINED_IO_WRITE_FORMATTED;
    if (binding == NULL)
        return 0;
    callee = defined_io_designator(value, derived, binding);
    if (callee == NULL)
        return 0;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const int32_t f2c_dtio_unit = (int32_t)(%s); "
                      "int32_t f2c_dtio_iostat = 0; char f2c_dtio_iomsg[256]; "
                      "memset(f2c_dtio_iomsg, ' ', sizeof(f2c_dtio_iomsg));\n",
                      unit_number != NULL ? unit_number : "0");
    if (formatted) {
        indent(&context->output, depth + 1);
        if (v_list == NULL)
            f2c_buffer_append(&context->output, "const int32_t f2c_dtio_empty_v_list[1] = {0}; ");
        f2c_buffer_printf(&context->output,
                          "const int32_t *f2c_dtio_v_list = %s; "
                          "const size_t f2c_dtio_v_list_count = (size_t)(%s);\n",
                          v_list != NULL ? v_list : "f2c_dtio_empty_v_list",
                          v_list_count != NULL ? v_list_count : "0U");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "++f2c_child_io_depth;\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "%s((void *)&(%s), &f2c_dtio_unit, %s, f2c_dtio_v_list, "
                          "&f2c_dtio_iostat, f2c_dtio_iomsg, strlen(%s), "
                          "sizeof(f2c_dtio_iomsg));\n",
                          callee, value, iotype != NULL ? iotype : "\"LISTDIRECTED\"",
                          iotype != NULL ? iotype : "\"LISTDIRECTED\"");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "--f2c_child_io_depth;\n");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "(void)f2c_dtio_v_list_count;\n");
    } else {
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "++f2c_child_io_depth;\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "%s((void *)&(%s), &f2c_dtio_unit, &f2c_dtio_iostat, "
                          "f2c_dtio_iomsg, sizeof(f2c_dtio_iomsg));\n",
                          callee, value);
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "--f2c_child_io_depth;\n");
    }
    if (status != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (f2c_dtio_iostat != 0) %s = 0;\n", status);
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(callee);
    return 1;
}

static void emit_namelist_value(Context *context, Unit *unit, const char *file,
                                const Symbol *symbol, const char *value,
                                const char *character_length_override, int input, int depth);

static void emit_io_item(Context *context, Unit *unit, const char *file, const F2cIoItem *item,
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
        char *variable = emit_required_expression(unit, item->iterator);
        char *start = emit_required_expression(unit, item->initial);
        char *finish = emit_required_expression(unit, item->limit);
        char *step = emit_required_expression(unit, item->step);
        if (variable != NULL && start != NULL && finish != NULL && step != NULL) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (%s = %s; ((%s) >= 0 ? %s <= %s : %s >= %s); %s += %s) "
                              "{\n",
                              variable, start, step, variable, finish, variable, finish, variable,
                              step);
            for (i = 0U; i < item->child_count; ++i)
                emit_io_item(context, unit, file, &item->children[i], input, status, 0,
                             defined_kind, unit_number, iotype, depth + 1);
            indent(&context->output, depth);
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
                indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "fputc(' ', %s);\n", file);
            }
            if (!emit_defined_io_call(context, name, symbol->derived_type, defined_kind,
                                      unit_number, iotype, NULL, "0U", status, depth)) {
                indent(&context->output, depth);
                f2c_buffer_append(&context->output, "abort(); /* missing defined I/O */\n");
            }
        } else {
            char *count = f2c_symbol_element_count(unit, symbol);
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_dtio_index = 0U; f2c_dtio_index < %s; "
                              "++f2c_dtio_index) {\n",
                              count != NULL ? count : "0U");
            {
                Buffer value = {0};
                f2c_buffer_printf(&value, "%s[f2c_dtio_index]", name);
                if (!input && iotype != NULL && strcmp(iotype, "\"LISTDIRECTED\"") == 0) {
                    indent(&context->output, depth + 1);
                    f2c_buffer_printf(&context->output, "fputc(' ', %s);\n", file);
                }
                if (!emit_defined_io_call(context, value.data, symbol->derived_type, defined_kind,
                                          unit_number, iotype, NULL, "0U", status, depth + 1)) {
                    indent(&context->output, depth + 1);
                    f2c_buffer_append(&context->output, "abort(); /* missing defined I/O */\n");
                }
                free(value.data);
            }
            indent(&context->output, depth);
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
            indent(&context->output, depth);
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
                emit_namelist_value(context, unit, file, symbol,
                                    value.data != NULL ? value.data
                                                       : f2c_symbol_c_name(unit, symbol),
                                    NULL, 1, depth + 1);
                free(value.data);
            }
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(element_count);
            free(character_length);
        } else if (expression != NULL && expression->type == TYPE_LOGICAL) {
            char *value = emit_item_expression(unit, item);
            if (value == NULL)
                return;
            indent(&context->output, depth);
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
            indent(&context->output, depth);
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
            char *value = emit_item_expression(unit, item);
            if (value == NULL)
                return;
            indent(&context->output, depth);
            if (status != NULL)
                f2c_buffer_printf(&context->output, "%s = F2C_READ(%s, &%s);\n", status, file,
                                  value);
            else
                f2c_buffer_printf(&context->output, "(void)F2C_READ(%s, &%s);\n", file, value);
            free(value);
        }
    } else {
        if (expression != NULL && expression->type == TYPE_LOGICAL && expression->rank == 0U) {
            char *value = emit_item_expression(unit, item);
            if (value != NULL) {
                indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "f2c_write_bool(%s, (%s) != 0);\n", file,
                                  value);
            }
            free(value);
        } else if (expression != NULL && expression->type == TYPE_CHARACTER &&
                   expression->rank == 0U) {
            char *value = emit_item_expression(unit, item);
            char *length = f2c_character_length_expression(unit, expression);
            char *pointer =
                value != NULL ? f2c_character_source_pointer(unit, expression, value) : NULL;
            if (value != NULL && length != NULL && pointer != NULL) {
                indent(&context->output, depth);
                f2c_buffer_printf(&context->output, "f2c_write_character(%s, %s, (size_t)(%s));\n",
                                  file, pointer, length);
            }
            free(value);
            free(length);
            free(pointer);
        } else if (simple_name && symbol != NULL && symbol->rank != 0U) {
            char *element_count = f2c_symbol_element_count(unit, symbol);
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{\n");
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "size_t f2c_io_index; for (f2c_io_index = 0; f2c_io_index < %s; "
                              "++f2c_io_index) {\n",
                              element_count);
            indent(&context->output, depth + 2);
            if (symbol->type == TYPE_LOGICAL)
                f2c_buffer_printf(&context->output, "f2c_write_bool(%s, %s[f2c_io_index] != 0);\n",
                                  file, f2c_symbol_c_name(unit, symbol));
            else
                f2c_buffer_printf(&context->output, "F2C_WRITE(%s, (%s[f2c_io_index]));\n", file,
                                  f2c_symbol_c_name(unit, symbol));
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "}\n");
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            free(element_count);
        } else {
            char *value;
            if (simple_name && symbol != NULL && symbol->type == TYPE_CHARACTER &&
                (symbol->argument || symbol->character_length != NULL || symbol->rank != 0U))
                value = f2c_strdup(f2c_symbol_c_name(unit, symbol));
            else
                value = emit_item_expression(unit, item);
            if (value == NULL)
                return;
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "F2C_WRITE(%s, (%s));\n", file, value);
            free(value);
        }
    }
}

static void emit_formatted_scalar(Context *context, Unit *unit, const F2cExpr *expression,
                                  const Symbol *symbol, const char *value, int input, int depth) {
    Type type =
        expression != NULL ? expression->type : (symbol != NULL ? symbol->type : TYPE_UNKNOWN);
    indent(&context->output, depth);
    if (type == TYPE_CHARACTER) {
        char *length = expression != NULL ? f2c_character_length_expression(unit, expression)
                                          : f2c_symbol_character_length(unit, symbol);
        char *pointer = expression != NULL ? f2c_character_source_pointer(unit, expression, value)
                                           : f2c_strdup(value);
        if (input)
            f2c_buffer_printf(&context->output,
                              "(void)f2c_format_read_character(&f2c_io_format, %s, "
                              "(size_t)(%s));\n",
                              pointer != NULL ? pointer : value, length != NULL ? length : "1U");
        else
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_character(&f2c_io_format, %s, "
                              "(size_t)(%s));\n",
                              pointer != NULL ? pointer : value, length != NULL ? length : "1U");
        free(length);
        free(pointer);
    } else if (type == TYPE_LOGICAL) {
        if (input) {
            f2c_buffer_append(&context->output, "{ bool f2c_formatted_value; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_format_read_logical(&f2c_io_format, "
                              "&f2c_formatted_value) > 0) %s = f2c_formatted_value ? 1 : 0; }\n",
                              value);
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_logical(&f2c_io_format, (%s) != 0);\n", value);
        }
    } else if (type == TYPE_INTEGER) {
        if (input) {
            f2c_buffer_append(&context->output, "{ int64_t f2c_formatted_value; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_format_read_integer(&f2c_io_format, "
                              "&f2c_formatted_value) > 0) %s = (%s)f2c_formatted_value; }\n",
                              value,
                              symbol != NULL ? f2c_symbol_c_type(symbol)
                                             : f2c_expression_c_type(expression));
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_integer(&f2c_io_format, (int64_t)(%s));\n", value);
        }
    } else if (type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX) {
        if (input) {
            f2c_buffer_append(&context->output,
                              "{ double f2c_formatted_real, f2c_formatted_imaginary; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_format_read_real(&f2c_io_format, &f2c_formatted_real) > "
                              "0 && f2c_format_read_real(&f2c_io_format, "
                              "&f2c_formatted_imaginary) > 0) %s = %s(f2c_formatted_real, "
                              "f2c_formatted_imaginary); }\n",
                              value, type == TYPE_COMPLEX ? "f2c_make_c" : "f2c_make_z");
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_real(&f2c_io_format, (double)%s(%s)); "
                              "f2c_format_write_real(&f2c_io_format, (double)%s(%s));\n",
                              type == TYPE_COMPLEX ? "crealf" : "creal", value,
                              type == TYPE_COMPLEX ? "cimagf" : "cimag", value);
        }
    } else {
        if (input) {
            f2c_buffer_append(&context->output, "{ double f2c_formatted_value; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_format_read_real(&f2c_io_format, "
                              "&f2c_formatted_value) > 0) %s = (%s)f2c_formatted_value; }\n",
                              value,
                              symbol != NULL ? f2c_symbol_c_type(symbol)
                                             : f2c_expression_c_type(expression));
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_format_write_real(&f2c_io_format, (double)(%s));\n", value);
        }
    }
}

static void emit_formatted_derived(Context *context, const char *value, F2cDerivedType *derived,
                                   int input, const char *unit_number, int depth) {
    const F2cDefinedIoKind kind =
        input ? F2C_DEFINED_IO_READ_FORMATTED : F2C_DEFINED_IO_WRITE_FORMATTED;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{ f2c_format_descriptor f2c_dtio_descriptor;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (!f2c_format_next(&f2c_io_format, &f2c_dtio_descriptor) || "
                      "f2c_dtio_descriptor.code[0] != 'D' || "
                      "f2c_dtio_descriptor.code[1] != 'T') { "
                      "f2c_io_format.status = 0; } else {\n");
    if (!emit_defined_io_call(context, value, derived, kind, unit_number,
                              "f2c_dtio_descriptor.iotype", "f2c_dtio_descriptor.v_list",
                              "f2c_dtio_descriptor.v_list_count", "f2c_io_format.status",
                              depth + 2)) {
        indent(&context->output, depth + 2);
        f2c_buffer_append(&context->output, "f2c_io_format.status = 0;\n");
    }
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

static void emit_formatted_item(Context *context, Unit *unit, const F2cIoItem *item, int input,
                                const char *unit_number, int depth) {
    const F2cExpr *expression;
    Symbol *symbol;
    if (item == NULL)
        return;
    if (item->implied_do) {
        size_t i;
        char *variable = emit_required_expression(unit, item->iterator);
        char *start = emit_required_expression(unit, item->initial);
        char *finish = emit_required_expression(unit, item->limit);
        char *step = emit_required_expression(unit, item->step);
        if (variable != NULL && start != NULL && finish != NULL && step != NULL) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (%s = %s; ((%s) >= 0 ? %s <= %s : %s >= %s); %s += %s) "
                              "{\n",
                              variable, start, step, variable, finish, variable, finish, variable,
                              step);
            for (i = 0U; i < item->child_count; ++i)
                emit_formatted_item(context, unit, &item->children[i], input, unit_number,
                                    depth + 1);
            indent(&context->output, depth);
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
    if (expression == NULL)
        return;
    if (expression->kind == F2C_EXPR_NAME && symbol != NULL && symbol->rank != 0U) {
        char *count = f2c_symbol_element_count(unit, symbol);
        char *character_length =
            symbol->type == TYPE_CHARACTER ? f2c_symbol_character_length(unit, symbol) : NULL;
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_format_index = 0U; f2c_format_index < %s; "
                          "++f2c_format_index) {\n",
                          count != NULL ? count : "0U");
        {
            Buffer value = {0};
            if (symbol->type == TYPE_CHARACTER)
                f2c_buffer_printf(&value, "%s + f2c_format_index * (size_t)(%s)",
                                  f2c_symbol_c_name(unit, symbol),
                                  character_length != NULL ? character_length : "1U");
            else
                f2c_buffer_printf(&value, "%s[f2c_format_index]", f2c_symbol_c_name(unit, symbol));
            if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL)
                emit_formatted_derived(
                    context, value.data != NULL ? value.data : f2c_symbol_c_name(unit, symbol),
                    symbol->derived_type, input, unit_number, depth + 1);
            else
                emit_formatted_scalar(context, unit, NULL, symbol,
                                      value.data != NULL ? value.data
                                                         : f2c_symbol_c_name(unit, symbol),
                                      input, depth + 1);
            free(value.data);
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(count);
        free(character_length);
    } else {
        char *value = emit_item_expression(unit, item);
        if (value != NULL) {
            if (expression->type == TYPE_DERIVED && expression->derived_type != NULL)
                emit_formatted_derived(context, value, expression->derived_type, input, unit_number,
                                       depth);
            else
                emit_formatted_scalar(context, unit, expression, symbol, value, input, depth);
        }
        free(value);
    }
}

static const F2cIoControl *io_control(const F2cStatement *statement, F2cIoControlKind kind,
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

static void emit_namelist_value(Context *context, Unit *unit, const char *file,
                                const Symbol *symbol, const char *value,
                                const char *character_length_override, int input, int depth) {
    if (symbol->type == TYPE_CHARACTER) {
        char *owned_length =
            character_length_override == NULL ? f2c_symbol_character_length(unit, symbol) : NULL;
        const char *length = character_length_override != NULL
                                 ? character_length_override
                                 : (owned_length != NULL ? owned_length : "1U");
        indent(&context->output, depth);
        if (input) {
            f2c_buffer_printf(&context->output, "(void)f2c_read_character(%s, %s, (size_t)(%s));\n",
                              file, value, length);
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_namelist_write_character(%s, %s, (size_t)(%s));\n", file, value,
                              length);
        }
        free(owned_length);
    } else if (symbol->type == TYPE_LOGICAL) {
        indent(&context->output, depth);
        if (input) {
            f2c_buffer_append(&context->output, "{ bool f2c_namelist_logical; ");
            f2c_buffer_printf(&context->output,
                              "if (f2c_read_bool(%s, &f2c_namelist_logical) > 0) %s = "
                              "f2c_namelist_logical ? 1 : 0; }\n",
                              file, value);
        } else {
            f2c_buffer_printf(&context->output, "f2c_write_bool(%s, (%s) != 0);\n", file, value);
        }
    } else {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          input ? "(void)F2C_READ(%s, &%s);\n" : "F2C_WRITE(%s, (%s));\n", file,
                          value);
    }
}

static char *namelist_component_count(Unit *unit, const Symbol *symbol, const char *owner) {
    Buffer count = {0};
    size_t dimension;
    if (symbol->rank == 0U)
        return f2c_strdup("1U");
    if ((symbol->allocatable || symbol->pointer) && owner != NULL) {
        for (dimension = 0U; dimension < symbol->rank; ++dimension)
            f2c_buffer_printf(&count, "%s(size_t)(%s).%s_extent_%zu", dimension == 0U ? "" : " * ",
                              owner, f2c_symbol_c_name(unit, symbol), dimension + 1U);
        return f2c_buffer_take(&count);
    }
    return f2c_symbol_element_count(unit, (Symbol *)symbol);
}

static char *namelist_character_length(Unit *unit, const Symbol *symbol, const char *owner) {
    Buffer length = {0};
    if (symbol->deferred_character && owner != NULL) {
        f2c_buffer_printf(&length, "(size_t)(%s).%s_character_length", owner,
                          f2c_symbol_c_name(unit, symbol));
        return f2c_buffer_take(&length);
    }
    return f2c_symbol_character_length(unit, symbol);
}

static void emit_namelist_autoallocation(Context *context, Unit *unit, const char *file,
                                         const Symbol *symbol, const char *value, const char *owner,
                                         const char *path, int depth) {
    const char *type;
    size_t dimension;
    if (!symbol->allocatable || symbol->pointer || symbol->procedure_pointer)
        return;
    type = f2c_symbol_c_type(symbol);
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (%s == NULL && ", value);
    if (symbol->rank == 0U)
        f2c_buffer_printf(&context->output,
                          "f2c_namelist_designator_prefix(%s, f2c_namelist_group_start, %s)) {\n",
                          file, path);
    else
        f2c_buffer_append(&context->output, "true) {\n");
    if (symbol->rank != 0U) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "int64_t f2c_namelist_lower_%zu[%zu], "
                          "f2c_namelist_upper_%zu[%zu];\n",
                          (size_t)depth, symbol->rank, (size_t)depth, symbol->rank);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_namelist_designator_bounds(%s, "
                          "f2c_namelist_group_start, %s, %zuU, f2c_namelist_lower_%zu, "
                          "f2c_namelist_upper_%zu)) {\n",
                          file, path, symbol->rank, (size_t)depth, (size_t)depth);
        indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output,
                          "size_t f2c_namelist_extents_%zu[%zu]; size_t "
                          "f2c_namelist_elements_%zu = 1U;\n",
                          (size_t)depth, symbol->rank, (size_t)depth);
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output,
                              "if (f2c_namelist_upper_%zu[%zu] < f2c_namelist_lower_%zu[%zu]) {\n",
                              (size_t)depth, dimension, (size_t)depth, dimension);
            indent(&context->output, depth + 3);
            f2c_buffer_append(&context->output, "abort();\n");
            indent(&context->output, depth + 2);
            f2c_buffer_append(&context->output, "}\n");
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output,
                              "if (f2c_namelist_lower_%zu[%zu] < INT32_MIN || "
                              "f2c_namelist_upper_%zu[%zu] > INT32_MAX) {\n",
                              (size_t)depth, dimension, (size_t)depth, dimension);
            indent(&context->output, depth + 3);
            f2c_buffer_append(&context->output, "abort();\n");
            indent(&context->output, depth + 2);
            f2c_buffer_append(&context->output, "}\n");
            indent(&context->output, depth + 2);
            f2c_buffer_append(&context->output, "{\n");
            indent(&context->output, depth + 3);
            f2c_buffer_printf(&context->output,
                              "const uint64_t f2c_namelist_span = "
                              "(uint64_t)f2c_namelist_upper_%zu[%zu] - "
                              "(uint64_t)f2c_namelist_lower_%zu[%zu] + UINT64_C(1);\n",
                              (size_t)depth, dimension, (size_t)depth, dimension);
            indent(&context->output, depth + 3);
            f2c_buffer_printf(&context->output,
                              "if (f2c_namelist_span > (uint64_t)INT32_MAX || "
                              "f2c_namelist_span > SIZE_MAX || "
                              "(f2c_namelist_span != 0U && f2c_namelist_elements_%zu > "
                              "SIZE_MAX / (size_t)f2c_namelist_span)) {\n",
                              (size_t)depth);
            indent(&context->output, depth + 4);
            f2c_buffer_append(&context->output, "abort();\n");
            indent(&context->output, depth + 3);
            f2c_buffer_append(&context->output, "}\n");
            indent(&context->output, depth + 3);
            f2c_buffer_printf(&context->output,
                              "f2c_namelist_extents_%zu[%zu] = "
                              "(size_t)f2c_namelist_span;\n",
                              (size_t)depth, dimension);
            indent(&context->output, depth + 3);
            f2c_buffer_printf(&context->output,
                              "f2c_namelist_elements_%zu *= "
                              "(size_t)f2c_namelist_span;\n",
                              (size_t)depth);
            indent(&context->output, depth + 2);
            f2c_buffer_append(&context->output, "}\n");
        }
    } else {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "size_t f2c_namelist_elements_%zu = 1U;\n",
                          (size_t)depth);
    }
    if (symbol->type == TYPE_CHARACTER) {
        char *length =
            symbol->deferred_character ? NULL : namelist_character_length(unit, symbol, owner);
        indent(&context->output, depth + (symbol->rank != 0U ? 2 : 1));
        if (symbol->deferred_character)
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_namelist_character_length_%zu = "
                              "f2c_namelist_designator_character_length(%s, "
                              "f2c_namelist_group_start, %s); ",
                              (size_t)depth, file, path);
        else
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_namelist_character_length_%zu = (size_t)(%s); ",
                              (size_t)depth, length != NULL ? length : "0U");
        f2c_buffer_printf(&context->output,
                          "if (f2c_namelist_character_length_%zu != 0U && "
                          "f2c_namelist_elements_%zu > SIZE_MAX / "
                          "f2c_namelist_character_length_%zu) abort();\n",
                          (size_t)depth, (size_t)depth, (size_t)depth);
        free(length);
    }
    indent(&context->output, depth + (symbol->rank != 0U ? 2 : 1));
    if (symbol->type == TYPE_CHARACTER)
        f2c_buffer_printf(&context->output,
                          "%s *f2c_namelist_allocation_%zu = (%s *)calloc("
                          "f2c_namelist_elements_%zu == 0U || "
                          "f2c_namelist_character_length_%zu == 0U ? 1U : "
                          "f2c_namelist_elements_%zu * f2c_namelist_character_length_%zu, "
                          "sizeof(%s));\n",
                          type, (size_t)depth, type, (size_t)depth, (size_t)depth, (size_t)depth,
                          (size_t)depth, type);
    else
        f2c_buffer_printf(&context->output,
                          "%s *f2c_namelist_allocation_%zu = (%s *)calloc("
                          "f2c_namelist_elements_%zu == 0U ? 1U : "
                          "f2c_namelist_elements_%zu, sizeof(%s));\n",
                          type, (size_t)depth, type, (size_t)depth, (size_t)depth, type);
    indent(&context->output, depth + (symbol->rank != 0U ? 2 : 1));
    f2c_buffer_printf(&context->output, "if (f2c_namelist_allocation_%zu == NULL) abort();\n",
                      (size_t)depth);
    if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        indent(&context->output, depth + (symbol->rank != 0U ? 2 : 1));
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_namelist_initialize_%zu = 0U; "
                          "f2c_namelist_initialize_%zu < f2c_namelist_elements_%zu; "
                          "++f2c_namelist_initialize_%zu) f2c_initialize_%s("
                          "&f2c_namelist_allocation_%zu[f2c_namelist_initialize_%zu]);\n",
                          (size_t)depth, (size_t)depth, (size_t)depth, (size_t)depth,
                          symbol->derived_type->c_name, (size_t)depth, (size_t)depth);
    }
    indent(&context->output, depth + (symbol->rank != 0U ? 2 : 1));
    f2c_buffer_printf(&context->output, "%s = f2c_namelist_allocation_%zu;\n", value,
                      (size_t)depth);
    if (symbol->type == TYPE_CHARACTER && symbol->deferred_character) {
        indent(&context->output, depth + (symbol->rank != 0U ? 2 : 1));
        if (owner != NULL)
            f2c_buffer_printf(&context->output,
                              "(%s).%s_character_length = "
                              "f2c_namelist_character_length_%zu;\n",
                              owner, f2c_symbol_c_name(unit, symbol), (size_t)depth);
        else
            f2c_buffer_printf(&context->output,
                              "f2c_char_len_%s = f2c_namelist_character_length_%zu;\n", value,
                              (size_t)depth);
    }
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        indent(&context->output, depth + 2);
        if (owner != NULL)
            f2c_buffer_printf(&context->output,
                              "(%s).%s_lower_%zu = (int32_t)f2c_namelist_lower_%zu[%zu]; "
                              "(%s).%s_extent_%zu = "
                              "(int32_t)f2c_namelist_extents_%zu[%zu];\n",
                              owner, f2c_symbol_c_name(unit, symbol), dimension + 1U, (size_t)depth,
                              dimension, owner, f2c_symbol_c_name(unit, symbol), dimension + 1U,
                              (size_t)depth, dimension);
        else
            f2c_buffer_printf(&context->output,
                              "%s_lower_%zu = (int32_t)f2c_namelist_lower_%zu[%zu]; "
                              "%s_extent_%zu = (int32_t)f2c_namelist_extents_%zu[%zu];\n",
                              value, dimension + 1U, (size_t)depth, dimension, value,
                              dimension + 1U, (size_t)depth, dimension);
    }
    if (symbol->rank != 0U) {
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
}

static void emit_namelist_object(Context *context, Unit *unit, const char *file,
                                 const Symbol *symbol, const char *value, const char *owner,
                                 const char *path, int input, const char *unit_number, int depth,
                                 int scalarized, int indirect) {
    const size_t path_id = (size_t)depth;
    const F2cDefinedIoKind namelist_kind =
        input ? F2C_DEFINED_IO_READ_FORMATTED : F2C_DEFINED_IO_WRITE_FORMATTED;
    if (input && !scalarized)
        emit_namelist_autoallocation(context, unit, file, symbol, value, owner, path, depth);
    if (!scalarized && symbol->type == TYPE_DERIVED && symbol->derived_type != NULL &&
        defined_io_binding(symbol->derived_type, namelist_kind) != NULL) {
        char *count = namelist_component_count(unit, symbol, owner);
        if (input) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if (f2c_namelist_member(%s, f2c_namelist_group_start, %s)) {\n",
                              file, path);
            ++depth;
        } else {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "fprintf(%s, \" %%s=\", %s);\n", file, path);
        }
        if (symbol->rank == 0U) {
            Buffer scalar = {0};
            if (indirect)
                f2c_buffer_printf(&scalar, "*(%s)", value);
            else
                f2c_buffer_append(&scalar, value);
            (void)emit_defined_io_call(context, scalar.data, symbol->derived_type, namelist_kind,
                                       unit_number, "\"NAMELIST\"", NULL, "0U", NULL, depth);
            free(scalar.data);
        } else {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_namelist_dtio_%zu = 0U; "
                              "f2c_namelist_dtio_%zu < %s; ++f2c_namelist_dtio_%zu) {\n",
                              path_id, path_id, count != NULL ? count : "0U", path_id);
            {
                Buffer element = {0};
                f2c_buffer_printf(&element, "%s[f2c_namelist_dtio_%zu]", value, path_id);
                (void)emit_defined_io_call(context, element.data, symbol->derived_type,
                                           namelist_kind, unit_number, "\"NAMELIST\"", NULL, "0U",
                                           NULL, depth + 1);
                free(element.data);
            }
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        if (input) {
            --depth;
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        free(count);
        return;
    }
    if (!scalarized && symbol->rank != 0U &&
        ((symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) || symbol->allocatable)) {
        char *count = namelist_component_count(unit, symbol, owner);
        size_t dimension;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_namelist_count_%zu = (size_t)(%s);\n",
                          path_id, count != NULL ? count : "0U");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_namelist_index_%zu = 0U; "
                          "f2c_namelist_index_%zu < f2c_namelist_count_%zu; "
                          "++f2c_namelist_index_%zu) {\n",
                          path_id, path_id, path_id, path_id);
        indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output, "char f2c_namelist_path_%zu[512];\n", path_id);
        indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output,
                          "if (snprintf(f2c_namelist_path_%zu, "
                          "sizeof(f2c_namelist_path_%zu), \"%%s(",
                          path_id, path_id);
        for (dimension = 0U; dimension < symbol->rank; ++dimension)
            f2c_buffer_append(&context->output, dimension == 0U ? "%lld" : ",%lld");
        f2c_buffer_printf(&context->output, ")\", %s", path);
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            char *extent;
            char *lower;
            if ((symbol->allocatable || symbol->pointer) && owner != NULL) {
                Buffer dynamic_extent = {0};
                Buffer dynamic_lower = {0};
                f2c_buffer_printf(&dynamic_extent, "(size_t)(%s).%s_extent_%zu", owner,
                                  f2c_symbol_c_name(unit, symbol), dimension + 1U);
                f2c_buffer_printf(&dynamic_lower, "(int64_t)(%s).%s_lower_%zu", owner,
                                  f2c_symbol_c_name(unit, symbol), dimension + 1U);
                extent = f2c_buffer_take(&dynamic_extent);
                lower = f2c_buffer_take(&dynamic_lower);
            } else {
                extent = f2c_symbol_dimension_extent(unit, symbol, dimension);
                lower = f2c_symbol_dimension_lower(unit, symbol, dimension);
            }
            f2c_buffer_printf(&context->output,
                              ", (long long)((int64_t)(%s) + "
                              "(int64_t)((f2c_namelist_index_%zu / (size_t)(",
                              lower != NULL ? lower : "1", path_id);
            for (size_t prior = 0U; prior < dimension; ++prior) {
                char *prior_extent;
                if ((symbol->allocatable || symbol->pointer) && owner != NULL) {
                    Buffer dynamic = {0};
                    f2c_buffer_printf(&dynamic, "(size_t)(%s).%s_extent_%zu", owner,
                                      f2c_symbol_c_name(unit, symbol), prior + 1U);
                    prior_extent = f2c_buffer_take(&dynamic);
                } else {
                    prior_extent = f2c_symbol_dimension_extent(unit, symbol, prior);
                }
                f2c_buffer_printf(&context->output, "%s(%s)", prior == 0U ? "" : " * ",
                                  prior_extent != NULL ? prior_extent : "1U");
                free(prior_extent);
            }
            if (dimension == 0U)
                f2c_buffer_append(&context->output, "1U");
            f2c_buffer_printf(&context->output, ")) %% (size_t)(%s)))",
                              extent != NULL ? extent : "1U");
            free(extent);
            free(lower);
        }
        f2c_buffer_printf(&context->output, ") < 0) abort();\n");
        {
            Buffer element = {0};
            Buffer element_path = {0};
            if (symbol->type == TYPE_CHARACTER) {
                char *length = namelist_character_length(unit, symbol, owner);
                f2c_buffer_printf(&element, "%s + f2c_namelist_index_%zu * (size_t)(%s)", value,
                                  path_id, length != NULL ? length : "0U");
                free(length);
            } else {
                f2c_buffer_printf(&element, "%s[f2c_namelist_index_%zu]", value, path_id);
            }
            f2c_buffer_printf(&element_path, "f2c_namelist_path_%zu", path_id);
            emit_namelist_object(context, unit, file, symbol, element.data, owner,
                                 element_path.data, input, unit_number, depth + 2, 1, 0);
            free(element.data);
            free(element_path.data);
        }
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(count);
        return;
    }
    if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        Buffer object = {0};
        size_t component_index;
        if (indirect)
            f2c_buffer_printf(&object, "*(%s)", value);
        else
            f2c_buffer_append(&object, value);
        for (component_index = 0U; component_index < symbol->derived_type->component_count;
             ++component_index) {
            const Symbol *component = &symbol->derived_type->components[component_index];
            Buffer component_value = {0};
            Buffer component_path = {0};
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{\n");
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output, "char f2c_namelist_path_%zu[512];\n", path_id);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (snprintf(f2c_namelist_path_%zu, "
                              "sizeof(f2c_namelist_path_%zu), \"%%s%%%%%s\", %s) < 0) abort();\n",
                              path_id, path_id, component->name, path);
            f2c_buffer_printf(&component_value, "(%s).%s", object.data,
                              f2c_symbol_c_name(unit, component));
            f2c_buffer_printf(&component_path, "f2c_namelist_path_%zu", path_id);
            if (input && component->allocatable)
                emit_namelist_autoallocation(context, unit, file, component, component_value.data,
                                             object.data, component_path.data, depth + 1);
            if (component->allocatable || component->pointer) {
                indent(&context->output, depth + 1);
                f2c_buffer_printf(&context->output, "if (%s != NULL) {\n", component_value.data);
            }
            emit_namelist_object(
                context, unit, file, component, component_value.data, object.data,
                component_path.data, input, unit_number,
                depth + 1 + ((component->allocatable || component->pointer) ? 1 : 0), 0,
                component->rank == 0U && (component->allocatable || component->pointer) &&
                    component->type != TYPE_CHARACTER);
            free(component_value.data);
            free(component_path.data);
            if (component->allocatable || component->pointer) {
                indent(&context->output, depth + 1);
                f2c_buffer_append(&context->output, "}\n");
            }
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        free(object.data);
        return;
    }
    if (input) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_namelist_member(%s, f2c_namelist_group_start, %s)) {\n", file,
                          path);
        ++depth;
    } else {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "fprintf(%s, \" %%s=\", %s);\n", file, path);
    }
    if (symbol->rank == 0U || scalarized) {
        Buffer scalar = {0};
        char *character_length =
            symbol->type == TYPE_CHARACTER ? namelist_character_length(unit, symbol, owner) : NULL;
        if (indirect && symbol->type != TYPE_CHARACTER)
            f2c_buffer_printf(&scalar, "*(%s)", value);
        else
            f2c_buffer_append(&scalar, value);
        emit_namelist_value(context, unit, file, symbol, scalar.data, character_length, input,
                            depth);
        free(character_length);
        free(scalar.data);
    } else {
        char *element_count = namelist_component_count(unit, symbol, owner);
        char *character_length =
            symbol->type == TYPE_CHARACTER ? namelist_character_length(unit, symbol, owner) : NULL;
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "for (size_t f2c_namelist_value_%zu = 0U; "
                          "f2c_namelist_value_%zu < %s; ++f2c_namelist_value_%zu) {\n",
                          path_id, path_id, element_count != NULL ? element_count : "0U", path_id);
        {
            Buffer element = {0};
            if (symbol->type == TYPE_CHARACTER)
                f2c_buffer_printf(&element, "%s + f2c_namelist_value_%zu * (size_t)(%s)", value,
                                  path_id, character_length != NULL ? character_length : "1U");
            else
                f2c_buffer_printf(&element, "%s[f2c_namelist_value_%zu]", value, path_id);
            emit_namelist_value(context, unit, file, symbol, element.data, character_length, input,
                                depth + 1);
            free(element.data);
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(element_count);
        free(character_length);
    }
    if (input) {
        --depth;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
}

static int emit_namelist(Context *context, Unit *unit, const char *file,
                         const F2cNamelistGroup *group, int input, const char *unit_number,
                         int depth) {
    size_t i;
    if (group == NULL)
        return 0;
    if (input) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "long f2c_namelist_start = ftell(%s);\n"
                          "%*slong f2c_namelist_group_start = "
                          "f2c_namelist_group(%s, f2c_namelist_start, \"%s\");\n",
                          file, depth * 4, "", file, group->name);
    } else {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "fputs(\"&%s\", %s);\n", group->name, file);
    }
    for (i = 0U; i < group->member_count; ++i) {
        Symbol *symbol = f2c_find_symbol(unit, group->members[i]);
        const char *name;
        char *path;
        if (symbol == NULL)
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        path = c_string_literal(group->members[i], strlen(group->members[i]));
        emit_namelist_object(context, unit, file, symbol, name, NULL, path != NULL ? path : "\"\"",
                             input, unit_number, depth, 0,
                             symbol->rank == 0U && (symbol->allocatable || symbol->pointer) &&
                                 symbol->type != TYPE_CHARACTER);
        free(path);
    }
    indent(&context->output, depth);
    if (input) {
        f2c_buffer_printf(&context->output, "f2c_namelist_end(%s, f2c_namelist_group_start);\n",
                          file);
    } else {
        f2c_buffer_printf(&context->output, "fputs(\" /\\n\", %s);\n", file);
    }
    return 1;
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
    const F2cIoControl *control = io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    char *unit_value;
    Buffer result = {0};
    if (control == NULL)
        return f2c_strdup(input ? "stdin" : "stdout");
    if (control->asterisk) {
        unit_value = f2c_strdup(input ? "stdin" : "stdout");
    } else if (control->value != NULL && control->value->type == TYPE_CHARACTER) {
        unit_value = f2c_strdup("f2c_internal_file");
    } else {
        char *translated = emit_required_expression(unit, control->value);
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
    const F2cIoControl *control = io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    if (internal_file)
        return f2c_strdup("f2c_internal_unit");
    if (control == NULL || control->asterisk)
        return f2c_strdup(input ? "5" : "6");
    return control->value != NULL ? emit_required_expression(unit, control->value) : NULL;
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
        (unit_control = io_control(statement, F2C_IO_CONTROL_UNIT, 0U)) != NULL &&
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
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        ++depth;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "FILE *f2c_internal_file = tmpfile();\n");
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "if (f2c_internal_file == NULL) abort();\n");
        indent(&context->output, depth);
        f2c_buffer_append(
            &context->output,
            "const int32_t f2c_internal_unit = f2c_register_internal_unit(f2c_internal_file);\n");
        if (input) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_internal_record = 0U; f2c_internal_record < "
                              "(size_t)(%s); ++f2c_internal_record) {\n",
                              internal_record_count);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if ((size_t)(%s) != 0U) (void)fwrite(%s + "
                              "f2c_internal_record * (size_t)(%s), 1U, (size_t)(%s), "
                              "f2c_internal_file);\n",
                              internal_length, internal_pointer, internal_length, internal_length);
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "fputc('\\n', f2c_internal_file);\n");
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "rewind(f2c_internal_file);\n");
        }
    }
    end_control = input ? io_control(statement, F2C_IO_CONTROL_END, (size_t)-1) : NULL;
    eor_control = input ? io_control(statement, F2C_IO_CONTROL_EOR, (size_t)-1) : NULL;
    err_control = io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    iostat_control = io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    iomsg_control = io_control(statement, F2C_IO_CONTROL_IOMSG, (size_t)-1);
    size_control = input ? io_control(statement, F2C_IO_CONTROL_SIZE, (size_t)-1) : NULL;
    advance_control = io_control(statement, F2C_IO_CONTROL_ADVANCE, (size_t)-1);
    if (input)
        end_label = end_control != NULL ? emit_required_expression(unit, end_control->value) : NULL;
    if (input)
        eor_label = eor_control != NULL ? emit_required_expression(unit, eor_control->value) : NULL;
    err_label = err_control != NULL ? emit_required_expression(unit, err_control->value) : NULL;
    iostat = iostat_control != NULL ? emit_required_expression(unit, iostat_control->value) : NULL;
    if (iomsg_control != NULL && iomsg_control->value != NULL) {
        iomsg_value = emit_required_expression(unit, iomsg_control->value);
        iomsg_pointer = iomsg_value != NULL
                            ? f2c_character_source_pointer(unit, iomsg_control->value, iomsg_value)
                            : NULL;
        iomsg_length = f2c_character_length_expression(unit, iomsg_control->value);
    }
    if (size_control != NULL)
        size_value = emit_required_expression(unit, size_control->value);
    if (advance_control != NULL && advance_control->value != NULL) {
        Buffer advance = {0};
        advance_value = emit_required_expression(unit, advance_control->value);
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
    format_control = io_control(statement, F2C_IO_CONTROL_FMT, 1U);
    formatted = format_control != NULL && !format_control->asterisk;
    list_directed = format_control != NULL && format_control->asterisk;
    namelist_control = io_control(statement, F2C_IO_CONTROL_NML, (size_t)-1);
    if (namelist_control != NULL && namelist_control->value != NULL)
        namelist_group = f2c_find_namelist(unit, namelist_control->value->text);
    if (needs_status) {
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        ++depth;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "int f2c_io_status = 1;\n");
    }
    if (formatted) {
        format_text = constant_format(context, unit, format_control, &format_length);
        if (format_text != NULL) {
            format_literal = c_string_literal(format_text, format_length);
        } else if (format_control->value != NULL && format_control->value->type == TYPE_CHARACTER) {
            format_value = emit_required_expression(unit, format_control->value);
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
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        ++depth;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "f2c_format_state f2c_io_format;\n");
        indent(&context->output, depth);
        (void)snprintf(constant_format_length, sizeof(constant_format_length), "%zuU",
                       format_length);
        f2c_buffer_printf(
            &context->output, "f2c_format_initialize(&f2c_io_format, %s, %s, (size_t)(%s), %s);\n",
            file, format_literal != NULL ? format_literal : format_pointer,
            format_literal != NULL ? constant_format_length : format_length_expression,
            input ? "true" : "false");
    }
    if (namelist_group != NULL) {
        if (!emit_namelist(context, unit, file, namelist_group, input, unit_number, depth)) {
            free(file);
            return 0;
        }
    } else if (formatted) {
        for (i = 0U; i < statement->io_item_count; ++i)
            emit_formatted_item(context, unit, &statement->io_items[i], input, unit_number, depth);
    } else if (statement->io_item_count == 1U && statement->io_items[0].implied_do) {
        emit_io_item(context, unit, file, &statement->io_items[0], input,
                     input && needs_status ? "f2c_io_status" : NULL, 0,
                     input ? (list_directed ? F2C_DEFINED_IO_READ_FORMATTED
                                            : F2C_DEFINED_IO_READ_UNFORMATTED)
                           : (list_directed ? F2C_DEFINED_IO_WRITE_FORMATTED
                                            : F2C_DEFINED_IO_WRITE_UNFORMATTED),
                     unit_number, list_directed ? "\"LISTDIRECTED\"" : NULL, depth);
    } else {
        for (i = 0U; i < statement->io_item_count; ++i)
            emit_io_item(context, unit, file, &statement->io_items[i], input,
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
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_format_finish(&f2c_io_format, %s);\n",
                          advance_expression);
        if (size_value != NULL) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = (int32_t)f2c_io_format.transferred;\n",
                              size_value);
        }
        if (needs_status) {
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "f2c_io_status = f2c_io_format.status;\n");
        }
        --depth;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    if (input) {
        if (namelist_group == NULL && !formatted && !io_character_record_format(format_control)) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if ((%s) && f2c_child_io_depth == 0U) f2c_finish_read(%s);\n",
                              advance_expression, file);
        }
    } else if (namelist_group == NULL && !formatted) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if ((%s) && f2c_child_io_depth == 0U) fputc('\\n', %s);\n",
                          advance_expression, file);
    }
    if (!input && needs_status) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (ferror(%s)) f2c_io_status = 0;\n", file);
    }
    if (iostat != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "%s = f2c_io_status == EOF ? -1 : (f2c_io_status == -2 ? -2 : "
                          "(f2c_io_status <= 0 ? 1 : 0));\n",
                          iostat);
    }
    if (iomsg_pointer != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_set_iomsg(%s, (size_t)(%s), f2c_io_status);\n",
                          iomsg_pointer, iomsg_length);
    }
    if (end_label != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_io_status == EOF) goto f2c_label_%s;\n",
                          end_label);
    }
    if (eor_label != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_io_status == -2) goto f2c_label_%s;\n",
                          eor_label);
    }
    if (err_label != NULL) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (f2c_io_status == 0) goto f2c_label_%s;\n",
                          err_label);
    }
    if (needs_status) {
        --depth;
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    if (internal_file) {
        if (!input) {
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "rewind(f2c_internal_file);\n");
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "{ int f2c_internal_character = EOF;\n");
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "for (size_t f2c_internal_record = 0U; f2c_internal_record < "
                              "(size_t)(%s); ++f2c_internal_record) {\n",
                              internal_record_count);
            indent(&context->output, depth + 2);
            f2c_buffer_append(&context->output, "size_t f2c_internal_index = 0U;\n");
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output,
                              "char *f2c_internal_destination = %s + f2c_internal_record * "
                              "(size_t)(%s);\n",
                              internal_pointer, internal_length);
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output,
                              "while (f2c_internal_index < (size_t)(%s) && "
                              "(f2c_internal_character = fgetc(f2c_internal_file)) != EOF && "
                              "f2c_internal_character != '\\n' && f2c_internal_character != '\\r') "
                              "f2c_internal_destination[f2c_internal_index++] = "
                              "(char)f2c_internal_character;\n",
                              internal_length);
            indent(&context->output, depth + 2);
            f2c_buffer_printf(&context->output,
                              "if (f2c_internal_index < (size_t)(%s)) "
                              "memset(f2c_internal_destination + f2c_internal_index, ' ', "
                              "(size_t)(%s) - f2c_internal_index);\n",
                              internal_length, internal_length);
            indent(&context->output, depth + 2);
            f2c_buffer_append(&context->output,
                              "if (f2c_internal_character == '\\r') { int f2c_internal_next = "
                              "fgetc(f2c_internal_file); if (f2c_internal_next != '\\n' && "
                              "f2c_internal_next != EOF) (void)ungetc(f2c_internal_next, "
                              "f2c_internal_file); }\n");
            indent(&context->output, depth + 1);
            f2c_buffer_append(&context->output, "}\n");
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "f2c_unregister_internal_unit(f2c_internal_unit);\n");
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "fclose(f2c_internal_file);\n");
        --depth;
        indent(&context->output, depth);
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

int f2c_emit_print_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth) {
    size_t i;
    if (statement->io_item_count != statement->item_count)
        return 0;
    for (i = 0U; i < statement->io_item_count; ++i)
        emit_io_item(context, unit, "stdout", &statement->io_items[i], 0, NULL, 0,
                     F2C_DEFINED_IO_WRITE_FORMATTED, "6", "\"LISTDIRECTED\"", depth);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "fputc('\\n', stdout);\n");
    return 1;
}

int f2c_emit_open_statement(Context *context, Unit *unit, const F2cStatement *statement,
                            int depth) {
    const F2cIoControl *unit_control = io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const F2cIoControl *file_control = io_control(statement, F2C_IO_CONTROL_FILE, (size_t)-1);
    const F2cIoControl *status_control = io_control(statement, F2C_IO_CONTROL_STATUS, (size_t)-1);
    const F2cIoControl *form_control = io_control(statement, F2C_IO_CONTROL_FORM, (size_t)-1);
    const F2cIoControl *iostat_control = io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    const F2cIoControl *err_control = io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
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
    unit_value = emit_required_expression(unit, unit_control->value);
    file_value = file_control != NULL && file_control->value != NULL
                     ? emit_required_expression(unit, file_control->value)
                     : f2c_strdup("\"\"");
    file_length = file_control != NULL && file_control->value != NULL
                      ? f2c_character_length_expression(unit, file_control->value)
                      : f2c_strdup("0U");
    if (status_control != NULL && status_control->value != NULL) {
        status_value = emit_required_expression(unit, status_control->value);
        status_length = f2c_character_length_expression(unit, status_control->value);
    }
    if (form_control != NULL && form_control->value != NULL) {
        form_value = emit_required_expression(unit, form_control->value);
        form_length = f2c_character_length_expression(unit, form_control->value);
    }
    if (iostat_control != NULL && iostat_control->value != NULL)
        iostat_value = emit_required_expression(unit, iostat_control->value);
    if (err_control != NULL && err_control->value != NULL)
        err_label = emit_required_expression(unit, err_control->value);
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
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_io_ok = f2c_open_unit((int32_t)(%s), %s, "
                      "(size_t)(%s), %s, (size_t)(%s), %s, (size_t)(%s));\n",
                      unit_value, file_value, file_length,
                      status_value != NULL ? status_value : "\"unknown\"",
                      status_length != NULL ? status_length : "7U",
                      form_value != NULL ? form_value : "\"formatted\"",
                      form_length != NULL ? form_length : "9U");
    if (iostat_value != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_io_ok ? 0 : 1;\n", iostat_value);
    }
    if (err_label != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (!f2c_io_ok) goto f2c_label_%s;\n", err_label);
    } else if (iostat_value == NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (!f2c_io_ok) { fputs(\"Fortran OPEN failed\\n\", stderr); "
                          "abort(); }\n");
    }
    indent(&context->output, depth);
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
    const F2cIoControl *unit_control = io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const F2cIoControl *iostat_control = io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    const F2cIoControl *err_control = io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    char *unit_value;
    char *iostat_value = NULL;
    char *err_label = NULL;
    if (unit_control == NULL || unit_control->value == NULL)
        return 0;
    unit_value = emit_required_expression(unit, unit_control->value);
    if (iostat_control != NULL && iostat_control->value != NULL)
        iostat_value = emit_required_expression(unit, iostat_control->value);
    if (err_control != NULL && err_control->value != NULL)
        err_label = emit_required_expression(unit, err_control->value);
    if (unit_value == NULL || (iostat_control != NULL && iostat_value == NULL) ||
        (err_control != NULL && err_label == NULL)) {
        free(unit_value);
        free(iostat_value);
        free(err_label);
        return 0;
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_io_ok = f2c_rewind_unit((int32_t)(%s));\n",
                      unit_value);
    if (iostat_value != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_io_ok ? 0 : 1;\n", iostat_value);
    }
    if (err_label != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (!f2c_io_ok) goto f2c_label_%s;\n", err_label);
    } else if (iostat_value == NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (!f2c_io_ok) { fputs(\"Fortran REWIND failed\\n\", stderr); "
                          "abort(); }\n");
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(unit_value);
    free(iostat_value);
    free(err_label);
    return 1;
}

int f2c_emit_close_statement(Context *context, Unit *unit, const F2cStatement *statement,
                             int depth) {
    const F2cIoControl *unit_control = io_control(statement, F2C_IO_CONTROL_UNIT, 0U);
    const F2cIoControl *iostat_control = io_control(statement, F2C_IO_CONTROL_IOSTAT, (size_t)-1);
    const F2cIoControl *err_control = io_control(statement, F2C_IO_CONTROL_ERR, (size_t)-1);
    char *unit_value;
    char *iostat_value = NULL;
    char *err_label = NULL;
    if (unit_control == NULL || unit_control->value == NULL)
        return 0;
    unit_value = emit_required_expression(unit, unit_control->value);
    if (iostat_control != NULL && iostat_control->value != NULL)
        iostat_value = emit_required_expression(unit, iostat_control->value);
    if (err_control != NULL && err_control->value != NULL)
        err_label = emit_required_expression(unit, err_control->value);
    if (unit_value == NULL || (iostat_control != NULL && iostat_value == NULL) ||
        (err_control != NULL && err_label == NULL)) {
        free(unit_value);
        free(iostat_value);
        free(err_label);
        return 0;
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const bool f2c_io_ok = f2c_close_unit((int32_t)(%s));\n",
                      unit_value);
    if (iostat_value != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = f2c_io_ok ? 0 : 1;\n", iostat_value);
    }
    if (err_label != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "if (!f2c_io_ok) goto f2c_label_%s;\n", err_label);
    } else if (iostat_value == NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (!f2c_io_ok) { fputs(\"Fortran CLOSE failed\\n\", stderr); "
                          "abort(); }\n");
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(unit_value);
    free(iostat_value);
    free(err_label);
    return 1;
}
