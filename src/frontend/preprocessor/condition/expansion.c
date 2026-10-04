#include "frontend/preprocessor/condition/private.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct ExpansionFrame {
    const char *name;
    size_t length;
    const struct ExpansionFrame *parent;
} ExpansionFrame;

typedef PreprocessorMacroArgument ConditionArgument;

void f2c_preprocessor_condition_diagnose(Preprocessor *preprocessor, F2cDiagnosticCode code,
                                         size_t line, size_t column, const char *message) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    ++span.end.column;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, "%s", message);
}

static void diagnose_name(Preprocessor *preprocessor, F2cDiagnosticCode code, size_t line,
                          size_t column, const char *prefix_text, const char *name, size_t length) {
    F2cSourceSpan span = {0};
    span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, column};
    span.end = span.begin;
    span.end.column += length != 0U ? length : 1U;
    f2c_diagnostic_span_code(preprocessor->context, code, &span, 1, "%s '%.*s'", prefix_text,
                             (int)(length > (size_t)INT32_MAX ? (size_t)INT32_MAX : length), name);
}

static int expansion_contains(const ExpansionFrame *frame, const char *name, size_t length) {
    for (; frame != NULL; frame = frame->parent) {
        if (frame->length == length && strncmp(frame->name, name, length) == 0)
            return 1;
    }
    return 0;
}

static size_t quoted_end(const char *text, size_t length, size_t begin) {
    const char quote = text[begin];
    size_t cursor = begin + 1U;
    while (cursor < length) {
        if (text[cursor] == '\\' && cursor + 1U < length) {
            cursor += 2U;
            continue;
        }
        if (text[cursor++] == quote)
            break;
    }
    return cursor;
}

static int reserve_arguments(ConditionArgument **arguments, size_t *capacity, size_t count) {
    ConditionArgument *replacement;
    size_t next_capacity;
    if (count < *capacity)
        return 1;
    next_capacity = *capacity == 0U ? 4U : *capacity * 2U;
    if (next_capacity < *capacity || next_capacity > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement = (ConditionArgument *)realloc(*arguments, next_capacity * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    *arguments = replacement;
    *capacity = next_capacity;
    return 1;
}

static int append_argument(Preprocessor *preprocessor, ConditionArgument **arguments, size_t *count,
                           size_t *capacity, const char *text, size_t begin, size_t end,
                           size_t line, size_t column, const PreprocessorMacro *macro) {
    while (begin < end && isspace((unsigned char)text[begin]) != 0)
        ++begin;
    while (end > begin && isspace((unsigned char)text[end - 1U]) != 0)
        --end;
    if (*count >= preprocessor->context->limits.max_macro_arguments) {
        diagnose_name(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column,
                      "conditional macro argument limit exceeded for", macro->name,
                      macro->name_length);
        return 0;
    }
    if (!reserve_arguments(arguments, capacity, *count)) {
        f2c_preprocessor_condition_diagnose(
            preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, column,
            "out of memory while parsing conditional macro arguments");
        return 0;
    }
    (*arguments)[*count] = (ConditionArgument){text + begin, end - begin, {NULL, 0U, 0U}};
    ++*count;
    return 1;
}

static int parse_invocation_arguments(Preprocessor *preprocessor, const PreprocessorMacro *macro,
                                      const char *text, size_t length, size_t opening, size_t line,
                                      size_t column, ConditionArgument **arguments,
                                      size_t *argument_count, size_t *after) {
    size_t capacity = 0U;
    size_t begin = opening + 1U;
    size_t index = begin;
    size_t depth = 1U;
    int saw_separator = 0;
    *arguments = NULL;
    *argument_count = 0U;
    while (index < length) {
        if (text[index] == '\'' || text[index] == '"') {
            index = quoted_end(text, length, index);
            continue;
        }
        if (text[index] == '(') {
            if (depth >= preprocessor->context->limits.max_parse_depth) {
                diagnose_name(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column + opening,
                              "conditional macro invocation nesting limit exceeded for",
                              macro->name, macro->name_length);
                goto failure;
            }
            ++depth;
            ++index;
            continue;
        }
        if (text[index] == ')') {
            --depth;
            if (depth == 0U) {
                size_t nonspace = begin;
                while (nonspace < index && isspace((unsigned char)text[nonspace]) != 0)
                    ++nonspace;
                if (saw_separator || nonspace != index || macro->parameter_count != 0U ||
                    macro->variadic) {
                    if (!append_argument(preprocessor, arguments, argument_count, &capacity, text,
                                         begin, index, line, column + opening, macro)) {
                        goto failure;
                    }
                }
                if (macro->variadic ? *argument_count < macro->parameter_count
                                    : *argument_count != macro->parameter_count) {
                    diagnose_name(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + opening,
                                  "conditional macro argument count mismatch for", macro->name,
                                  macro->name_length);
                    goto failure;
                }
                *after = index + 1U;
                return 1;
            }
            ++index;
            continue;
        }
        if (text[index] == ',' && depth == 1U) {
            if (!append_argument(preprocessor, arguments, argument_count, &capacity, text, begin,
                                 index, line, column + opening, macro)) {
                goto failure;
            }
            saw_separator = 1;
            begin = ++index;
            continue;
        }
        ++index;
    }
    diagnose_name(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + opening,
                  "unterminated conditional macro invocation for", macro->name, macro->name_length);

failure:
    free(*arguments);
    *arguments = NULL;
    *argument_count = 0U;
    return 0;
}

static int append_expanded_condition(Preprocessor *preprocessor, const char *text, size_t length,
                                     size_t line, size_t column, Buffer *output,
                                     const ExpansionFrame *parent, size_t depth) {
    size_t index = 0U;
    int defined_operand = 0;
    while (index < length) {
        const char value = text[index];
        if (value == '\'' || value == '"') {
            const size_t end = quoted_end(text, length, index);
            f2c_buffer_append_n(output, text + index, end - index);
            index = end;
            continue;
        }
        if (condition_identifier_start(value)) {
            size_t end = index + 1U;
            size_t macro_index;
            while (end < length && condition_identifier_continue(text[end]))
                ++end;
            if (condition_word_equal(text + index, end - index, "defined")) {
                defined_operand = 1;
                f2c_buffer_append_n(output, text + index, end - index);
                index = end;
                continue;
            }
            if (defined_operand) {
                defined_operand = 0;
                f2c_buffer_append_n(output, text + index, end - index);
                index = end;
                continue;
            }
            macro_index = f2c_preprocessor_find_macro(preprocessor, text + index, end - index);
            if (macro_index != SIZE_MAX) {
                const PreprocessorMacro *macro = &preprocessor->macros[macro_index];
                ExpansionFrame frame;
                ConditionArgument *arguments = NULL;
                size_t argument_count = 0U;
                size_t after = end;
                size_t opening = end;
                Buffer operated_replacement = {0};
                const char *replacement_text = macro->value;
                size_t replacement_length = macro->value_length;
                if (macro->function_like) {
                    while (opening < length && isspace((unsigned char)text[opening]) != 0)
                        ++opening;
                    if (opening >= length || text[opening] != '(') {
                        f2c_buffer_append_n(output, text + index, end - index);
                        index = end;
                        continue;
                    }
                }
                if (expansion_contains(parent, text + index, end - index)) {
                    diagnose_name(preprocessor, F2C_DIAGNOSTIC_SYNTAX, line, column + index,
                                  "cyclic conditional macro expansion for", text + index,
                                  end - index);
                    return 0;
                }
                if (depth >= preprocessor->context->limits.max_macro_expansion_depth) {
                    f2c_preprocessor_condition_diagnose(
                        preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column + index,
                        "conditional macro expansion depth limit exceeded");
                    return 0;
                }
                frame.name = macro->name;
                frame.length = macro->name_length;
                frame.parent = parent;
                if (macro->function_like &&
                    !parse_invocation_arguments(preprocessor, macro, text, length, opening, line,
                                                column, &arguments, &argument_count, &after))
                    return 0;
                if (macro->function_like || f2c_preprocessor_replacement_has_operator(macro)) {
                    F2cSourcePosition origin = {preprocessor->current_source_name, line,
                                                column + index};
                    if (!f2c_preprocessor_build_operator_replacement(preprocessor, macro, arguments,
                                                                     argument_count, origin,
                                                                     &operated_replacement)) {
                        free(arguments);
                        free(operated_replacement.data);
                        return 0;
                    }
                    replacement_text =
                        operated_replacement.data != NULL ? operated_replacement.data : "";
                    replacement_length = operated_replacement.length;
                }
                f2c_buffer_append_n(output, " ", 1U);
                if (!append_expanded_condition(preprocessor, replacement_text, replacement_length,
                                               line, column + index, output, &frame, depth + 1U)) {
                    free(arguments);
                    free(operated_replacement.data);
                    return 0;
                }
                free(arguments);
                free(operated_replacement.data);
                f2c_buffer_append_n(output, " ", 1U);
                end = macro->function_like ? after : end;
            } else {
                f2c_buffer_append_n(output, text + index, end - index);
            }
            index = end;
            continue;
        }
        f2c_buffer_append_n(output, text + index, 1U);
        ++index;
    }
    if (output->failed || output->limit_exceeded) {
        f2c_preprocessor_condition_diagnose(
            preprocessor,
            output->limit_exceeded ? F2C_DIAGNOSTIC_RESOURCE_LIMIT : F2C_DIAGNOSTIC_OUT_OF_MEMORY,
            line, column,
            output->limit_exceeded ? "expanded preprocessor expression is too large"
                                   : "out of memory while expanding a preprocessor expression");
        return 0;
    }
    return 1;
}

int f2c_preprocessor_expand_condition(Preprocessor *preprocessor, const char *text, size_t length,
                                      size_t line, size_t column, Buffer *output) {
    return append_expanded_condition(preprocessor, text, length, line, column, output, NULL, 0U);
}
