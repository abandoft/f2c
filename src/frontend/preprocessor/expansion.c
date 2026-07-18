#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <stdint.h>
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
                               size_t name_length) {
    F2cSourceSpan span = {0};
    span.begin = position;
    span.end = position;
    span.end.column += name_length != 0U ? name_length : 1U;
    f2c_diagnostic_span_code(
        preprocessor->context, code, &span, 1, "%s '%.*s'", message,
        (int)(name_length > (size_t)INT32_MAX ? (size_t)INT32_MAX : name_length), name);
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
                       const ExpansionFrame *parent, size_t depth, Buffer *output,
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
            macro_index = f2c_preprocessor_find_macro(preprocessor, text + index, end - index);
            if (macro_index != SIZE_MAX) {
                const PreprocessorMacro *macro = &preprocessor->macros[macro_index];
                ExpansionOrigin nested_origin;
                ExpansionFrame frame;
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
                frame.name = macro->name;
                frame.length = macro->name_length;
                frame.parent = parent;
                if (!expand_text(preprocessor, macro->value, macro->value_length,
                                 (F2cSourcePosition){macro->definition_source_name,
                                                     macro->definition_line,
                                                     macro->definition_column},
                                 &nested_origin, &frame, depth + 1U, output, source_map))
                    return 0;
                index = end;
                continue;
            }
        }
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
                         0U, output, source_map))
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
