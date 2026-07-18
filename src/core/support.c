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
    if (buffer->limit != 0U &&
        (buffer->length > buffer->limit || extra > buffer->limit - buffer->length)) {
        buffer->limit_exceeded = 1;
        buffer->failed = 1;
        return;
    }
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
    F2cTokenStream source;
    F2cTokenStream expected;
    f2c_token_stream_init(&source, text, 1U, 1U);
    f2c_token_stream_init(&expected, word, 1U, 1U);
    for (;;) {
        size_t index;
        f2c_token_stream_next(&expected);
        if (expected.token.kind == F2C_TOKEN_END)
            return 1;
        f2c_token_stream_next(&source);
        if (source.token.kind != expected.token.kind ||
            source.token.length != expected.token.length)
            return 0;
        for (index = 0U; index < source.token.length; ++index)
            if (tolower((unsigned char)source.token.begin[index]) !=
                tolower((unsigned char)expected.token.begin[index]))
                return 0;
    }
}

static const char *diagnostic_source_name(const Context *context) {
    return context->options != NULL && context->options->source_name != NULL
               ? context->options->source_name
               : "<input>";
}

static void emit_structured_diagnostic(Context *context, F2cDiagnosticCode code,
                                       const F2cSourceSpan *span, size_t line, size_t column,
                                       int error, const char *message, size_t message_length) {
    F2cDiagnostic diagnostic;
    const char *name;
    if (context->diagnostic_callback == NULL)
        return;
    name = span != NULL && span->begin.source_name != NULL ? span->begin.source_name
                                                           : diagnostic_source_name(context);
    memset(&diagnostic, 0, sizeof(diagnostic));
    diagnostic.code = code;
    diagnostic.severity = error ? F2C_DIAGNOSTIC_ERROR : F2C_DIAGNOSTIC_WARNING;
    diagnostic.begin.source_name = name;
    diagnostic.begin.line = line;
    diagnostic.begin.column = column;
    if (span != NULL) {
        diagnostic.end.source_name = span->end.source_name != NULL ? span->end.source_name : name;
        diagnostic.end.line = span->end.line;
        diagnostic.end.column = span->end.column;
        diagnostic.has_spelling_location = span->has_spelling;
        if (span->has_spelling) {
            diagnostic.spelling_begin.source_name = span->spelling_begin.source_name;
            diagnostic.spelling_begin.line = span->spelling_begin.line;
            diagnostic.spelling_begin.column = span->spelling_begin.column;
            diagnostic.spelling_end.source_name = span->spelling_end.source_name;
            diagnostic.spelling_end.line = span->spelling_end.line;
            diagnostic.spelling_end.column = span->spelling_end.column;
        }
    } else {
        diagnostic.end = diagnostic.begin;
    }
    diagnostic.message = message;
    diagnostic.message_length = message_length;
    context->diagnostic_callback(&diagnostic, context->diagnostic_user_data);
}

static void diagnostic_v(Context *context, F2cDiagnosticCode code, const F2cSourceSpan *span,
                         size_t line, size_t column, int error, const char *format, va_list args) {
    va_list copy;
    int prefix_count;
    int message_count;
    int spelling_note_count = 0;
    size_t total;
    char *message = NULL;
    const char *name = span != NULL && span->begin.source_name != NULL
                           ? span->begin.source_name
                           : diagnostic_source_name(context);
    if (span != NULL) {
        line = span->begin.line;
        column = span->begin.column;
    }
    if (context->limits.max_diagnostics != 0U &&
        context->diagnostic_count >= context->limits.max_diagnostics) {
        if (!context->diagnostic_limit_reported) {
            static const char notice[] =
                "f2c: error: diagnostic count limit reached; remaining diagnostics suppressed\n";
            static const char callback_notice[] =
                "diagnostic count limit reached; remaining diagnostics suppressed";
            context->diagnostic_limit_reported = 1;
            ++context->result.error_count;
            emit_structured_diagnostic(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, span, line, column,
                                       1, callback_notice, sizeof(callback_notice) - 1U);
            if (context->diagnostics.limit == 0U ||
                (context->diagnostics.length <= context->diagnostics.limit &&
                 sizeof(notice) - 1U <= context->diagnostics.limit - context->diagnostics.length)) {
                f2c_buffer_append_n(&context->diagnostics, notice, sizeof(notice) - 1U);
            } else if (!context->diagnostic_bytes_limit_reported) {
                context->diagnostics.limit_exceeded = 1;
                context->diagnostic_bytes_limit_reported = 1;
                ++context->result.error_count;
            }
        }
        return;
    }
    ++context->diagnostic_count;
    prefix_count =
        snprintf(NULL, 0U, "%s:%zu:%zu: %s: ", name, line, column, error ? "error" : "warning");
    va_copy(copy, args);
    message_count = vsnprintf(NULL, 0U, format, copy);
    va_end(copy);
    if (span != NULL && span->has_spelling) {
        const char *spelling_name =
            span->spelling_begin.source_name != NULL ? span->spelling_begin.source_name : name;
        spelling_note_count =
            snprintf(NULL, 0U, "%s:%zu:%zu: note: expanded from macro definition\n", spelling_name,
                     span->spelling_begin.line, span->spelling_begin.column);
    }
    if (prefix_count < 0 || message_count < 0 || spelling_note_count < 0 ||
        (size_t)prefix_count > SIZE_MAX - (size_t)message_count - 1U ||
        (size_t)spelling_note_count >
            SIZE_MAX - (size_t)prefix_count - (size_t)message_count - 1U) {
        context->diagnostics.failed = 1;
    } else {
        message = (char *)malloc((size_t)message_count + 1U);
        if (message == NULL) {
            static const char fallback[] = "unable to format diagnostic due to memory exhaustion";
            context->diagnostics.failed = 1;
            emit_structured_diagnostic(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, span, line, column, 1,
                                       fallback, sizeof(fallback) - 1U);
            goto counted;
        }
        va_copy(copy, args);
        (void)vsnprintf(message, (size_t)message_count + 1U, format, copy);
        va_end(copy);
        emit_structured_diagnostic(context, code, span, line, column, error, message,
                                   (size_t)message_count);
        total = (size_t)prefix_count + (size_t)message_count + 1U + (size_t)spelling_note_count;
        if (context->diagnostics.limit != 0U &&
            (context->diagnostics.length > context->diagnostics.limit ||
             total > context->diagnostics.limit - context->diagnostics.length)) {
            context->diagnostics.limit_exceeded = 1;
            if (!context->diagnostic_bytes_limit_reported) {
                static const char notice[] =
                    "diagnostic byte limit reached; remaining text diagnostics suppressed";
                context->diagnostic_bytes_limit_reported = 1;
                ++context->result.error_count;
                emit_structured_diagnostic(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, span, line,
                                           column, 1, notice, sizeof(notice) - 1U);
            }
        } else {
            buffer_reserve(&context->diagnostics, total);
            if (!context->diagnostics.failed) {
                (void)snprintf(context->diagnostics.data + context->diagnostics.length,
                               context->diagnostics.capacity - context->diagnostics.length,
                               "%s:%zu:%zu: %s: ", name, line, column, error ? "error" : "warning");
                context->diagnostics.length += (size_t)prefix_count;
                memcpy(context->diagnostics.data + context->diagnostics.length, message,
                       (size_t)message_count);
                context->diagnostics.length += (size_t)message_count;
                context->diagnostics.data[context->diagnostics.length++] = '\n';
                context->diagnostics.data[context->diagnostics.length] = '\0';
                if (spelling_note_count != 0) {
                    const char *spelling_name = span->spelling_begin.source_name != NULL
                                                    ? span->spelling_begin.source_name
                                                    : name;
                    (void)snprintf(context->diagnostics.data + context->diagnostics.length,
                                   context->diagnostics.capacity - context->diagnostics.length,
                                   "%s:%zu:%zu: note: expanded from macro definition\n",
                                   spelling_name, span->spelling_begin.line,
                                   span->spelling_begin.column);
                    context->diagnostics.length += (size_t)spelling_note_count;
                }
            }
        }
    }
    free(message);
counted:
    if (error) {
        ++context->result.error_count;
    } else {
        ++context->result.warning_count;
    }
}

void f2c_diagnostic(Context *context, size_t line, int error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    diagnostic_v(context, F2C_DIAGNOSTIC_GENERAL, NULL, line, 1U, error, format, args);
    va_end(args);
}

void f2c_diagnostic_at(Context *context, size_t line, size_t column, int error, const char *format,
                       ...) {
    va_list args;
    va_start(args, format);
    diagnostic_v(context, F2C_DIAGNOSTIC_GENERAL, NULL, line, column, error, format, args);
    va_end(args);
}

void f2c_diagnostic_code(Context *context, F2cDiagnosticCode code, size_t line, int error,
                         const char *format, ...) {
    va_list args;
    va_start(args, format);
    diagnostic_v(context, code, NULL, line, 1U, error, format, args);
    va_end(args);
}

void f2c_diagnostic_at_code(Context *context, F2cDiagnosticCode code, size_t line, size_t column,
                            int error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    diagnostic_v(context, code, NULL, line, column, error, format, args);
    va_end(args);
}

void f2c_diagnostic_span_code(Context *context, F2cDiagnosticCode code, const F2cSourceSpan *span,
                              int error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    diagnostic_v(context, code, span, span != NULL ? span->begin.line : 1U,
                 span != NULL ? span->begin.column : 1U, error, format, args);
    va_end(args);
}

void f2c_diagnostic_token_code(Context *context, F2cDiagnosticCode code, const Line *line,
                               const F2cToken *token, int error, const char *format, ...) {
    F2cSourceSpan span;
    va_list args;
    if (token != NULL) {
        span = token->span;
        if (span.begin.source_name == NULL)
            span.begin.source_name =
                line != NULL ? line->source_name : diagnostic_source_name(context);
        if (span.end.source_name == NULL)
            span.end.source_name = span.begin.source_name;
    } else {
        span = f2c_line_source_span(line, 0U, 0U);
    }
    va_start(args, format);
    diagnostic_v(context, code, &span, span.begin.line, span.begin.column, error, format, args);
    va_end(args);
}

int f2c_lines_push_mapped(Context *context, char *text, size_t number,
                          F2cSourceMapSegment *source_map, size_t source_map_count,
                          const F2cOptions *options) {
    Lines *lines = &context->lines;
    Line *replacement;
    size_t capacity;
    if (context->limits.max_logical_lines != 0U &&
        lines->count >= context->limits.max_logical_lines) {
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, number, 1,
                            "logical-line limit of %zu exceeded",
                            context->limits.max_logical_lines);
        free(text);
        free(source_map);
        return 0;
    }
    if (lines->count == lines->capacity) {
        capacity = lines->capacity == 0U ? 32U : lines->capacity * 2U;
        replacement = (Line *)realloc(lines->items, capacity * sizeof(*replacement));
        if (replacement == NULL) {
            free(text);
            free(source_map);
            return 0;
        }
        lines->items = replacement;
        lines->capacity = capacity;
    }
    lines->items[lines->count].text = text;
    lines->items[lines->count].source_name =
        source_map_count != 0U && source_map[0].expansion.source_name != NULL
            ? source_map[0].expansion.source_name
            : f2c_context_source_name(context, options != NULL && options->source_name != NULL
                                                   ? options->source_name
                                                   : "<input>");
    if (lines->items[lines->count].source_name == NULL) {
        free(text);
        free(source_map);
        return 0;
    }
    lines->items[lines->count].number = number;
    lines->items[lines->count].interface_depth = 0U;
    lines->items[lines->count].emit_source_comments =
        options != NULL && options->emit_source_comments;
    lines->items[lines->count].tokens = NULL;
    lines->items[lines->count].token_count = 0U;
    lines->items[lines->count].source_map = source_map;
    lines->items[lines->count].source_map_count = source_map_count;
    ++lines->count;
    return 1;
}

int f2c_lines_push(Context *context, char *text, size_t number, const F2cOptions *options) {
    return f2c_lines_push_mapped(context, text, number, NULL, 0U, options);
}
