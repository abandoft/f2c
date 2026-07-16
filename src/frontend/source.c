#include "internal/f2c.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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

static void normalize_fixed_numeric_blanks(char *line) {
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
            if (exponent != cursor + 1)
                memmove(cursor + 1, exponent, strlen(exponent) + 1U);
        }
    }
}

static int push_logical_statements(Context *context, char *line, size_t number) {
    char *cursor = line;
    char *start = line;
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
            char *clean;
            part = f2c_strdup_n(start, (size_t)(cursor - start));
            if (part == NULL) {
                free(line);
                return 0;
            }
            clean = f2c_trim(part);
            if (*clean != '\0') {
                if (clean != part)
                    memmove(part, clean, strlen(clean) + 1U);
                if (!f2c_lines_push(&context->lines, part, number, context->options)) {
                    free(line);
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
    free(line);
    return 1;
}

static int append_logical_line(Context *context, Buffer *logical, size_t number) {
    char *copy;
    char *clean;
    if (logical->length == 0U) {
        return 1;
    }
    copy = f2c_buffer_take(logical);
    if (copy == NULL) {
        return 0;
    }
    clean = f2c_trim(copy);
    if (*clean == '\0') {
        free(copy);
        return 1;
    }
    f2c_lowercase_code(clean);
    if (clean != copy) {
        memmove(copy, clean, strlen(clean) + 1U);
    }
    return push_logical_statements(context, copy, number);
}

int f2c_normalize_source(Context *context, const char *source, size_t length, F2cSourceForm form) {
    size_t offset = 0U;
    size_t line_number = 0U;
    size_t logical_number = 1U;
    Buffer logical = {0};
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
            char fixed_label[6] = {0};
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
                const size_t label_length = raw_length < 5U ? raw_length : 5U;
                char *clean_label;
                memcpy(fixed_label, raw, label_length);
                clean_label = f2c_trim(fixed_label);
                if (clean_label != fixed_label) {
                    memmove(fixed_label, clean_label, strlen(clean_label) + 1U);
                }
            }
            body = raw_length > 6U ? raw + 6 : raw + raw_length;
            /* Reference LAPACK uses the common extended fixed-form width and
             * contains meaningful punctuation after column 72. */
            if (strlen(body) > 126U) {
                body[126] = '\0';
            }
            strip_comment(body);
            body = f2c_trim(body);
            normalize_fixed_numeric_blanks(body);
            if (!next_continued && logical.length != 0U &&
                !append_logical_line(context, &logical, logical_number)) {
                free(raw);
                return 0;
            }
            if (!next_continued) {
                logical_number = line_number;
            }
            if (*body != '\0') {
                if (logical.length != 0U) {
                    f2c_buffer_append(&logical, " ");
                }
                if (fixed_label[0] != '\0') {
                    f2c_buffer_append(&logical, fixed_label);
                    f2c_buffer_append(&logical, " ");
                }
                f2c_buffer_append(&logical, body);
            }
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
                if (logical.length != 0U) {
                    f2c_buffer_append(&logical, " ");
                }
                f2c_buffer_append(&logical, body);
            }
            continued = next_continued;
        }
        free(raw);
        if (logical.failed) {
            return 0;
        }
    }
    return append_logical_line(context, &logical, logical_number);
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
        free(context->lines.items[i].text);
        context->lines.items[i].text = f2c_buffer_take(&replacement);

        for (j = i + 1U; j < context->lines.count; ++j) {
            const char *target = context->lines.items[j].text;
            const size_t label_length = strlen(label);
            if (strncmp(target, label, label_length) == 0 &&
                isspace((unsigned char)target[label_length])) {
                Buffer terminal = {0};
                f2c_buffer_printf(&terminal, "end do %s", label);
                if (terminal.failed)
                    return 0;
                free(context->lines.items[j].text);
                context->lines.items[j].text = f2c_buffer_take(&terminal);
                break;
            }
        }
    }
    return 1;
}
