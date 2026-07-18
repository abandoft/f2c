#include "codegen/io/private.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *unquote_fortran_string(const char *text, size_t *length_out) {
    Buffer result = {0};
    const char *quote_begin = f2c_character_literal_quote(text);
    const size_t length = quote_begin != NULL ? strlen(quote_begin) : 0U;
    const char quote = length != 0U ? quote_begin[0] : '\0';
    size_t i;
    *length_out = 0U;
    if (length < 2U || (quote != '\'' && quote != '"') || quote_begin[length - 1U] != quote)
        return NULL;
    for (i = 1U; i + 1U < length; ++i) {
        if (quote_begin[i] == quote && i + 2U < length && quote_begin[i + 1U] == quote)
            ++i;
        f2c_buffer_append_n(&result, &quote_begin[i], 1U);
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

typedef struct AssignedFormatCase {
    const char *label;
    char *literal;
    size_t length;
} AssignedFormatCase;

typedef struct AssignedFormats {
    AssignedFormatCase *items;
    size_t count;
    size_t capacity;
} AssignedFormats;

static void free_assigned_formats(AssignedFormats *formats) {
    size_t index;
    if (formats == NULL)
        return;
    for (index = 0U; index < formats->count; ++index)
        free(formats->items[index].literal);
    free(formats->items);
    memset(formats, 0, sizeof(*formats));
}

static int append_assigned_format(Context *context, Unit *unit, AssignedFormats *formats,
                                  const char *label) {
    char *text;
    char *literal;
    size_t length;
    size_t index;
    AssignedFormatCase *resized;
    size_t capacity;
    for (index = 0U; index < formats->count; ++index)
        if (strcmp(formats->items[index].label, label) == 0)
            return 1;
    text = labeled_format(context, unit, label, &length);
    if (text == NULL)
        return 1;
    literal = f2c_io_c_string_literal(text, length);
    free(text);
    if (literal == NULL)
        return 0;
    if (formats->count == formats->capacity) {
        capacity = formats->capacity == 0U ? 4U : formats->capacity * 2U;
        if (capacity < formats->capacity || capacity > SIZE_MAX / sizeof(*formats->items)) {
            free(literal);
            return 0;
        }
        resized = (AssignedFormatCase *)realloc(formats->items, capacity * sizeof(*formats->items));
        if (resized == NULL) {
            free(literal);
            return 0;
        }
        formats->items = resized;
        formats->capacity = capacity;
    }
    formats->items[formats->count].label = label;
    formats->items[formats->count].literal = literal;
    formats->items[formats->count].length = length;
    ++formats->count;
    return 1;
}

static int collect_assigned_formats(Context *context, Unit *unit, const F2cStatement *statement,
                                    const char *name, AssignedFormats *formats) {
    if (statement == NULL)
        return 1;
    if (statement->kind == F2C_STMT_ASSIGN_LABEL && statement->name != NULL &&
        strcmp(statement->name, name) == 0 && statement->label_count == 1U &&
        !append_assigned_format(context, unit, formats, statement->labels[0]))
        return 0;
    return collect_assigned_formats(context, unit, statement->nested, name, formats);
}

static int emit_assigned_format_selection(Context *context, Unit *unit,
                                          const F2cExpr *format_expression, int depth) {
    AssignedFormats formats = {0};
    char *selector = NULL;
    size_t index;
    int result = 0;
    if (format_expression == NULL || format_expression->text == NULL)
        return 0;
    for (index = 0U; index < unit->statement_count; ++index) {
        if (!collect_assigned_formats(context, unit, &unit->statements[index],
                                      format_expression->text, &formats))
            goto cleanup;
    }
    selector = f2c_io_emit_required_expression(unit, format_expression);
    if (selector == NULL || formats.count == 0U)
        goto cleanup;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "const char *f2c_io_format_text = NULL;\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_io_format_length = 0U;\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "switch ((int32_t)(%s)) {\n", selector);
    for (index = 0U; index < formats.count; ++index) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "case %s: f2c_io_format_text = %s; "
                          "f2c_io_format_length = %zuU; break;\n",
                          formats.items[index].label, formats.items[index].literal,
                          formats.items[index].length);
    }
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "default: break;\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_io_format_text == NULL) abort();\n");
    result = 1;

cleanup:
    free(selector);
    free_assigned_formats(&formats);
    return result;
}

int f2c_io_emit_formatted_transfer(Context *context, Unit *unit, const F2cStatement *statement,
                                   const F2cIoControl *format_control, const char *file,
                                   const char *unit_number, int input,
                                   const char *advance_expression, const char *size_target,
                                   const char *status_target, int depth) {
    char *format_text = NULL;
    char *format_literal = NULL;
    char *format_value = NULL;
    char *format_pointer = NULL;
    char *format_length_expression = NULL;
    size_t format_length = 0U;
    char constant_format_length[64];
    size_t index;
    int assigned_format;
    int result = 0;
    if (context == NULL || unit == NULL || statement == NULL || format_control == NULL ||
        format_control->asterisk || file == NULL || unit_number == NULL ||
        advance_expression == NULL)
        return 0;
    assigned_format = format_control->value != NULL &&
                      format_control->value->type == TYPE_INTEGER &&
                      format_control->value->kind == F2C_EXPR_NAME;
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
    if ((!assigned_format && format_literal == NULL &&
         (format_pointer == NULL || format_length_expression == NULL)) ||
        (format_literal != NULL && format_length == 0U)) {
        f2c_diagnostic(context, statement->line, 1,
                       "FORMAT label or CHARACTER expression could not be resolved");
        goto cleanup;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    ++depth;
    if (assigned_format &&
        !emit_assigned_format_selection(context, unit, format_control->value, depth)) {
        f2c_diagnostic(context, statement->line, 1,
                       "assigned FORMAT variable has no resolvable FORMAT label");
        goto cleanup;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "f2c_format_state f2c_io_format;\n");
    f2c_io_indent(&context->output, depth);
    (void)snprintf(constant_format_length, sizeof(constant_format_length), "%zuU", format_length);
    f2c_buffer_printf(&context->output,
                      "f2c_format_initialize(&f2c_io_format, %s, %s, (size_t)(%s), %s);\n", file,
                      assigned_format ? "f2c_io_format_text"
                                      : (format_literal != NULL ? format_literal : format_pointer),
                      assigned_format ? "f2c_io_format_length"
                                      : (format_literal != NULL ? constant_format_length
                                                                : format_length_expression),
                      input ? "true" : "false");
    for (index = 0U; index < statement->io_item_count; ++index)
        f2c_io_emit_formatted_item(context, unit, &statement->io_items[index], input, unit_number,
                                   depth);
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "f2c_format_finish(&f2c_io_format, %s);\n",
                      advance_expression);
    if (size_target != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = (int32_t)f2c_io_format.transferred;\n",
                          size_target);
    }
    if (status_target != NULL) {
        f2c_io_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "%s = f2c_io_format.status;\n", status_target);
    }
    --depth;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    free(format_text);
    free(format_literal);
    free(format_value);
    free(format_pointer);
    free(format_length_expression);
    return result;
}
