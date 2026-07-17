#include "frontend/token.h"

#include <stdlib.h>
#include <string.h>

F2cSourcePosition f2c_line_source_position(const Line *line, size_t logical_offset) {
    F2cSourcePosition position = {0};
    size_t index;
    position.source_name = line != NULL ? line->source_name : NULL;
    position.line = line != NULL ? line->number : 1U;
    position.column = logical_offset + 1U;
    if (line == NULL)
        return position;
    for (index = 0U; index < line->source_map_count; ++index) {
        const F2cSourceMapSegment *segment = &line->source_map[index];
        if (logical_offset >= segment->logical_begin &&
            logical_offset - segment->logical_begin < segment->length) {
            position.line = segment->physical_line;
            position.column =
                segment->physical_column + logical_offset - segment->logical_begin;
            return position;
        }
    }
    if (line->source_map_count != 0U) {
        const F2cSourceMapSegment *last = &line->source_map[line->source_map_count - 1U];
        if (logical_offset >= last->logical_begin + last->length) {
            position.line = last->physical_line;
            position.column = last->physical_column + last->length;
        }
    }
    return position;
}

F2cSourceSpan f2c_line_source_span(const Line *line, size_t logical_begin,
                                   size_t logical_length) {
    F2cSourceSpan span;
    span.begin = f2c_line_source_position(line, logical_begin);
    if (logical_length == 0U) {
        span.end = span.begin;
    } else {
        span.end = f2c_line_source_position(line, logical_begin + logical_length - 1U);
        ++span.end.column;
    }
    return span;
}

static int left_delimiter(F2cTokenKind kind) {
    return kind == F2C_TOKEN_LEFT_PAREN || kind == F2C_TOKEN_LEFT_BRACKET ||
           kind == F2C_TOKEN_ARRAY_BEGIN;
}

static int right_delimiter(F2cTokenKind kind) {
    return kind == F2C_TOKEN_RIGHT_PAREN || kind == F2C_TOKEN_RIGHT_BRACKET ||
           kind == F2C_TOKEN_ARRAY_END;
}

static int matching_kinds(F2cTokenKind left, F2cTokenKind right) {
    return (left == F2C_TOKEN_LEFT_PAREN && right == F2C_TOKEN_RIGHT_PAREN) ||
           (left == F2C_TOKEN_LEFT_BRACKET && right == F2C_TOKEN_RIGHT_BRACKET) ||
           (left == F2C_TOKEN_ARRAY_BEGIN && right == F2C_TOKEN_ARRAY_END);
}

int f2c_line_token_equals(const Line *line, size_t index, const char *text) {
    return line != NULL && index < line->token_count &&
           line->tokens[index].kind == F2C_TOKEN_IDENTIFIER &&
           f2c_token_equals(&line->tokens[index], text);
}

size_t f2c_line_find_token(const Line *line, size_t start, F2cTokenKind kind, const char *text) {
    size_t index;
    if (line == NULL)
        return SIZE_MAX;
    for (index = start; index < line->token_count; ++index) {
        if (line->tokens[index].kind == kind &&
            (text == NULL || f2c_token_equals(&line->tokens[index], text)))
            return index;
    }
    return SIZE_MAX;
}

F2cTokenRange f2c_line_token_range(const Line *line, size_t begin, size_t end) {
    F2cTokenRange range = {0};
    if (line == NULL || begin > end || end > line->token_count)
        return range;
    range.source = line->text;
    range.source_length = line->text != NULL ? strlen(line->text) : 0U;
    range.tokens = begin < end ? &line->tokens[begin] : NULL;
    range.count = end - begin;
    return range;
}

void f2c_token_cursor_init(F2cTokenCursor *cursor, const F2cToken *tokens, size_t count) {
    if (cursor == NULL)
        return;
    cursor->tokens = tokens;
    cursor->count = count;
    cursor->position = 0U;
}

const F2cToken *f2c_token_cursor_peek(const F2cTokenCursor *cursor, size_t lookahead) {
    if (cursor == NULL || cursor->tokens == NULL || lookahead > SIZE_MAX - cursor->position ||
        cursor->position + lookahead >= cursor->count)
        return NULL;
    return &cursor->tokens[cursor->position + lookahead];
}

const F2cToken *f2c_token_cursor_take(F2cTokenCursor *cursor) {
    const F2cToken *token = f2c_token_cursor_peek(cursor, 0U);
    if (token != NULL)
        ++cursor->position;
    return token;
}

int f2c_token_cursor_consume(F2cTokenCursor *cursor, F2cTokenKind kind, const char *text) {
    const F2cToken *token = f2c_token_cursor_peek(cursor, 0U);
    if (token == NULL || token->kind != kind || (text != NULL && !f2c_token_equals(token, text)))
        return 0;
    ++cursor->position;
    return 1;
}

int f2c_token_matching_delimiter(const F2cToken *tokens, size_t count, size_t open_index,
                                 size_t *close_index) {
    F2cTokenKind *stack;
    size_t capacity;
    size_t depth = 0U;
    size_t index;
    if (tokens == NULL || open_index >= count || !left_delimiter(tokens[open_index].kind))
        return 0;
    capacity = count - open_index;
    if (capacity > SIZE_MAX / sizeof(*stack))
        return 0;
    stack = (F2cTokenKind *)malloc(capacity * sizeof(*stack));
    if (stack == NULL)
        return 0;
    for (index = open_index; index < count; ++index) {
        const F2cTokenKind kind = tokens[index].kind;
        if (left_delimiter(kind)) {
            stack[depth++] = kind;
        } else if (right_delimiter(kind)) {
            if (depth == 0U || !matching_kinds(stack[depth - 1U], kind)) {
                free(stack);
                return 0;
            }
            if (--depth == 0U) {
                if (close_index != NULL)
                    *close_index = index;
                free(stack);
                return 1;
            }
        }
    }
    free(stack);
    return 0;
}

int f2c_token_range_balanced(const F2cToken *tokens, size_t count) {
    size_t index;
    if (tokens == NULL)
        return count == 0U;
    for (index = 0U; index < count; ++index) {
        size_t close;
        if (right_delimiter(tokens[index].kind))
            return 0;
        if (left_delimiter(tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(tokens, count, index, &close))
                return 0;
            index = close;
        }
    }
    return 1;
}

char *f2c_token_range_text(F2cTokenRange range) {
    uintptr_t source;
    uintptr_t source_end;
    uintptr_t begin;
    uintptr_t last_begin;
    const F2cToken *last;
    if (range.count == 0U)
        return f2c_strdup("");
    if (range.source == NULL || range.tokens == NULL)
        return NULL;
    source = (uintptr_t)(const void *)range.source;
    if (range.source_length > UINTPTR_MAX - source)
        return NULL;
    source_end = source + range.source_length;
    begin = (uintptr_t)(const void *)range.tokens[0].begin;
    last = &range.tokens[range.count - 1U];
    last_begin = (uintptr_t)(const void *)last->begin;
    if (begin < source || begin > source_end || last_begin < begin || last_begin > source_end ||
        last->length > source_end - last_begin)
        return NULL;
    return f2c_strdup_n((const char *)(const void *)begin,
                        (size_t)(last_begin - begin) + last->length);
}
