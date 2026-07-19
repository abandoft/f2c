#include "codegen/io/private.h"

#include <stdlib.h>
#include <string.h>

static const F2cStatement *find_format_statement(const Unit *unit, const char *label) {
    size_t index;
    if (unit == NULL || label == NULL)
        return NULL;
    for (index = 0U; index < unit->statement_count; ++index) {
        const F2cStatement *statement = &unit->statements[index];
        if (statement->kind == F2C_STMT_FORMAT && statement->name != NULL &&
            f2c_statement_labels_equal(statement->name, label))
            return statement;
    }
    return NULL;
}

static char *format_program_name(const char *category, size_t line, const char *suffix) {
    Buffer name = {0};
    f2c_buffer_printf(&name, "f2c_io_%s_%zu", category, line);
    if (suffix != NULL)
        f2c_buffer_printf(&name, "_%s", suffix);
    return f2c_buffer_take(&name);
}

typedef struct AssignedFormatCase {
    const char *label;
    const F2cFormat *format;
    char *program_name;
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
        free(formats->items[index].program_name);
    free(formats->items);
    memset(formats, 0, sizeof(*formats));
}

static int append_assigned_format(Unit *unit, AssignedFormats *formats, const char *label,
                                  size_t line) {
    const F2cStatement *definition;
    char *program_name;
    size_t index;
    AssignedFormatCase *resized;
    size_t capacity;
    for (index = 0U; index < formats->count; ++index)
        if (f2c_statement_labels_equal(formats->items[index].label, label))
            return 1;
    definition = find_format_statement(unit, label);
    if (definition == NULL || definition->format == NULL || !definition->format->validated)
        return 1;
    program_name = format_program_name("assigned_format", line, label);
    if (program_name == NULL)
        return 0;
    if (formats->count == formats->capacity) {
        capacity = formats->capacity == 0U ? 4U : formats->capacity * 2U;
        if (capacity < formats->capacity || capacity > SIZE_MAX / sizeof(*formats->items)) {
            free(program_name);
            return 0;
        }
        resized = (AssignedFormatCase *)realloc(formats->items, capacity * sizeof(*formats->items));
        if (resized == NULL) {
            free(program_name);
            return 0;
        }
        formats->items = resized;
        formats->capacity = capacity;
    }
    formats->items[formats->count].label = label;
    formats->items[formats->count].format = definition->format;
    formats->items[formats->count].program_name = program_name;
    ++formats->count;
    return 1;
}

static int collect_assigned_formats(Unit *unit, const F2cStatement *statement, const char *name,
                                    size_t line, AssignedFormats *formats) {
    if (statement == NULL)
        return 1;
    if (statement->kind == F2C_STMT_ASSIGN_LABEL && statement->name != NULL &&
        strcmp(statement->name, name) == 0 && statement->label_count == 1U &&
        !append_assigned_format(unit, formats, statement->labels[0], line))
        return 0;
    return collect_assigned_formats(unit, statement->nested, name, line, formats);
}

static int emit_assigned_format_selection(Context *context, Unit *unit,
                                          const F2cStatement *io_statement,
                                          const F2cExpr *format_expression, int depth) {
    AssignedFormats formats = {0};
    char *selector = NULL;
    size_t index;
    int result = 0;
    if (format_expression == NULL || format_expression->text == NULL)
        return 0;
    for (index = 0U; index < unit->statement_count; ++index) {
        if (!collect_assigned_formats(unit, &unit->statements[index], format_expression->text,
                                      io_statement->line, &formats))
            goto cleanup;
    }
    selector = f2c_io_emit_required_expression(unit, format_expression);
    if (selector == NULL || formats.count == 0U)
        goto cleanup;
    for (index = 0U; index < formats.count; ++index)
        if (!f2c_io_emit_format_program(context, formats.items[index].format,
                                        formats.items[index].program_name, depth))
            goto cleanup;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output,
                      "const f2c_format_instruction *f2c_io_format_program = NULL;\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_io_format_program_count = 0U;\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "switch ((int32_t)(%s)) {\n", selector);
    for (index = 0U; index < formats.count; ++index) {
        f2c_io_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "case %s: f2c_io_format_program = %s; "
                          "f2c_io_format_program_count = sizeof(%s) / sizeof(%s[0]); break;\n",
                          formats.items[index].label, formats.items[index].program_name,
                          formats.items[index].program_name, formats.items[index].program_name);
    }
    f2c_io_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "default: break;\n");
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
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
    char *format_value = NULL;
    char *format_pointer = NULL;
    char *format_length_expression = NULL;
    char *program_name = NULL;
    size_t index;
    int assigned_format;
    int constant_format;
    int result = 0;
    if (context == NULL || unit == NULL || statement == NULL || format_control == NULL ||
        format_control->asterisk || file == NULL || unit_number == NULL ||
        advance_expression == NULL)
        return 0;
    assigned_format = format_control->value != NULL &&
                      format_control->value->type == TYPE_INTEGER &&
                      format_control->value->kind == F2C_EXPR_NAME;
    constant_format = format_control->format != NULL && format_control->format->validated;
    if (!constant_format && !assigned_format && format_control->value != NULL &&
        format_control->value->type == TYPE_CHARACTER) {
        format_value = f2c_io_emit_required_expression(unit, format_control->value);
        format_pointer =
            format_value != NULL
                ? f2c_character_source_pointer(unit, format_control->value, format_value)
                : NULL;
        format_length_expression = f2c_character_length_expression(unit, format_control->value);
    }
    if (!constant_format && !assigned_format &&
        (format_pointer == NULL || format_length_expression == NULL)) {
        f2c_diagnostic(context, statement->line, 1,
                       "FORMAT label or CHARACTER expression could not be resolved");
        goto cleanup;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    ++depth;
    if (constant_format) {
        program_name = format_program_name("format_program", statement->line, NULL);
        if (program_name == NULL ||
            !f2c_io_emit_format_program(context, format_control->format, program_name, depth)) {
            f2c_diagnostic(context, statement->line, 1,
                           "constant FORMAT could not be lowered to a static program");
            goto cleanup;
        }
    }
    if (assigned_format &&
        !emit_assigned_format_selection(context, unit, statement, format_control->value, depth)) {
        f2c_diagnostic(context, statement->line, 1,
                       "assigned FORMAT variable has no resolvable FORMAT label");
        goto cleanup;
    }
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "f2c_format_state f2c_io_format;\n");
    f2c_io_indent(&context->output, depth);
    if (constant_format) {
        f2c_buffer_printf(&context->output,
                          "f2c_format_initialize_program(&f2c_io_format, %s, %s, "
                          "sizeof(%s) / sizeof(%s[0]), %s);\n",
                          file, program_name, program_name, program_name, input ? "true" : "false");
    } else if (assigned_format) {
        f2c_buffer_printf(&context->output,
                          "f2c_format_initialize_program(&f2c_io_format, %s, "
                          "f2c_io_format_program, f2c_io_format_program_count, %s);\n",
                          file, input ? "true" : "false");
    } else {
        f2c_buffer_printf(&context->output,
                          "f2c_format_initialize(&f2c_io_format, %s, %s, (size_t)(%s), %s);\n",
                          file, format_pointer, format_length_expression, input ? "true" : "false");
    }
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
        f2c_buffer_printf(&context->output,
                          "%s = f2c_stream_error(%s) ? F2C_IO_STATUS_RECORD : "
                          "f2c_io_format.status;\n",
                          status_target, file);
    }
    --depth;
    f2c_io_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    free(format_value);
    free(format_pointer);
    free(format_length_expression);
    free(program_name);
    return result;
}
