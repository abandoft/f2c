#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ExpansionFrame {
    const char *name;
    size_t length;
    const struct ExpansionFrame *parent;
} ExpansionFrame;

typedef struct ExpansionOrigin {
    F2cSourcePosition position;
    size_t width;
} ExpansionOrigin;

typedef PreprocessorMacroArgument MacroArgument;

typedef struct MacroInvocation {
    const PreprocessorMacro *macro;
    const MacroArgument *arguments;
    size_t argument_count;
    const ExpansionFrame *argument_parent;
} MacroInvocation;

static int identifier_start(char value) {
    return isalpha((unsigned char)value) != 0 || value == '_';
}

static int identifier_continue(char value) {
    return isalnum((unsigned char)value) != 0 || value == '_';
}

static size_t quoted_end(const char *text, size_t length, size_t begin, int *closed) {
    const char quote = text[begin];
    size_t cursor = begin + 1U;
    if (closed != NULL)
        *closed = 0;
    while (cursor < length) {
        if (text[cursor++] == quote) {
            if (cursor < length && text[cursor] == quote) {
                ++cursor;
            } else {
                if (closed != NULL)
                    *closed = 1;
                break;
            }
        }
    }
    return cursor;
}

static size_t hollerith_end(const char *text, size_t length, size_t begin, size_t *remaining) {
    size_t cursor = begin;
    size_t count = 0U;
    if (remaining != NULL)
        *remaining = 0U;
    while (cursor < length && isdigit((unsigned char)text[cursor]) != 0) {
        const unsigned digit = (unsigned)(text[cursor] - '0');
        if (count > (SIZE_MAX - digit) / 10U)
            return begin;
        count = count * 10U + digit;
        ++cursor;
    }
    if (cursor == begin || cursor >= length || (text[cursor] != 'h' && text[cursor] != 'H'))
        return begin;
    ++cursor;
    if (count <= length - cursor)
        return cursor + count;
    if (remaining != NULL)
        *remaining = count - (length - cursor);
    return length;
}

static int line_has_trailing_ampersand(const char *text, size_t length) {
    while (length != 0U && isspace((unsigned char)text[length - 1U]) != 0)
        --length;
    return length != 0U && text[length - 1U] == '&';
}

static char continued_quote_in_line(const char *text, size_t length, size_t begin) {
    size_t index = begin;
    while (index < length) {
        if (text[index] == '!')
            return '\0';
        if (text[index] == '\'' || text[index] == '"') {
            int closed;
            const size_t end = quoted_end(text, length, index, &closed);
            if (!closed)
                return line_has_trailing_ampersand(text, length) ? text[index] : '\0';
            index = end;
            continue;
        }
        if (isdigit((unsigned char)text[index]) != 0) {
            const size_t hollerith = hollerith_end(text, length, index, NULL);
            if (hollerith != index) {
                index = hollerith;
                continue;
            }
        }
        ++index;
    }
    return '\0';
}

static size_t continued_literal_end(const char *text, size_t length, char quote, int skip_ampersand,
                                    int *closed) {
    size_t cursor = 0U;
    while (cursor < length && isspace((unsigned char)text[cursor]) != 0)
        ++cursor;
    if (skip_ampersand && cursor < length && text[cursor] == '&')
        ++cursor;
    *closed = 0;
    while (cursor < length) {
        if (text[cursor++] != quote)
            continue;
        if (cursor < length && text[cursor] == quote) {
            ++cursor;
            continue;
        }
        *closed = 1;
        break;
    }
    return cursor;
}

static int fixed_continuation_line(const char *text, size_t length) {
    return length > 5U && text[5] != ' ' && text[5] != '0';
}

static void find_fixed_continuation_state(const char *text, size_t length, size_t begin,
                                          char *quote, size_t *hollerith_characters) {
    size_t index = begin;
    *quote = '\0';
    *hollerith_characters = 0U;
    while (index < length) {
        if (text[index] == '!')
            return;
        if (text[index] == '\'' || text[index] == '"') {
            int closed;
            const size_t end = quoted_end(text, length, index, &closed);
            if (!closed) {
                *quote = text[index];
                return;
            }
            index = end;
            continue;
        }
        if (isdigit((unsigned char)text[index]) != 0) {
            size_t remaining;
            const size_t end = hollerith_end(text, length, index, &remaining);
            if (end != index) {
                if (remaining != 0U) {
                    *hollerith_characters = remaining;
                    return;
                }
                index = end;
                continue;
            }
        }
        ++index;
    }
}

static int expansion_contains(const ExpansionFrame *frame, const char *name, size_t length) {
    for (; frame != NULL; frame = frame->parent) {
        if (frame->length == length && strncmp(frame->name, name, length) == 0)
            return 1;
    }
    return 0;
}

static void diagnose_expansion(Preprocessor *preprocessor, F2cDiagnosticCode code,
                               F2cSourcePosition position, const char *message, const char *name,
                               size_t name_length);

static int reserve_arguments(MacroArgument **arguments, size_t *capacity, size_t count) {
    MacroArgument *replacement;
    size_t next_capacity;
    if (count < *capacity)
        return 1;
    next_capacity = *capacity == 0U ? 4U : *capacity * 2U;
    if (next_capacity < *capacity || next_capacity > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement = (MacroArgument *)realloc(*arguments, next_capacity * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    *arguments = replacement;
    *capacity = next_capacity;
    return 1;
}

static int append_argument(Preprocessor *preprocessor, MacroArgument **arguments, size_t *count,
                           size_t *capacity, const char *text, size_t begin, size_t end,
                           F2cSourcePosition text_origin, F2cSourcePosition diagnostic_origin,
                           const PreprocessorMacro *macro) {
    F2cSourcePosition origin = text_origin;
    while (begin < end && isspace((unsigned char)text[begin]) != 0)
        ++begin;
    while (end > begin && isspace((unsigned char)text[end - 1U]) != 0)
        --end;
    if (*count >= preprocessor->context->limits.max_macro_arguments) {
        diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, diagnostic_origin,
                           "function-like macro argument limit exceeded for", macro->name,
                           macro->name_length);
        return 0;
    }
    if (!reserve_arguments(arguments, capacity, *count)) {
        diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_OUT_OF_MEMORY, diagnostic_origin,
                           "out of memory while parsing arguments for", macro->name,
                           macro->name_length);
        return 0;
    }
    origin.column += begin;
    (*arguments)[*count] = (MacroArgument){text + begin, end - begin, origin};
    ++*count;
    return 1;
}

static int invocation_arity_matches(const PreprocessorMacro *macro, size_t argument_count) {
    return macro->variadic ? argument_count >= macro->parameter_count
                           : argument_count == macro->parameter_count;
}

static int parse_invocation_arguments(Preprocessor *preprocessor, const PreprocessorMacro *macro,
                                      const char *text, size_t length, size_t opening,
                                      F2cSourcePosition text_origin,
                                      F2cSourcePosition diagnostic_origin,
                                      MacroArgument **arguments, size_t *argument_count,
                                      size_t *after) {
    size_t capacity = 0U;
    size_t begin = opening + 1U;
    size_t index = begin;
    size_t depth = 1U;
    int saw_separator = 0;
    *arguments = NULL;
    *argument_count = 0U;
    while (index < length) {
        if (text[index] == '\'' || text[index] == '"') {
            index = quoted_end(text, length, index, NULL);
            continue;
        }
        if (isdigit((unsigned char)text[index]) != 0) {
            const size_t hollerith = hollerith_end(text, length, index, NULL);
            if (hollerith != index) {
                index = hollerith;
                continue;
            }
        }
        if (text[index] == '(') {
            if (depth >= preprocessor->context->limits.max_parse_depth) {
                diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT, diagnostic_origin,
                                   "macro invocation nesting limit exceeded for", macro->name,
                                   macro->name_length);
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
                                         begin, index, text_origin, diagnostic_origin, macro)) {
                        goto failure;
                    }
                }
                if (!invocation_arity_matches(macro, *argument_count)) {
                    diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_SYNTAX, diagnostic_origin,
                                       "function-like macro argument count mismatch for",
                                       macro->name, macro->name_length);
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
                                 index, text_origin, diagnostic_origin, macro)) {
                goto failure;
            }
            saw_separator = 1;
            begin = ++index;
            continue;
        }
        ++index;
    }
    diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_SYNTAX, diagnostic_origin,
                       "unterminated function-like macro invocation for", macro->name,
                       macro->name_length);

failure:
    free(*arguments);
    *arguments = NULL;
    *argument_count = 0U;
    return 0;
}

static void diagnose_expansion(Preprocessor *preprocessor, F2cDiagnosticCode code,
                               F2cSourcePosition position, const char *message, const char *name,
                               size_t name_length) {
    F2cSourceSpan span = {0};
    span.begin = position;
    span.end = position;
    span.end.column += name_length != 0U ? name_length : 1U;
    f2c_diagnostic_span_code(
        preprocessor->context, code, &span, 1, "%s '%.*s'", message,
        (int)(name_length > (size_t)INT32_MAX ? (size_t)INT32_MAX : name_length), name);
}

static int macro_parameter(const PreprocessorMacro *macro, const char *name, size_t length,
                           size_t *parameter, int *variadic) {
    size_t index;
    for (index = 0U; index < macro->parameter_count; ++index) {
        const PreprocessorMacroParameter *formal = &macro->parameters[index];
        if (formal->name_length == length && strncmp(formal->name, name, length) == 0) {
            *parameter = index;
            *variadic = 0;
            return 1;
        }
    }
    if (macro->variadic && length == 11U && strncmp(name, "__VA_ARGS__", 11U) == 0) {
        *parameter = macro->parameter_count;
        *variadic = 1;
        return 1;
    }
    return 0;
}

int f2c_preprocessor_replacement_has_operator(const PreprocessorMacro *macro) {
    size_t index = 0U;
    while (index < macro->value_length) {
        if (macro->value[index] == '\'' || macro->value[index] == '"') {
            index = quoted_end(macro->value, macro->value_length, index, NULL);
            continue;
        }
        if (macro->value[index] == '#')
            return 1;
        ++index;
    }
    return 0;
}

static void trim_buffer_space(Buffer *buffer) {
    while (buffer->length != 0U && isspace((unsigned char)buffer->data[buffer->length - 1U]) != 0)
        --buffer->length;
    if (buffer->data != NULL)
        buffer->data[buffer->length] = '\0';
}

static void append_replacement_piece(Buffer *output, const char *text, size_t length,
                                     int *paste_pending) {
    size_t begin = 0U;
    if (*paste_pending) {
        while (begin < length && isspace((unsigned char)text[begin]) != 0)
            ++begin;
    }
    f2c_buffer_append_n(output, text + begin, length - begin);
    if (begin != length)
        *paste_pending = 0;
}

static void append_variadic_arguments(Buffer *output, const MacroInvocation *invocation,
                                      size_t first, int *paste_pending) {
    size_t argument;
    for (argument = first; argument < invocation->argument_count; ++argument) {
        if (argument != first)
            append_replacement_piece(output, ", ", 2U, paste_pending);
        append_replacement_piece(output, invocation->arguments[argument].text,
                                 invocation->arguments[argument].length, paste_pending);
    }
}

static void append_stringized_argument(Buffer *output, const MacroInvocation *invocation,
                                       size_t parameter, int variadic, int *paste_pending) {
    Buffer raw = {0};
    size_t index = 0U;
    int pending_space = 0;
    raw.limit = output->limit;
    if (variadic)
        append_variadic_arguments(&raw, invocation, parameter, &pending_space);
    else
        f2c_buffer_append_n(&raw, invocation->arguments[parameter].text,
                            invocation->arguments[parameter].length);
    append_replacement_piece(output, "\"", 1U, paste_pending);
    while (index < raw.length) {
        const char value = raw.data[index++];
        if (isspace((unsigned char)value) != 0) {
            pending_space = 1;
            continue;
        }
        if (pending_space && output->length != 0U && output->data[output->length - 1U] != '"')
            f2c_buffer_append_n(output, " ", 1U);
        pending_space = 0;
        if (value == '\\' || value == '"')
            f2c_buffer_append_n(output, "\\", 1U);
        f2c_buffer_append_n(output, &value, 1U);
    }
    f2c_buffer_append_n(output, "\"", 1U);
    free(raw.data);
}

static int build_operator_replacement(Preprocessor *preprocessor, const MacroInvocation *invocation,
                                      F2cSourcePosition diagnostic_origin, Buffer *output) {
    const PreprocessorMacro *macro = invocation->macro;
    size_t index = 0U;
    int paste_pending = 0;
    int saw_operand = 0;
    output->limit = preprocessor->context->limits.max_preprocessed_bytes;
    while (index < macro->value_length) {
        size_t end = index + 1U;
        if (macro->value[index] == '\'' || macro->value[index] == '"') {
            end = quoted_end(macro->value, macro->value_length, index, NULL);
            append_replacement_piece(output, macro->value + index, end - index, &paste_pending);
            saw_operand = 1;
            index = end;
            continue;
        }
        if (macro->value[index] == '#') {
            size_t parameter;
            int variadic;
            if (index + 1U < macro->value_length && macro->value[index + 1U] == '#') {
                trim_buffer_space(output);
                if (!saw_operand || paste_pending) {
                    diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_SYNTAX, diagnostic_origin,
                                       "invalid leading token-paste operator in", macro->name,
                                       macro->name_length);
                    return 0;
                }
                paste_pending = 1;
                index += 2U;
                while (index < macro->value_length &&
                       isspace((unsigned char)macro->value[index]) != 0)
                    ++index;
                if (index == macro->value_length) {
                    diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_SYNTAX, diagnostic_origin,
                                       "invalid trailing token-paste operator in", macro->name,
                                       macro->name_length);
                    return 0;
                }
                continue;
            }
            while (end < macro->value_length && isspace((unsigned char)macro->value[end]) != 0)
                ++end;
            index = end;
            if (index >= macro->value_length || !identifier_start(macro->value[index])) {
                diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_SYNTAX, diagnostic_origin,
                                   "stringizing operator does not name a parameter in", macro->name,
                                   macro->name_length);
                return 0;
            }
            end = index + 1U;
            while (end < macro->value_length && identifier_continue(macro->value[end]))
                ++end;
            if (!macro_parameter(macro, macro->value + index, end - index, &parameter, &variadic)) {
                diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_SYNTAX, diagnostic_origin,
                                   "stringizing operator does not name a parameter in", macro->name,
                                   macro->name_length);
                return 0;
            }
            append_stringized_argument(output, invocation, parameter, variadic, &paste_pending);
            saw_operand = 1;
            index = end;
            continue;
        }
        if (identifier_start(macro->value[index])) {
            size_t parameter;
            int variadic;
            while (end < macro->value_length && identifier_continue(macro->value[end]))
                ++end;
            if (macro_parameter(macro, macro->value + index, end - index, &parameter, &variadic)) {
                if (variadic) {
                    append_variadic_arguments(output, invocation, parameter, &paste_pending);
                    paste_pending = 0;
                } else {
                    append_replacement_piece(output, invocation->arguments[parameter].text,
                                             invocation->arguments[parameter].length,
                                             &paste_pending);
                    if (invocation->arguments[parameter].length == 0U)
                        paste_pending = 0;
                }
            } else {
                append_replacement_piece(output, macro->value + index, end - index, &paste_pending);
            }
            saw_operand = 1;
            index = end;
            continue;
        }
        append_replacement_piece(output, macro->value + index, 1U, &paste_pending);
        if (isspace((unsigned char)macro->value[index]) == 0)
            saw_operand = 1;
        ++index;
    }
    if (paste_pending) {
        diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_SYNTAX, diagnostic_origin,
                           "invalid trailing token-paste operator in", macro->name,
                           macro->name_length);
        return 0;
    }
    if (output->failed || output->limit_exceeded) {
        diagnose_expansion(
            preprocessor,
            output->limit_exceeded ? F2C_DIAGNOSTIC_RESOURCE_LIMIT : F2C_DIAGNOSTIC_OUT_OF_MEMORY,
            diagnostic_origin,
            output->limit_exceeded ? "function-like macro replacement is too large for"
                                   : "out of memory while replacing",
            macro->name, macro->name_length);
        return 0;
    }
    return 1;
}

int f2c_preprocessor_build_operator_replacement(Preprocessor *preprocessor,
                                                const PreprocessorMacro *macro,
                                                const PreprocessorMacroArgument *arguments,
                                                size_t argument_count,
                                                F2cSourcePosition diagnostic_origin,
                                                Buffer *output) {
    const MacroInvocation invocation = {macro, arguments, argument_count, NULL};
    return build_operator_replacement(preprocessor, &invocation, diagnostic_origin, output);
}

static int append_text(Preprocessor *preprocessor, Buffer *output, F2cSourceMap *source_map,
                       const char *text, size_t length, F2cSourcePosition text_origin,
                       const ExpansionOrigin *expansion) {
    F2cSourcePosition primary = expansion != NULL ? expansion->position : text_origin;
    const size_t primary_width = expansion != NULL ? expansion->width : length;
    const unsigned char primary_step = (unsigned char)(expansion != NULL ? 0U : 1U);
    return f2c_preprocessor_append(preprocessor, output, source_map, text, length, primary,
                                   primary_width, primary_step, text_origin, length, 1U,
                                   expansion != NULL);
}

static int expand_text(Preprocessor *preprocessor, const char *text, size_t length,
                       F2cSourcePosition text_origin, const ExpansionOrigin *expansion,
                       const ExpansionFrame *parent, size_t depth,
                       const MacroInvocation *invocation, Buffer *output,
                       F2cSourceMap *source_map) {
    size_t index = 0U;
    while (index < length) {
        size_t end = index + 1U;
        F2cSourcePosition unit_origin = text_origin;
        unit_origin.column += index;
        if (text[index] == '!') {
            return append_text(preprocessor, output, source_map, text + index, length - index,
                               unit_origin, expansion);
        }
        if (text[index] == '\'' || text[index] == '"') {
            end = quoted_end(text, length, index, NULL);
        } else if (isdigit((unsigned char)text[index]) != 0) {
            const size_t hollerith = hollerith_end(text, length, index, NULL);
            if (hollerith != index)
                end = hollerith;
        } else if (identifier_start(text[index])) {
            size_t macro_index;
            while (end < length && identifier_continue(text[end]))
                ++end;
            if (invocation != NULL) {
                size_t parameter;
                int variadic;
                const int substituted = macro_parameter(invocation->macro, text + index,
                                                        end - index, &parameter, &variadic);
                if (substituted) {
                    if (!variadic) {
                        const MacroArgument *argument = &invocation->arguments[parameter];
                        if (!expand_text(preprocessor, argument->text, argument->length,
                                         argument->origin, expansion, invocation->argument_parent,
                                         depth, NULL, output, source_map))
                            return 0;
                    } else {
                        for (parameter = invocation->macro->parameter_count;
                             parameter < invocation->argument_count; ++parameter) {
                            const MacroArgument *argument = &invocation->arguments[parameter];
                            if (parameter != invocation->macro->parameter_count &&
                                !append_text(preprocessor, output, source_map, ", ", 2U,
                                             unit_origin, expansion))
                                return 0;
                            if (!expand_text(preprocessor, argument->text, argument->length,
                                             argument->origin, expansion,
                                             invocation->argument_parent, depth, NULL, output,
                                             source_map))
                                return 0;
                        }
                    }
                    index = end;
                    continue;
                }
            }
            macro_index = f2c_preprocessor_find_macro(preprocessor, text + index, end - index);
            if (macro_index != SIZE_MAX) {
                const PreprocessorMacro *macro = &preprocessor->macros[macro_index];
                ExpansionOrigin nested_origin;
                ExpansionFrame frame;
                MacroArgument *arguments = NULL;
                size_t argument_count = 0U;
                size_t after = end;
                size_t opening = end;
                MacroInvocation nested_invocation;
                Buffer operated_replacement = {0};
                const char *replacement_text = macro->value;
                size_t replacement_length = macro->value_length;
                if (macro->function_like) {
                    while (opening < length && isspace((unsigned char)text[opening]) != 0)
                        ++opening;
                    if (opening >= length || text[opening] != '(')
                        goto append_original;
                }
                if (expansion_contains(parent, text + index, end - index)) {
                    diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_SYNTAX,
                                       expansion != NULL ? expansion->position : unit_origin,
                                       "cyclic source macro expansion for", text + index,
                                       end - index);
                    return 0;
                }
                if (depth >= preprocessor->context->limits.max_macro_expansion_depth) {
                    diagnose_expansion(preprocessor, F2C_DIAGNOSTIC_RESOURCE_LIMIT,
                                       expansion != NULL ? expansion->position : unit_origin,
                                       "source macro expansion depth limit exceeded for",
                                       text + index, end - index);
                    return 0;
                }
                nested_origin =
                    expansion != NULL ? *expansion : (ExpansionOrigin){unit_origin, end - index};
                if (macro->function_like &&
                    !parse_invocation_arguments(
                        preprocessor, macro, text, length, opening, text_origin,
                        expansion != NULL ? expansion->position : unit_origin, &arguments,
                        &argument_count, &after))
                    return 0;
                frame.name = macro->name;
                frame.length = macro->name_length;
                frame.parent = parent;
                nested_invocation.macro = macro;
                nested_invocation.arguments = arguments;
                nested_invocation.argument_count = argument_count;
                nested_invocation.argument_parent = parent;
                if (macro->function_like || f2c_preprocessor_replacement_has_operator(macro)) {
                    if (!build_operator_replacement(preprocessor, &nested_invocation,
                                                    expansion != NULL ? expansion->position
                                                                      : unit_origin,
                                                    &operated_replacement)) {
                        free(arguments);
                        free(operated_replacement.data);
                        return 0;
                    }
                    replacement_text =
                        operated_replacement.data != NULL ? operated_replacement.data : "";
                    replacement_length = operated_replacement.length;
                }
                if (!expand_text(preprocessor, replacement_text, replacement_length,
                                 (F2cSourcePosition){macro->definition_source_name,
                                                     macro->definition_line,
                                                     macro->definition_column},
                                 &nested_origin, &frame, depth + 1U, NULL, output, source_map)) {
                    free(arguments);
                    free(operated_replacement.data);
                    return 0;
                }
                free(arguments);
                free(operated_replacement.data);
                index = macro->function_like ? after : end;
                continue;
            }
        }
    append_original:
        if (!append_text(preprocessor, output, source_map, text + index, end - index, unit_origin,
                         expansion))
            return 0;
        index = end;
    }
    return 1;
}

int f2c_preprocessor_expand_source_line(Preprocessor *preprocessor, const char *text, size_t length,
                                        size_t line, F2cSourceForm form, Buffer *output,
                                        F2cSourceMap *source_map) {
    const char *source_name = preprocessor->current_source_name;
    F2cSourcePosition origin;
    size_t begin = 0U;
    if (source_name == NULL) {
        F2cSourceSpan span = {0};
        span.begin = (F2cSourcePosition){preprocessor->current_source_name, line, 1U};
        span.end = span.begin;
        ++span.end.column;
        f2c_diagnostic_span_code(preprocessor->context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &span, 1,
                                 "out of memory while recording source expansion locations");
        return 0;
    }
    origin = (F2cSourcePosition){source_name, line, 1U};
    if (form == F2C_SOURCE_FIXED && length != 0U &&
        (text[0] == 'c' || text[0] == 'C' || text[0] == '*' || text[0] == '!'))
        return append_text(preprocessor, output, source_map, text, length, origin, NULL);
    if (form == F2C_SOURCE_FIXED && (preprocessor->continued_character_quote != '\0' ||
                                     preprocessor->continued_hollerith_characters != 0U)) {
        if (!fixed_continuation_line(text, length)) {
            preprocessor->continued_character_quote = '\0';
            preprocessor->continued_hollerith_characters = 0U;
        } else {
            const size_t statement_begin = length < 6U ? length : 6U;
            if (!append_text(preprocessor, output, source_map, text, statement_begin, origin, NULL))
                return 0;
            begin = statement_begin;
            if (preprocessor->continued_hollerith_characters != 0U) {
                const size_t available = length - begin;
                const size_t protected_length =
                    preprocessor->continued_hollerith_characters < available
                        ? preprocessor->continued_hollerith_characters
                        : available;
                F2cSourcePosition payload_origin = origin;
                payload_origin.column += begin;
                if (!append_text(preprocessor, output, source_map, text + begin, protected_length,
                                 payload_origin, NULL))
                    return 0;
                begin += protected_length;
                preprocessor->continued_hollerith_characters -= protected_length;
                if (preprocessor->continued_hollerith_characters != 0U)
                    return 1;
            } else {
                int closed;
                F2cSourcePosition payload_origin = origin;
                const size_t protected_end =
                    begin + continued_literal_end(text + begin, length - begin,
                                                  preprocessor->continued_character_quote, 0,
                                                  &closed);
                payload_origin.column += begin;
                if (!append_text(preprocessor, output, source_map, text + begin,
                                 protected_end - begin, payload_origin, NULL))
                    return 0;
                begin = protected_end;
                if (!closed)
                    return 1;
                preprocessor->continued_character_quote = '\0';
            }
        }
    }
    if (form == F2C_SOURCE_FREE && preprocessor->continued_character_quote != '\0') {
        int closed;
        begin = continued_literal_end(text, length, preprocessor->continued_character_quote, 1,
                                      &closed);
        if (!append_text(preprocessor, output, source_map, text, begin, origin, NULL))
            return 0;
        if (!closed) {
            preprocessor->continued_character_quote = line_has_trailing_ampersand(text, length)
                                                          ? preprocessor->continued_character_quote
                                                          : '\0';
            return 1;
        }
        preprocessor->continued_character_quote = '\0';
    }
    if (begin < length) {
        F2cSourcePosition remainder_origin = origin;
        remainder_origin.column += begin;
        if (!expand_text(preprocessor, text + begin, length - begin, remainder_origin, NULL, NULL,
                         0U, NULL, output, source_map))
            return 0;
    }
    if (form == F2C_SOURCE_FREE)
        preprocessor->continued_character_quote = continued_quote_in_line(text, length, begin);
    else if (form == F2C_SOURCE_FIXED) {
        const size_t statement_begin = begin > 6U ? begin : (length < 6U ? length : 6U);
        find_fixed_continuation_state(text, length, statement_begin,
                                      &preprocessor->continued_character_quote,
                                      &preprocessor->continued_hollerith_characters);
    }
    return 1;
}

int f2c_preprocessor_expand_directive_operand(Preprocessor *preprocessor, const char *text,
                                              size_t length, size_t line, size_t column,
                                              Buffer *output) {
    F2cSourceMap source_map = {0};
    F2cSourcePosition origin = {preprocessor->current_source_name, line, column};
    const int result =
        expand_text(preprocessor, text, length, origin, NULL, NULL, 0U, NULL, output, &source_map);
    f2c_source_map_discard(&source_map);
    return result;
}
