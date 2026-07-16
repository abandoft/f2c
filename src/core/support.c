#include "internal/f2c.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void buffer_reserve(Buffer *buffer, size_t extra) {
    size_t needed;
    size_t capacity;
    char *replacement;
    if (buffer->failed || extra > SIZE_MAX - buffer->length - 1U) {
        buffer->failed = 1;
        return;
    }
    needed = buffer->length + extra + 1U;
    if (needed <= buffer->capacity) {
        return;
    }
    capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    replacement = (char *)realloc(buffer->data, capacity);
    if (replacement == NULL) {
        buffer->failed = 1;
        return;
    }
    buffer->data = replacement;
    buffer->capacity = capacity;
}

void f2c_buffer_append_n(Buffer *buffer, const char *text, size_t length) {
    buffer_reserve(buffer, length);
    if (buffer->failed) {
        return;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
}

void f2c_buffer_append(Buffer *buffer, const char *text) {
    f2c_buffer_append_n(buffer, text, strlen(text));
}

void f2c_buffer_printf(Buffer *buffer, const char *format, ...) {
    va_list args;
    va_list copy;
    int count;
    va_start(args, format);
    va_copy(copy, args);
    count = vsnprintf(NULL, 0U, format, copy);
    va_end(copy);
    if (count >= 0) {
        buffer_reserve(buffer, (size_t)count);
        if (!buffer->failed) {
            (void)vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length,
                            format, args);
            buffer->length += (size_t)count;
        }
    } else {
        buffer->failed = 1;
    }
    va_end(args);
}

char *f2c_buffer_take(Buffer *buffer) {
    char *result;
    if (buffer->failed) {
        free(buffer->data);
        memset(buffer, 0, sizeof(*buffer));
        return NULL;
    }
    if (buffer->data == NULL) {
        result = (char *)malloc(1U);
        if (result != NULL) {
            result[0] = '\0';
        }
        return result;
    }
    result = buffer->data;
    memset(buffer, 0, sizeof(*buffer));
    return result;
}

char *f2c_strdup_n(const char *text, size_t length) {
    char *result = (char *)malloc(length + 1U);
    if (result != NULL) {
        memcpy(result, text, length);
        result[length] = '\0';
    }
    return result;
}

char *f2c_strdup(const char *text) { return f2c_strdup_n(text, strlen(text)); }

char *f2c_trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

void f2c_lowercase_code(char *text) {
    int quote = 0;
    for (; *text != '\0'; ++text) {
        if ((*text == '\'' || *text == '"') && (quote == 0 || quote == (unsigned char)*text)) {
            quote = quote == 0 ? (unsigned char)*text : 0;
        } else if (quote == 0) {
            *text = (char)tolower((unsigned char)*text);
        }
    }
}

int f2c_starts_word(const char *text, const char *word) {
    F2cLexer source;
    F2cLexer expected;
    f2c_lexer_init(&source, text, 1U, 1U);
    f2c_lexer_init(&expected, word, 1U, 1U);
    for (;;) {
        size_t index;
        f2c_lexer_next(&expected);
        if (expected.token.kind == F2C_TOKEN_END)
            return 1;
        f2c_lexer_next(&source);
        if (source.token.kind != expected.token.kind ||
            source.token.length != expected.token.length)
            return 0;
        for (index = 0U; index < source.token.length; ++index)
            if (tolower((unsigned char)source.token.begin[index]) !=
                tolower((unsigned char)expected.token.begin[index]))
                return 0;
    }
}

static void diagnostic_v(Context *context, size_t line, size_t column, int error,
                         const char *format, va_list args) {
    va_list copy;
    int count;
    const char *name = context->options != NULL && context->options->source_name != NULL
                           ? context->options->source_name
                           : "<input>";
    f2c_buffer_printf(&context->diagnostics, "%s:%zu:%zu: %s: ", name, line, column,
                      error ? "error" : "warning");
    va_copy(copy, args);
    count = vsnprintf(NULL, 0U, format, copy);
    va_end(copy);
    if (count >= 0) {
        buffer_reserve(&context->diagnostics, (size_t)count + 1U);
        if (!context->diagnostics.failed) {
            (void)vsnprintf(context->diagnostics.data + context->diagnostics.length,
                            context->diagnostics.capacity - context->diagnostics.length, format,
                            args);
            context->diagnostics.length += (size_t)count;
            context->diagnostics.data[context->diagnostics.length] = '\0';
            f2c_buffer_append(&context->diagnostics, "\n");
        }
    }
    if (error) {
        ++context->result.error_count;
    } else {
        ++context->result.warning_count;
    }
}

void f2c_diagnostic(Context *context, size_t line, int error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    diagnostic_v(context, line, 1U, error, format, args);
    va_end(args);
}

void f2c_diagnostic_at(Context *context, size_t line, size_t column, int error, const char *format,
                       ...) {
    va_list args;
    va_start(args, format);
    diagnostic_v(context, line, column, error, format, args);
    va_end(args);
}

int f2c_lines_push(Lines *lines, char *text, size_t number, const F2cOptions *options) {
    Line *replacement;
    size_t capacity;
    if (lines->count == lines->capacity) {
        capacity = lines->capacity == 0U ? 32U : lines->capacity * 2U;
        replacement = (Line *)realloc(lines->items, capacity * sizeof(*replacement));
        if (replacement == NULL) {
            free(text);
            return 0;
        }
        lines->items = replacement;
        lines->capacity = capacity;
    }
    lines->items[lines->count].text = text;
    lines->items[lines->count].source_name = f2c_strdup(
        options != NULL && options->source_name != NULL ? options->source_name : "<input>");
    if (lines->items[lines->count].source_name == NULL) {
        free(text);
        return 0;
    }
    lines->items[lines->count].number = number;
    lines->items[lines->count].interface_depth = 0U;
    lines->items[lines->count].emit_source_comments =
        options != NULL && options->emit_source_comments;
    lines->items[lines->count].tokens = NULL;
    lines->items[lines->count].token_count = 0U;
    ++lines->count;
    return 1;
}
