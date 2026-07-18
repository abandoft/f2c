#include "frontend/preprocessor.h"
#include "internal/f2c.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct SourceBuffer {
    Buffer text;
    F2cSourceMap map;
    const char *source_name;
} SourceBuffer;

static void source_buffer_discard(SourceBuffer *buffer) {
    free(buffer->text.data);
    f2c_source_map_discard(&buffer->map);
    memset(buffer, 0, sizeof(*buffer));
}

static int source_buffer_append_mapped(SourceBuffer *buffer, const char *text, size_t length,
                                       const F2cPreprocessedSource *source, size_t source_begin,
                                       size_t fallback_line, size_t fallback_column) {
    const size_t logical_begin = buffer->text.length;
    if (length == 0U)
        return 1;
    f2c_buffer_append_n(&buffer->text, text, length);
    if (buffer->text.failed)
        return 0;
    if (source != NULL && source->source_map.count != 0U &&
        f2c_source_map_append_slice(&buffer->map, logical_begin, source->source_map.items,
                                    source->source_map.count, source_begin, length))
        return 1;
    if (source != NULL && source->source_map.count != 0U)
        return 0;
    {
        const F2cSourcePosition origin = {buffer->source_name, fallback_line, fallback_column};
        return f2c_source_map_append(&buffer->map, logical_begin, length, origin, length, 1U,
                                     origin, length, 1U, 0);
    }
}

static int source_buffer_separator(SourceBuffer *buffer, const F2cPreprocessedSource *source,
                                   size_t source_offset, size_t line, size_t column) {
    if (buffer->text.length == 0U)
        return 1;
    return source_buffer_append_mapped(buffer, " ", 1U, source, source_offset, line,
                                       column > 1U ? column - 1U : column);
}

static F2cSourceMapSegment *source_map_slice(const SourceBuffer *buffer, size_t begin,
                                             size_t length, size_t *count) {
    return f2c_source_map_slice(buffer->map.items, buffer->map.count, begin, length, count);
}

static void lowercase_range(char *begin, char *end) {
    while (begin < end) {
        *begin = (char)tolower((unsigned char)*begin);
        ++begin;
    }
}

static void normalize_code_case(char *text) {
    F2cTokenStream stream;
    char *cursor = text;
    f2c_token_stream_init(&stream, text, 1U, 1U);
    for (;;) {
        const char *protected_begin = NULL;
        const char *token_end;
        f2c_token_stream_next(&stream);
        lowercase_range(cursor, (char *)stream.token.begin);
        if (stream.token.kind == F2C_TOKEN_END)
            return;
        token_end = stream.token.begin + stream.token.length;
        if (stream.token.kind == F2C_TOKEN_STRING)
            protected_begin = f2c_character_literal_quote(stream.token.begin);
        else if (stream.token.kind == F2C_TOKEN_HOLLERITH) {
            size_t payload_length;
            (void)f2c_hollerith_payload(stream.token.begin, &protected_begin, &payload_length);
        }
        if (protected_begin != NULL && protected_begin < token_end) {
            if (stream.token.kind == F2C_TOKEN_STRING)
                ++protected_begin;
            lowercase_range((char *)stream.token.begin, (char *)protected_begin);
        } else {
            lowercase_range((char *)stream.token.begin, (char *)token_end);
        }
        cursor = (char *)token_end;
    }
}

static void strip_comment(char *line) {
    F2cTokenStream stream;
    f2c_token_stream_init(&stream, line, 1U, 1U);
    for (;;) {
        f2c_token_stream_next(&stream);
        if (stream.token.kind != F2C_TOKEN_END)
            continue;
        if (*stream.token.begin == '!')
            *(char *)stream.token.begin = '\0';
        return;
    }
}

static int trailing_free_continuation(const char *body, size_t length,
                                      int *character_continuation) {
    const char *marker;
    F2cTokenStream stream;
    if (character_continuation != NULL)
        *character_continuation = 0;
    if (length == 0U || body[length - 1U] != '&')
        return 0;
    marker = body + length - 1U;
    f2c_token_stream_init(&stream, body, 1U, 1U);
    for (;;) {
        const char *end;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            return 1;
        end = stream.token.begin + stream.token.length;
        if (marker < stream.token.begin || marker >= end)
            continue;
        if (stream.token.kind == F2C_TOKEN_STRING || stream.token.kind == F2C_TOKEN_HOLLERITH)
            return 0;
        if (stream.token.kind == F2C_TOKEN_INVALID &&
            f2c_character_literal_quote(stream.token.begin) != NULL &&
            character_continuation != NULL)
            *character_continuation = 1;
        return 1;
    }
}

static void normalize_fixed_numeric_blanks(char *line, size_t *columns) {
    char *cursor;
    int quote = 0;
    for (cursor = line; *cursor != '\0'; ++cursor) {
        if ((*cursor == '\'' || *cursor == '"') &&
            (quote == 0 || quote == (unsigned char)*cursor)) {
            if (quote != 0 && cursor[1] == *cursor)
                ++cursor;
            else
                quote = quote == 0 ? (unsigned char)*cursor : 0;
        } else if (quote == 0 && *cursor == '.' && cursor > line &&
                   isdigit((unsigned char)cursor[-1])) {
            char *exponent = cursor + 1;
            char *digits;
            while (isspace((unsigned char)*exponent))
                ++exponent;
            if (*exponent != 'e' && *exponent != 'E' && *exponent != 'd' && *exponent != 'D')
                continue;
            digits = exponent + 1;
            while (isspace((unsigned char)*digits))
                ++digits;
            if (*digits == '+' || *digits == '-') {
                ++digits;
                while (isspace((unsigned char)*digits))
                    ++digits;
            }
            if (!isdigit((unsigned char)*digits))
                continue;
            if (exponent != cursor + 1) {
                const size_t destination = (size_t)(cursor + 1 - line);
                const size_t source = (size_t)(exponent - line);
                const size_t moved = strlen(exponent);
                memmove(cursor + 1, exponent, strlen(exponent) + 1U);
                memmove(columns + destination, columns + source, moved * sizeof(*columns));
            }
        }
    }
}

static int push_logical_statements(Context *context, SourceBuffer *logical, size_t number) {
    F2cTokenStream stream;
    size_t start = 0U;
    f2c_token_stream_init(&stream, logical->text.data, number, 1U);
    for (;;) {
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_SEMICOLON || stream.token.kind == F2C_TOKEN_END) {
            char *part;
            F2cSourceMapSegment *map;
            size_t map_count;
            size_t begin = start;
            size_t end = (size_t)(stream.token.begin - logical->text.data);
            while (begin < end && isspace((unsigned char)logical->text.data[begin]))
                ++begin;
            while (end > begin && isspace((unsigned char)logical->text.data[end - 1U]))
                --end;
            part = f2c_strdup_n(logical->text.data + begin, end - begin);
            if (part == NULL) {
                source_buffer_discard(logical);
                return 0;
            }
            if (begin != end) {
                map = source_map_slice(logical, begin, end - begin, &map_count);
                if (logical->map.count != 0U && map == NULL) {
                    free(part);
                    source_buffer_discard(logical);
                    return 0;
                }
                if (map_count != 0U)
                    number = map[0].expansion.line;
                if (!f2c_lines_push_mapped(context, part, number, map, map_count,
                                           context->options)) {
                    source_buffer_discard(logical);
                    return 0;
                }
            } else {
                free(part);
            }
            if (stream.token.kind == F2C_TOKEN_END)
                break;
            start = end + stream.token.length;
        }
    }
    source_buffer_discard(logical);
    return 1;
}

static int append_logical_line(Context *context, SourceBuffer *logical, size_t number) {
    if (logical->text.length == 0U) {
        return 1;
    }
    normalize_code_case(logical->text.data);
    return push_logical_statements(context, logical, number);
}

static int normalize_preprocessed_source(Context *context, const F2cPreprocessedSource *input,
                                         F2cSourceForm form) {
    const char *source = input->text;
    const size_t length = input->length;
    size_t offset = 0U;
    size_t line_number = 0U;
    size_t logical_number = 1U;
    SourceBuffer logical = {0};
    int continued = 0;
    int continued_character = 0;
    logical.source_name = f2c_context_source_name(
        context, context->options != NULL ? context->options->source_name : NULL);
    if (logical.source_name == NULL)
        return 0;
    while (offset < length) {
        const size_t line_begin = offset;
        size_t end = offset;
        char *raw;
        char *body;
        int next_continued = 0;
        int next_character_continuation = 0;
        while (end < length && source[end] != '\n' && source[end] != '\r') {
            ++end;
        }
        ++line_number;
        raw = f2c_strdup_n(source + offset, end - offset);
        if (raw == NULL) {
            return 0;
        }
        if (end < length && source[end] == '\r') {
            ++end;
            if (end < length && source[end] == '\n')
                ++end;
        } else if (end < length && source[end] == '\n') {
            ++end;
        }
        offset = end;

        if (form == F2C_SOURCE_FIXED) {
            const size_t raw_length = strlen(raw);
            size_t label_begin = 0U;
            size_t label_end = raw_length < 5U ? raw_length : 5U;
            size_t *columns = NULL;
            size_t body_length;
            if (raw_length > 72U &&
                (raw[0] == 'c' || raw[0] == 'C' || raw[0] == '*' || raw[0] == '!')) {
                char *embedded_program = strstr(raw + 72, "PROGRAM");
                if (embedded_program != NULL) {
                    const size_t program_length = strlen(embedded_program);
                    memset(raw, ' ', 6U);
                    memmove(raw + 6, embedded_program, program_length + 1U);
                }
            }
            if (raw_length != 0U &&
                (raw[0] == 'c' || raw[0] == 'C' || raw[0] == '*' || raw[0] == '!')) {
                free(raw);
                continue;
            }
            next_continued = raw_length > 5U && raw[5] != ' ' && raw[5] != '0';
            if (!next_continued && raw_length != 0U) {
                while (label_begin < label_end && isspace((unsigned char)raw[label_begin]))
                    ++label_begin;
                while (label_end > label_begin && isspace((unsigned char)raw[label_end - 1U]))
                    --label_end;
            }
            body = raw_length > 6U ? raw + 6 : raw + raw_length;
            /* Reference LAPACK uses the common extended fixed-form width and
             * contains meaningful punctuation after column 72. */
            if (strlen(body) > 126U) {
                body[126] = '\0';
            }
            strip_comment(body);
            body_length = strlen(body);
            if (body_length != 0U) {
                size_t column;
                columns = (size_t *)malloc(body_length * sizeof(*columns));
                if (columns == NULL) {
                    free(raw);
                    source_buffer_discard(&logical);
                    return 0;
                }
                for (column = 0U; column < body_length; ++column)
                    columns[column] = (size_t)(body - raw) + column + 1U;
                normalize_fixed_numeric_blanks(body, columns);
                body_length = strlen(body);
            }
            if (!next_continued && logical.text.length != 0U &&
                !append_logical_line(context, &logical, logical_number)) {
                free(columns);
                free(raw);
                return 0;
            }
            if (!next_continued) {
                logical_number = line_number;
            }
            if (body_length != 0U && columns != NULL) {
                size_t character = 0U;
                if (!next_continued &&
                    !source_buffer_separator(&logical, input, line_begin + columns[0] - 1U,
                                             line_number, columns[0])) {
                    free(columns);
                    free(raw);
                    source_buffer_discard(&logical);
                    return 0;
                }
                if (!next_continued && label_begin < label_end &&
                    (!source_buffer_append_mapped(
                         &logical, raw + label_begin, label_end - label_begin, input,
                         line_begin + label_begin, line_number, label_begin + 1U) ||
                     !source_buffer_separator(&logical, input, line_begin + columns[0] - 1U,
                                              line_number, columns[0]))) {
                    free(columns);
                    free(raw);
                    source_buffer_discard(&logical);
                    return 0;
                }
                while (character < body_length) {
                    size_t run = character + 1U;
                    while (run < body_length && columns[run] == columns[run - 1U] + 1U)
                        ++run;
                    if (!source_buffer_append_mapped(&logical, body + character, run - character,
                                                     input, line_begin + columns[character] - 1U,
                                                     line_number, columns[character])) {
                        free(columns);
                        free(raw);
                        source_buffer_discard(&logical);
                        return 0;
                    }
                    character = run;
                }
            }
            free(columns);
        } else {
            size_t body_length;
            strip_comment(raw);
            body = f2c_trim(raw);
            if (continued && *body == '&') {
                body = f2c_trim(body + 1);
            }
            body_length = strlen(body);
            if (trailing_free_continuation(body, body_length, &next_character_continuation)) {
                body[--body_length] = '\0';
                body = f2c_trim(body);
                next_continued = 1;
            }
            if (!continued) {
                if (!append_logical_line(context, &logical, logical_number)) {
                    free(raw);
                    return 0;
                }
                logical_number = line_number;
            }
            if (*body != '\0') {
                const size_t column = (size_t)(body - raw) + 1U;
                if ((!continued_character &&
                     !source_buffer_separator(&logical, input, line_begin + column - 1U,
                                              line_number, column)) ||
                    !source_buffer_append_mapped(&logical, body, strlen(body), input,
                                                 line_begin + column - 1U, line_number, column)) {
                    free(raw);
                    source_buffer_discard(&logical);
                    return 0;
                }
            }
            continued = next_continued;
            continued_character = next_character_continuation;
        }
        free(raw);
        if (logical.text.failed) {
            source_buffer_discard(&logical);
            return 0;
        }
    }
    return append_logical_line(context, &logical, logical_number);
}

int f2c_normalize_source(Context *context, const char *source, size_t length, F2cSourceForm form) {
    F2cPreprocessedSource preprocessed;
    int result;
    if (!f2c_preprocess_source(context, source, length, form, &preprocessed))
        return 0;
    result = normalize_preprocessed_source(context, &preprocessed, form);
    f2c_preprocessed_source_discard(&preprocessed);
    return result;
}

static int replace_rewritten_line(Line *line, char *text) {
    F2cSourceMapSegment *map;
    const F2cSourcePosition origin = f2c_line_source_position(line, 0U);
    const size_t length = text != NULL ? strlen(text) : 0U;
    map = length != 0U ? (F2cSourceMapSegment *)malloc(sizeof(*map)) : NULL;
    if (length != 0U && map == NULL) {
        free(text);
        return 0;
    }
    if (map != NULL) {
        const F2cSourcePosition spelling = origin;
        *map = (F2cSourceMapSegment){0U, length, origin, spelling, length, length, 1U, 1U, 0U};
    }
    free(line->text);
    free(line->source_map);
    line->text = text;
    line->source_map = map;
    line->source_map_count = map != NULL ? 1U : 0U;
    line->number = origin.line;
    return 1;
}

/* Convert the legacy "DO 10 I=... / 10 CONTINUE" spelling to the block form
 * used by the emitter, while retaining the terminal label for GOTO targets. */
int f2c_rewrite_labeled_do(Context *context) {
    size_t i;
    for (i = 0U; i < context->lines.count; ++i) {
        char *line = context->lines.items[i].text;
        char *cursor;
        char *label_begin;
        char *label_end;
        char label[32];
        size_t j;
        Buffer replacement = {0};
        if (!f2c_starts_word(line, "do") || f2c_starts_word(line, "do while"))
            continue;
        cursor = line + 2;
        while (isspace((unsigned char)*cursor))
            ++cursor;
        label_begin = cursor;
        while (isdigit((unsigned char)*cursor))
            ++cursor;
        label_end = cursor;
        if (label_end == label_begin || (!isspace((unsigned char)*cursor) && *cursor != ','))
            continue;
        if ((size_t)(label_end - label_begin) >= sizeof(label))
            continue;
        memcpy(label, label_begin, (size_t)(label_end - label_begin));
        label[label_end - label_begin] = '\0';
        while (isspace((unsigned char)*cursor))
            ++cursor;
        if (*cursor == ',') {
            ++cursor;
            while (isspace((unsigned char)*cursor))
                ++cursor;
        }
        if (strchr(cursor, '=') == NULL)
            continue;

        f2c_buffer_printf(&replacement, "do %s", cursor);
        if (replacement.failed)
            return 0;
        if (!replace_rewritten_line(&context->lines.items[i], f2c_buffer_take(&replacement)))
            return 0;

        for (j = i + 1U; j < context->lines.count; ++j) {
            const char *target = context->lines.items[j].text;
            const size_t label_length = strlen(label);
            if (strncmp(target, label, label_length) == 0 &&
                isspace((unsigned char)target[label_length])) {
                Buffer terminal = {0};
                f2c_buffer_printf(&terminal, "end do %s", label);
                if (terminal.failed)
                    return 0;
                if (!replace_rewritten_line(&context->lines.items[j], f2c_buffer_take(&terminal)))
                    return 0;
                break;
            }
        }
    }
    return 1;
}
