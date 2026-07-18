#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int identifier_start(char value) {
    return isalpha((unsigned char)value) != 0 || value == '_';
}

static int identifier_continue(char value) {
    return isalnum((unsigned char)value) != 0 || value == '_';
}

static const char *skip_space(const char *cursor) {
    while (isspace((unsigned char)*cursor) != 0)
        ++cursor;
    return cursor;
}

static int word_equal(const char *begin, size_t length, const char *word) {
    return strlen(word) == length && strncmp(begin, word, length) == 0;
}

static void diagnose_at(Preprocessor *preprocessor, F2cDiagnosticCode code, size_t line,
                        size_t column, const char *message) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    ++span.end.column;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, "%s", message);
}

static void diagnose_name(Preprocessor *preprocessor, F2cDiagnosticCode code, size_t line,
                          size_t column, const char *prefix, const char *name, size_t length) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    span.end.column += length != 0U ? length : 1U;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, "%s '%.*s'", prefix,
                             (int)(length > (size_t)INT32_MAX ? (size_t)INT32_MAX : length), name);
}

static void discard_preprocessor(Preprocessor *preprocessor) {
    size_t index;
    for (index = 0U; index < preprocessor->macro_count; ++index) {
        free(preprocessor->macros[index].name);
        free(preprocessor->macros[index].value);
    }
    free(preprocessor->macros);
    free(preprocessor->conditionals);
    memset(preprocessor, 0, sizeof(*preprocessor));
}

size_t f2c_preprocessor_find_macro(const Preprocessor *preprocessor, const char *name,
                                   size_t length) {
    size_t index;
    if (preprocessor->macros == NULL)
        return SIZE_MAX;
    for (index = 0U; index < preprocessor->macro_count; ++index) {
        const PreprocessorMacro *macro = &preprocessor->macros[index];
        if (macro->name_length == length && strncmp(macro->name, name, length) == 0)
            return index;
    }
    return SIZE_MAX;
}

static int reserve_macros(Preprocessor *preprocessor) {
    PreprocessorMacro *replacement;
    size_t capacity;
    if (preprocessor->macro_count < preprocessor->macro_capacity)
        return 1;
    capacity = preprocessor->macro_capacity == 0U ? 8U : preprocessor->macro_capacity * 2U;
    if (capacity < preprocessor->macro_capacity || capacity > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement =
        (PreprocessorMacro *)realloc(preprocessor->macros, capacity * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    preprocessor->macros = replacement;
    preprocessor->macro_capacity = capacity;
    return 1;
}

static int valid_macro_name(const char *name, size_t length) {
    size_t index;
    if (length == 0U || !identifier_start(name[0]))
        return 0;
    for (index = 1U; index < length; ++index) {
        if (!identifier_continue(name[index]))
            return 0;
    }
    return 1;
}

static int set_macro(Preprocessor *preprocessor, const char *name, size_t name_length,
                     const char *value, size_t value_length, size_t line, size_t column,
                     const char *definition_source_name, size_t definition_column,
                     F2cDiagnosticCode invalid_code) {
    const size_t existing = f2c_preprocessor_find_macro(preprocessor, name, name_length);
    char *owned_name = NULL;
    char *owned_value;
    if (!valid_macro_name(name, name_length)) {
        diagnose_name(preprocessor, invalid_code, line, column, "invalid preprocessor name", name,
                      name_length);
        return 0;
    }
    owned_value = f2c_strdup_n(value, value_length);
    if (existing == SIZE_MAX)
        owned_name = f2c_strdup_n(name, name_length);
    if (owned_value == NULL || (existing == SIZE_MAX && owned_name == NULL)) {
        free(owned_name);
        free(owned_value);
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                    "out of memory while defining a preprocessor name");
        return 0;
    }
    if (existing != SIZE_MAX && preprocessor->macros != NULL) {
        free(preprocessor->macros[existing].value);
        preprocessor->macros[existing].value = owned_value;
        preprocessor->macros[existing].value_length = value_length;
        preprocessor->macros[existing].definition_source_name = definition_source_name;
        preprocessor->macros[existing].definition_line = line;
        preprocessor->macros[existing].definition_column = definition_column;
        return 1;
    }
    if (preprocessor->macro_count >= preprocessor->context->limits.max_preprocessor_definitions) {
        free(owned_name);
        free(owned_value);
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                    "preprocessor definition limit exceeded");
        return 0;
    }
    if (!reserve_macros(preprocessor)) {
        free(owned_name);
        free(owned_value);
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                    "out of memory while growing the preprocessor definition table");
        return 0;
    }
    preprocessor->macros[preprocessor->macro_count++] = (PreprocessorMacro){
        owned_name, name_length,      owned_value, value_length, definition_source_name,
        line,       definition_column};
    return 1;
}

static void undefine_macro(Preprocessor *preprocessor, const char *name, size_t length) {
    const size_t index = f2c_preprocessor_find_macro(preprocessor, name, length);
    if (index == SIZE_MAX)
        return;
    free(preprocessor->macros[index].name);
    free(preprocessor->macros[index].value);
    --preprocessor->macro_count;
    if (index != preprocessor->macro_count)
        preprocessor->macros[index] = preprocessor->macros[preprocessor->macro_count];
}

static int initialize_preprocessor(Preprocessor *preprocessor, Context *context) {
    size_t index;
    memset(preprocessor, 0, sizeof(*preprocessor));
    preprocessor->context = context;
    preprocessor->active = 1;
    preprocessor->current_source_name = f2c_context_source_name(
        context, context->options != NULL ? context->options->source_name : NULL);
    preprocessor->current_line = 1U;
    if (preprocessor->current_source_name == NULL) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1U,
                    "out of memory while initializing a preprocessor source location");
        return 0;
    }
    if (context->preprocessor_definition_count != 0U && context->preprocessor_definitions == NULL) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_INVALID_ARGUMENT, 1U, 1U,
                    "preprocessor definitions are null but their count is nonzero");
        return 0;
    }
    if (context->preprocessor_definition_count > context->limits.max_preprocessor_definitions) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, 1U, 1U,
                    "preprocessor definition limit exceeded");
        return 0;
    }
    for (index = 0U; index < context->preprocessor_definition_count; ++index) {
        const F2cPreprocessorDefinition *definition = &context->preprocessor_definitions[index];
        const char *value = definition->value != NULL ? definition->value : "1";
        size_t name_length = 0U;
        size_t value_length = 0U;
        const char *definition_source_name;
        if (definition->name == NULL) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_INVALID_ARGUMENT, 1U, 1U,
                        "preprocessor definition has a null name");
            return 0;
        }
        while (definition->name[name_length] != '\0') {
            if (name_length == context->limits.max_preprocessed_bytes) {
                diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, 1U, 1U,
                            "preprocessor definition name is too large");
                return 0;
            }
            ++name_length;
        }
        while (value[value_length] != '\0') {
            if (value_length == context->limits.max_preprocessed_bytes) {
                diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, 1U, 1U,
                            "preprocessor definition value is too large");
                return 0;
            }
            ++value_length;
        }
        if (memchr(value, '\n', value_length) != NULL ||
            memchr(value, '\r', value_length) != NULL) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_INVALID_ARGUMENT, 1U, 1U,
                        "preprocessor definition values cannot contain line breaks");
            return 0;
        }
        definition_source_name = f2c_context_source_name(context, "<command-line>");
        if (definition_source_name == NULL) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1U,
                        "out of memory while recording a preprocessor definition location");
            return 0;
        }
        if (!set_macro(preprocessor, definition->name, name_length, value, value_length, 1U, 1U,
                       definition_source_name, 1U, F2C_DIAGNOSTIC_INVALID_ARGUMENT))
            return 0;
    }
    return 1;
}

static int reserve_conditionals(Preprocessor *preprocessor) {
    ConditionalFrame *replacement;
    size_t capacity;
    if (preprocessor->conditional_count < preprocessor->conditional_capacity)
        return 1;
    capacity =
        preprocessor->conditional_capacity == 0U ? 8U : preprocessor->conditional_capacity * 2U;
    if (capacity < preprocessor->conditional_capacity || capacity > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement =
        (ConditionalFrame *)realloc(preprocessor->conditionals, capacity * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    preprocessor->conditionals = replacement;
    preprocessor->conditional_capacity = capacity;
    return 1;
}

static int push_conditional(Preprocessor *preprocessor, int condition, size_t line, size_t column) {
    ConditionalFrame frame;
    if (preprocessor->conditional_count >= preprocessor->context->limits.max_parse_depth) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                    "conditional preprocessing nesting limit exceeded");
        return 0;
    }
    if (!reserve_conditionals(preprocessor)) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                    "out of memory while nesting conditional preprocessing");
        return 0;
    }
    frame.opening_line = line;
    frame.opening_source_name = preprocessor->current_source_name;
    frame.parent_active = preprocessor->active;
    frame.branch_taken = condition;
    frame.else_seen = 0;
    preprocessor->conditionals[preprocessor->conditional_count++] = frame;
    preprocessor->active = frame.parent_active && condition;
    return 1;
}

static int parse_name_operand(Preprocessor *preprocessor, const char *text, size_t line,
                              size_t column, const char **name, size_t *length) {
    const char *cursor = skip_space(text);
    const char *end;
    if (!identifier_start(*cursor)) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + (size_t)(cursor - text),
                    "expected a preprocessor name");
        return 0;
    }
    end = cursor + 1;
    while (identifier_continue(*end))
        ++end;
    if (*skip_space(end) != '\0') {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + (size_t)(end - text),
                    "unexpected text after preprocessor name");
        return 0;
    }
    *name = cursor;
    *length = (size_t)(end - cursor);
    return 1;
}

static int process_conditional_start(Preprocessor *preprocessor, const char *directive,
                                     size_t directive_length, const char *rest, size_t line,
                                     size_t column) {
    int condition = 0;
    if (word_equal(directive, directive_length, "ifdef") ||
        word_equal(directive, directive_length, "ifndef")) {
        const char *name;
        size_t length;
        if (!parse_name_operand(preprocessor, rest, line, column, &name, &length))
            return 0;
        condition = f2c_preprocessor_find_macro(preprocessor, name, length) != SIZE_MAX;
        if (word_equal(directive, directive_length, "ifndef"))
            condition = !condition;
    } else {
        if (*skip_space(rest) == '\0') {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                        "missing #if expression");
            return 0;
        }
        if (!f2c_preprocessor_evaluate_condition(preprocessor, rest, line, column,
                                                 preprocessor->active, &condition))
            return 0;
    }
    return push_conditional(preprocessor, condition, line, column);
}

static int process_elif(Preprocessor *preprocessor, const char *rest, size_t line, size_t column) {
    ConditionalFrame *frame;
    int condition = 0;
    if (preprocessor->conditional_count == preprocessor->conditional_base) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column, "#elif has no matching #if");
        return 0;
    }
    frame = &preprocessor->conditionals[preprocessor->conditional_count - 1U];
    if (frame->else_seen) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column, "#elif cannot follow #else");
        return 0;
    }
    if (*skip_space(rest) == '\0') {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column, "missing #elif expression");
        return 0;
    }
    if (!f2c_preprocessor_evaluate_condition(preprocessor, rest, line, column,
                                             frame->parent_active && !frame->branch_taken,
                                             &condition))
        return 0;
    preprocessor->active = frame->parent_active && !frame->branch_taken && condition;
    if (condition)
        frame->branch_taken = 1;
    return 1;
}

static int process_else(Preprocessor *preprocessor, const char *rest, size_t line, size_t column) {
    ConditionalFrame *frame;
    if (*skip_space(rest) != '\0') {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                    "unexpected text after #else");
        return 0;
    }
    if (preprocessor->conditional_count == preprocessor->conditional_base) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column, "#else has no matching #if");
        return 0;
    }
    frame = &preprocessor->conditionals[preprocessor->conditional_count - 1U];
    if (frame->else_seen) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                    "conditional group contains more than one #else");
        return 0;
    }
    frame->else_seen = 1;
    preprocessor->active = frame->parent_active && !frame->branch_taken;
    frame->branch_taken = 1;
    return 1;
}

static int process_endif(Preprocessor *preprocessor, const char *rest, size_t line, size_t column) {
    ConditionalFrame frame;
    if (*skip_space(rest) != '\0') {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                    "unexpected text after #endif");
        return 0;
    }
    if (preprocessor->conditional_count == preprocessor->conditional_base) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                    "#endif has no matching #if");
        return 0;
    }
    frame = preprocessor->conditionals[--preprocessor->conditional_count];
    preprocessor->active = frame.parent_active;
    return 1;
}

static int process_define(Preprocessor *preprocessor, const char *rest, size_t line,
                          size_t column) {
    const char *name = skip_space(rest);
    const char *end;
    const char *value;
    if (!identifier_start(*name)) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column,
                    "expected a name after #define");
        return 0;
    }
    end = name + 1;
    while (identifier_continue(*end))
        ++end;
    if (*end == '(') {
        diagnose_name(preprocessor, F2C_DIAGNOSTIC_UNSUPPORTED, line,
                      column + (size_t)(name - rest), "function-like macro is not supported", name,
                      (size_t)(end - name));
        return 0;
    }
    value = skip_space(end);
    {
        const char *source_name = preprocessor->current_source_name;
        if (source_name == NULL) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                        "out of memory while recording a preprocessor definition location");
            return 0;
        }
        return set_macro(preprocessor, name, (size_t)(end - name), value, strlen(value), line,
                         column + (size_t)(name - rest), source_name,
                         column + (size_t)(value - rest), F2C_DIAGNOSTIC_SYNTAX);
    }
}

static int process_line_directive(Preprocessor *preprocessor, const char *rest, size_t line,
                                  size_t column) {
    const char *cursor = skip_space(rest);
    char *number_end = NULL;
    unsigned long long parsed;
    const char *source_name = preprocessor->current_source_name;
    errno = 0;
    parsed = strtoull(cursor, &number_end, 10);
    if (errno == ERANGE || number_end == cursor || parsed == 0ULL ||
        parsed > (unsigned long long)SIZE_MAX) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + (size_t)(cursor - rest),
                    "invalid source line number after #line");
        return 0;
    }
    cursor = skip_space(number_end);
    if (*cursor == '"') {
        Buffer name = {0};
        int closed = 0;
        ++cursor;
        while (*cursor != '\0') {
            char value = *cursor++;
            if (value == '"') {
                closed = 1;
                break;
            }
            if (value == '\\' && (*cursor == '\\' || *cursor == '"'))
                value = *cursor++;
            f2c_buffer_append_n(&name, &value, 1U);
        }
        if (!closed || name.failed) {
            free(name.data);
            diagnose_at(preprocessor,
                        name.failed ? F2C_DIAGNOSTIC_OUT_OF_MEMORY : F2C_DIAGNOSTIC_SYNTAX, line,
                        column + (size_t)(cursor - rest),
                        name.failed ? "out of memory parsing #line source name"
                                    : "unterminated source name after #line");
            return 0;
        }
        source_name =
            f2c_context_source_name(preprocessor->context, name.data != NULL ? name.data : "");
        free(name.data);
        if (source_name == NULL) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
                        "out of memory recording #line source name");
            return 0;
        }
    }
    cursor = skip_space(cursor);
    while (*cursor != '\0') {
        char *flag_end = NULL;
        unsigned long flag;
        errno = 0;
        flag = strtoul(cursor, &flag_end, 10);
        if (errno == ERANGE || flag_end == cursor || flag < 1UL || flag > 4UL) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + (size_t)(cursor - rest),
                        "invalid flag after source line marker");
            return 0;
        }
        cursor = skip_space(flag_end);
    }
    preprocessor->pending_source_name = source_name;
    preprocessor->pending_line = (size_t)parsed;
    preprocessor->location_pending = 1;
    return 1;
}

static int process_directive(Preprocessor *preprocessor, const char *line_text, size_t line_number,
                             int *is_directive) {
    const char *hash = skip_space(line_text);
    const char *directive;
    const char *rest;
    size_t directive_length;
    const size_t column = (size_t)(hash - line_text) + 1U;
    size_t rest_column;
    *is_directive = *hash == '#';
    if (!*is_directive)
        return 1;
    directive = skip_space(hash + 1);
    if (*directive == '\0')
        return 1;
    if (isdigit((unsigned char)*directive) != 0)
        return preprocessor->active ? process_line_directive(preprocessor, directive, line_number,
                                                             (size_t)(directive - line_text) + 1U)
                                    : 1;
    rest = directive;
    if (!identifier_start(*rest)) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line_number,
                    (size_t)(rest - line_text) + 1U, "malformed preprocessor directive");
        return 0;
    }
    ++rest;
    while (identifier_continue(*rest))
        ++rest;
    directive_length = (size_t)(rest - directive);
    rest_column = (size_t)(rest - line_text) + 1U;
    if (word_equal(directive, directive_length, "if") ||
        word_equal(directive, directive_length, "ifdef") ||
        word_equal(directive, directive_length, "ifndef"))
        return process_conditional_start(preprocessor, directive, directive_length, rest,
                                         line_number, rest_column);
    if (word_equal(directive, directive_length, "elif"))
        return process_elif(preprocessor, rest, line_number, rest_column);
    if (word_equal(directive, directive_length, "else"))
        return process_else(preprocessor, rest, line_number, column);
    if (word_equal(directive, directive_length, "endif"))
        return process_endif(preprocessor, rest, line_number, column);
    if (!preprocessor->active)
        return 1;
    if (word_equal(directive, directive_length, "define"))
        return process_define(preprocessor, rest, line_number, rest_column);
    if (word_equal(directive, directive_length, "line"))
        return process_line_directive(preprocessor, rest, line_number, rest_column);
    if (word_equal(directive, directive_length, "include"))
        return f2c_preprocessor_process_include(preprocessor, rest, line_number, column);
    if (word_equal(directive, directive_length, "undef")) {
        const char *name;
        size_t length;
        if (!parse_name_operand(preprocessor, rest, line_number, rest_column, &name, &length))
            return 0;
        undefine_macro(preprocessor, name, length);
        return 1;
    }
    if (word_equal(directive, directive_length, "error")) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line_number, column,
                    *skip_space(rest) != '\0' ? skip_space(rest) : "active #error directive");
        return 0;
    }
    diagnose_name(preprocessor, F2C_DIAGNOSTIC_UNSUPPORTED, line_number, column,
                  "unsupported preprocessor directive", directive, directive_length);
    return 0;
}

int f2c_preprocessor_append(Preprocessor *preprocessor, Buffer *output, F2cSourceMap *source_map,
                            const char *text, size_t length, F2cSourcePosition expansion,
                            size_t expansion_width, unsigned char expansion_column_step,
                            F2cSourcePosition spelling, size_t spelling_width,
                            unsigned char spelling_column_step, int has_spelling) {
    const size_t consumed = preprocessor->context->preprocessed_bytes;
    const size_t limit = preprocessor->context->limits.max_preprocessed_bytes;
    const size_t logical_begin = output->length;
    if (consumed > limit || output->length > limit - consumed ||
        length > limit - consumed - output->length) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, expansion.line, expansion.column,
                    "preprocessed source limit exceeded");
        return 0;
    }
    f2c_buffer_append_n(output, text, length);
    if (output->failed) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, expansion.line, expansion.column,
                    "out of memory while preprocessing source");
        return 0;
    }
    if (!f2c_source_map_append(source_map, logical_begin, length, expansion, expansion_width,
                               expansion_column_step, spelling, spelling_width,
                               spelling_column_step, has_spelling)) {
        diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, expansion.line, expansion.column,
                    "out of memory while mapping preprocessed source");
        return 0;
    }
    return 1;
}

static const char *fortran_include_operand(const char *line, F2cSourceForm form, size_t *column) {
    const char *cursor = line;
    static const char keyword[] = "include";
    size_t index;
    if (form == F2C_SOURCE_FIXED) {
        if (*cursor == 'c' || *cursor == 'C' || *cursor == '*' || *cursor == '!')
            return NULL;
        cursor += strlen(cursor) > 6U ? 6U : strlen(cursor);
    }
    cursor = skip_space(cursor);
    for (index = 0U; index < sizeof(keyword) - 1U; ++index) {
        if (tolower((unsigned char)cursor[index]) != keyword[index])
            return NULL;
    }
    cursor += sizeof(keyword) - 1U;
    if (!isspace((unsigned char)*cursor))
        return NULL;
    cursor = skip_space(cursor);
    if (*cursor != '\'' && *cursor != '"')
        return NULL;
    *column = (size_t)(cursor - line) + 1U;
    return cursor;
}

int f2c_preprocessor_process_buffer(Preprocessor *preprocessor, const char *source, size_t length,
                                    F2cSourceForm form, const char *source_name, size_t depth,
                                    const PreprocessorIncludeFrame *include_parent, Buffer *output,
                                    F2cSourceMap *source_map) {
    const char *saved_source_name = preprocessor->current_source_name;
    const size_t saved_line = preprocessor->current_line;
    const char *saved_pending_source_name = preprocessor->pending_source_name;
    const size_t saved_pending_line = preprocessor->pending_line;
    const int saved_location_pending = preprocessor->location_pending;
    const int saved_active = preprocessor->active;
    Buffer *saved_output = preprocessor->output;
    F2cSourceMap *saved_output_source_map = preprocessor->output_source_map;
    const F2cSourceForm saved_form = preprocessor->source_form;
    const size_t saved_depth = preprocessor->include_depth;
    const PreprocessorIncludeFrame *saved_include_parent = preprocessor->include_parent;
    const size_t saved_conditional_base = preprocessor->conditional_base;
    const size_t conditional_base = preprocessor->conditional_count;
    PreprocessorIncludeFrame frame;
    size_t offset = 0U;
    int succeeded = 0;
    frame.source_name = source_name;
    frame.parent = include_parent;
    preprocessor->current_source_name = source_name;
    preprocessor->current_line = 1U;
    preprocessor->pending_source_name = NULL;
    preprocessor->pending_line = 0U;
    preprocessor->location_pending = 0;
    preprocessor->active = 1;
    preprocessor->output = output;
    preprocessor->output_source_map = source_map;
    preprocessor->source_form = form;
    preprocessor->include_depth = depth;
    preprocessor->include_parent = &frame;
    preprocessor->conditional_base = conditional_base;
    if (length >= 3U && (unsigned char)source[0] == 0xEFU && (unsigned char)source[1] == 0xBBU &&
        (unsigned char)source[2] == 0xBFU)
        offset = 3U;
    while (offset < length) {
        const size_t begin = offset;
        const size_t mapped_line = preprocessor->current_line;
        const char *mapped_source_name = preprocessor->current_source_name;
        size_t end = offset;
        size_t newline_end;
        char *physical_line;
        int directive = 0;
        while (end < length && source[end] != '\n' && source[end] != '\r')
            ++end;
        newline_end = end;
        if (newline_end < length && source[newline_end] == '\r') {
            ++newline_end;
            if (newline_end < length && source[newline_end] == '\n')
                ++newline_end;
        } else if (newline_end < length) {
            ++newline_end;
        }
        physical_line = f2c_strdup_n(source + begin, end - begin);
        if (physical_line == NULL) {
            diagnose_at(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, mapped_line, 1U,
                        "out of memory while preprocessing a source line");
            goto cleanup;
        }
        if (!process_directive(preprocessor, physical_line, mapped_line, &directive)) {
            free(physical_line);
            goto cleanup;
        }
        if (!directive && preprocessor->active) {
            size_t include_column = 0U;
            const char *include_operand =
                fortran_include_operand(physical_line, form, &include_column);
            if (include_operand != NULL) {
                directive = 1;
                if (!f2c_preprocessor_process_include(preprocessor, include_operand, mapped_line,
                                                      include_column)) {
                    free(physical_line);
                    goto cleanup;
                }
            }
        }
        if (!directive && preprocessor->active) {
            if (!f2c_preprocessor_expand_source_line(preprocessor, source + begin, end - begin,
                                                     mapped_line, form, output, source_map)) {
                free(physical_line);
                goto cleanup;
            }
        }
        free(physical_line);
        {
            F2cSourcePosition origin;
            origin.source_name = mapped_source_name;
            origin.line = mapped_line;
            origin.column = end - begin + 1U;
            if (origin.source_name == NULL ||
                !f2c_preprocessor_append(preprocessor, output, source_map, source + end,
                                         newline_end - end, origin, newline_end - end, 1U, origin,
                                         newline_end - end, 1U, 0))
                goto cleanup;
        }
        offset = newline_end;
        if (preprocessor->location_pending) {
            preprocessor->current_source_name = preprocessor->pending_source_name;
            preprocessor->current_line = preprocessor->pending_line;
            preprocessor->location_pending = 0;
        } else if (offset < length) {
            if (preprocessor->current_line == SIZE_MAX) {
                diagnose_at(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, mapped_line, 1U,
                            "source line number exceeds the representable range");
                goto cleanup;
            }
            ++preprocessor->current_line;
        }
    }
    if (preprocessor->conditional_count != conditional_base) {
        const ConditionalFrame *conditional =
            &preprocessor->conditionals[preprocessor->conditional_count - 1U];
        F2cSourceSpan span = {0};
        span.begin =
            (F2cSourcePosition){conditional->opening_source_name, conditional->opening_line, 1U};
        span.end = span.begin;
        ++span.end.column;
        f2c_diagnostic_span_code(preprocessor->context, F2C_DIAGNOSTIC_SYNTAX, &span, 1,
                                 "unterminated conditional preprocessing group opened here");
        goto cleanup;
    }
    succeeded = 1;

cleanup:
    preprocessor->conditional_count = conditional_base;
    preprocessor->current_source_name = saved_source_name;
    preprocessor->current_line = saved_line;
    preprocessor->pending_source_name = saved_pending_source_name;
    preprocessor->pending_line = saved_pending_line;
    preprocessor->location_pending = saved_location_pending;
    preprocessor->active = saved_active;
    preprocessor->output = saved_output;
    preprocessor->output_source_map = saved_output_source_map;
    preprocessor->source_form = saved_form;
    preprocessor->include_depth = saved_depth;
    preprocessor->include_parent = saved_include_parent;
    preprocessor->conditional_base = saved_conditional_base;
    return succeeded;
}

int f2c_preprocess_source(Context *context, const char *source, size_t length, F2cSourceForm form,
                          F2cPreprocessedSource *result) {
    Preprocessor preprocessor;
    Buffer output = {0};
    const char *source_name;
    int succeeded = 0;
    if (result == NULL)
        return 0;
    memset(result, 0, sizeof(*result));
    if (!initialize_preprocessor(&preprocessor, context))
        goto cleanup;
    source_name = preprocessor.current_source_name;
    if (!f2c_preprocessor_process_buffer(&preprocessor, source, length, form, source_name, 0U, NULL,
                                         &output, &result->source_map))
        goto cleanup;
    result->length = output.length;
    result->text = f2c_buffer_take(&output);
    if (result->text == NULL) {
        result->text = f2c_strdup("");
        if (result->text == NULL) {
            diagnose_at(&preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1U,
                        "out of memory while finalizing preprocessed source");
            goto cleanup;
        }
    }
    context->preprocessed_bytes += result->length;
    succeeded = 1;

cleanup:
    free(output.data);
    discard_preprocessor(&preprocessor);
    if (!succeeded)
        f2c_preprocessed_source_discard(result);
    return succeeded;
}

void f2c_preprocessed_source_discard(F2cPreprocessedSource *source) {
    if (source == NULL)
        return;
    free(source->text);
    f2c_source_map_discard(&source->source_map);
    memset(source, 0, sizeof(*source));
}
