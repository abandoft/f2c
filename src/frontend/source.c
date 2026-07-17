#include "internal/f2c.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct SourceBuffer {
    Buffer text;
    F2cSourceMapSegment *map;
    size_t map_count;
    size_t map_capacity;
} SourceBuffer;

static void source_buffer_discard(SourceBuffer *buffer) {
    free(buffer->text.data);
    free(buffer->map);
    memset(buffer, 0, sizeof(*buffer));
}

static int source_map_reserve(SourceBuffer *buffer, size_t additional) {
    F2cSourceMapSegment *replacement;
    size_t required;
    size_t capacity;
    if (additional > SIZE_MAX - buffer->map_count)
        return 0;
    required = buffer->map_count + additional;
    if (required <= buffer->map_capacity)
        return 1;
    capacity = buffer->map_capacity == 0U ? 8U : buffer->map_capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U)
            return 0;
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement = (F2cSourceMapSegment *)realloc(buffer->map,
                                                 capacity * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    buffer->map = replacement;
    buffer->map_capacity = capacity;
    return 1;
}

static int source_buffer_append(SourceBuffer *buffer, const char *text, size_t length,
                                size_t physical_line, size_t physical_column) {
    const size_t logical_begin = buffer->text.length;
    F2cSourceMapSegment *last;
    if (length == 0U)
        return 1;
    if (!source_map_reserve(buffer, 1U))
        return 0;
    f2c_buffer_append_n(&buffer->text, text, length);
    if (buffer->text.failed)
        return 0;
    last = buffer->map_count != 0U ? &buffer->map[buffer->map_count - 1U] : NULL;
    if (last != NULL && last->logical_begin + last->length == logical_begin &&
        last->physical_line == physical_line &&
        last->physical_column + last->length == physical_column) {
        last->length += length;
        return 1;
    }
    buffer->map[buffer->map_count++] =
        (F2cSourceMapSegment){logical_begin, length, physical_line, physical_column};
    return 1;
}

static int source_buffer_separator(SourceBuffer *buffer, size_t line, size_t column) {
    if (buffer->text.length == 0U)
        return 1;
    return source_buffer_append(buffer, " ", 1U, line, column > 1U ? column - 1U : column);
}

static F2cSourceMapSegment *source_map_slice(const SourceBuffer *buffer, size_t begin,
                                             size_t length, size_t *count) {
    F2cSourceMapSegment *result;
    size_t index;
    size_t used = 0U;
    const size_t end = begin + length;
    *count = 0U;
    if (buffer->map_count == 0U || length == 0U)
        return NULL;
    if (buffer->map_count > SIZE_MAX / sizeof(*result))
        return NULL;
    result = (F2cSourceMapSegment *)malloc(buffer->map_count * sizeof(*result));
    if (result == NULL)
        return NULL;
    for (index = 0U; index < buffer->map_count; ++index) {
        const F2cSourceMapSegment *segment = &buffer->map[index];
        const size_t segment_end = segment->logical_begin + segment->length;
        const size_t overlap_begin = segment->logical_begin > begin ? segment->logical_begin : begin;
        const size_t overlap_end = segment_end < end ? segment_end : end;
        if (overlap_begin >= overlap_end)
            continue;
        result[used].logical_begin = overlap_begin - begin;
        result[used].length = overlap_end - overlap_begin;
        result[used].physical_line = segment->physical_line;
        result[used].physical_column =
            segment->physical_column + overlap_begin - segment->logical_begin;
        ++used;
    }
    if (used == 0U) {
        free(result);
        return NULL;
    }
    *count = used;
    return result;
}

static void strip_comment(char *line) {
    int quote = 0;
    char *cursor;
    for (cursor = line; *cursor != '\0'; ++cursor) {
        if ((*cursor == '\'' || *cursor == '"') &&
            (quote == 0 || quote == (unsigned char)*cursor)) {
            if (quote != 0 && cursor[1] == *cursor) {
                ++cursor;
            } else {
                quote = quote == 0 ? (unsigned char)*cursor : 0;
            }
        } else if (*cursor == '!' && quote == 0) {
            *cursor = '\0';
            return;
        }
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
    char *cursor = logical->text.data;
    char *start = cursor;
    int quote = 0;
    for (;;) {
        if ((*cursor == '\'' || *cursor == '"') &&
            (quote == 0 || quote == (unsigned char)*cursor)) {
            if (quote != 0 && cursor[1] == *cursor)
                ++cursor;
            else
                quote = quote == 0 ? (unsigned char)*cursor : 0;
        }
        if ((*cursor == ';' && quote == 0) || *cursor == '\0') {
            char *part;
            F2cSourceMapSegment *map;
            size_t map_count;
            size_t begin = (size_t)(start - logical->text.data);
            size_t end = (size_t)(cursor - logical->text.data);
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
                if (logical->map_count != 0U && map == NULL) {
                    free(part);
                    source_buffer_discard(logical);
                    return 0;
                }
                if (map_count != 0U)
                    number = map[0].physical_line;
                if (!f2c_lines_push_mapped(context, part, number, map, map_count,
                                           context->options)) {
                    source_buffer_discard(logical);
                    return 0;
                }
            } else {
                free(part);
            }
            if (*cursor == '\0')
                break;
            start = cursor + 1;
        }
        ++cursor;
    }
    source_buffer_discard(logical);
    return 1;
}

static int append_logical_line(Context *context, SourceBuffer *logical, size_t number) {
    if (logical->text.length == 0U) {
        return 1;
    }
    f2c_lowercase_code(logical->text.data);
    return push_logical_statements(context, logical, number);
}

int f2c_normalize_source(Context *context, const char *source, size_t length, F2cSourceForm form) {
    size_t offset = 0U;
    size_t line_number = 0U;
    size_t logical_number = 1U;
    SourceBuffer logical = {0};
    int continued = 0;
    int pp_depth = 0;
    int pp_active = 1;
    int pp_parent[16] = {0};
    int pp_taken[16] = {0};
    while (offset < length) {
        size_t end = offset;
        char *raw;
        char *body;
        int next_continued = 0;
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

        {
            char *directive = f2c_trim(raw);
            if (*directive == '#') {
                ++directive;
                directive = f2c_trim(directive);
                /* Fortran preprocessing identifiers are case-sensitive in the
                 * preprocessor, but LAPACK conventionally spells feature
                 * macros in upper case.  Normalise a private copy before
                 * evaluating the small, deterministic feature subset that the
                 * translator supports. */
                f2c_lowercase_code(directive);
                if (f2c_starts_word(directive, "if") || f2c_starts_word(directive, "ifdef") ||
                    f2c_starts_word(directive, "ifndef")) {
                    const int condition = f2c_starts_word(directive, "ifndef") ||
                                          strstr(directive, "!defined") != NULL;
                    if (pp_depth < 16) {
                        pp_parent[pp_depth] = pp_active;
                        pp_taken[pp_depth] = condition;
                        pp_active = pp_active && condition;
                        ++pp_depth;
                    }
                } else if (f2c_starts_word(directive, "elif") && pp_depth > 0) {
                    const int index = pp_depth - 1;
                    const int condition = strstr(directive, "!defined") != NULL ||
                                          strstr(directive, "use_isnan") != NULL;
                    pp_active = pp_parent[index] && !pp_taken[index] && condition;
                    if (condition)
                        pp_taken[index] = 1;
                } else if (f2c_starts_word(directive, "else") && pp_depth > 0) {
                    const int index = pp_depth - 1;
                    pp_active = pp_parent[index] && !pp_taken[index];
                    pp_taken[index] = 1;
                } else if (f2c_starts_word(directive, "endif") && pp_depth > 0) {
                    --pp_depth;
                    pp_active = pp_parent[pp_depth];
                }
                free(raw);
                continue;
            }
            if (!pp_active) {
                free(raw);
                continue;
            }
        }

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
            body = f2c_trim(body);
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
            if (*body != '\0') {
                size_t character = 0U;
                if (!source_buffer_separator(&logical, line_number, columns[0])) {
                    free(columns);
                    free(raw);
                    source_buffer_discard(&logical);
                    return 0;
                }
                if (label_begin < label_end &&
                    (!source_buffer_append(&logical, raw + label_begin, label_end - label_begin,
                                           line_number, label_begin + 1U) ||
                     !source_buffer_separator(&logical, line_number, columns[0]))) {
                    free(columns);
                    free(raw);
                    source_buffer_discard(&logical);
                    return 0;
                }
                while (character < body_length) {
                    size_t run = character + 1U;
                    while (run < body_length && columns[run] == columns[run - 1U] + 1U)
                        ++run;
                    if (!source_buffer_append(&logical, body + character, run - character,
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
            if (body_length != 0U && body[body_length - 1U] == '&') {
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
                if (!source_buffer_separator(&logical, line_number, column) ||
                    !source_buffer_append(&logical, body, strlen(body), line_number, column)) {
                    free(raw);
                    source_buffer_discard(&logical);
                    return 0;
                }
            }
            continued = next_continued;
        }
        free(raw);
        if (logical.text.failed) {
            source_buffer_discard(&logical);
            return 0;
        }
    }
    return append_logical_line(context, &logical, logical_number);
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
    if (map != NULL)
        *map = (F2cSourceMapSegment){0U, length, origin.line, origin.column};
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
                if (!replace_rewritten_line(&context->lines.items[j],
                                            f2c_buffer_take(&terminal)))
                    return 0;
                break;
            }
        }
    }
    return 1;
}
