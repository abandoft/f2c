#include "ast/statement/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int left_delimiter(F2cTokenKind kind) {
    return kind == F2C_TOKEN_LEFT_PAREN || kind == F2C_TOKEN_LEFT_BRACKET ||
           kind == F2C_TOKEN_ARRAY_BEGIN;
}

static int append_token(Line *line, const F2cToken *token, size_t *capacity) {
    F2cToken *replacement;
    size_t next;
    if (line->token_count < *capacity) {
        line->tokens[line->token_count++] = *token;
        return 1;
    }
    next = *capacity == 0U ? 16U : *capacity * 2U;
    if (next < *capacity || next > SIZE_MAX / sizeof(*line->tokens))
        return 0;
    replacement = (F2cToken *)realloc(line->tokens, next * sizeof(*line->tokens));
    if (replacement == NULL)
        return 0;
    line->tokens = replacement;
    *capacity = next;
    line->tokens[line->token_count++] = *token;
    return 1;
}

int f2c_statement_tokenize_transient(const char *text, size_t line_number, Line *line) {
    F2cTokenStream stream;
    size_t capacity = 0U;
    if (text == NULL || line == NULL)
        return 0;
    memset(line, 0, sizeof(*line));
    line->text = (char *)text;
    line->number = line_number;
    f2c_token_stream_init(&stream, text, line_number, 1U);
    for (;;) {
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            return 1;
        if (!append_token(line, &stream.token, &capacity)) {
            f2c_statement_release_transient(line);
            return 0;
        }
    }
}

void f2c_statement_release_transient(Line *line) {
    if (line == NULL)
        return;
    free(line->tokens);
    memset(line, 0, sizeof(*line));
}

static size_t top_level_token(const F2cToken *tokens, size_t begin, size_t end, F2cTokenKind kind,
                              size_t after) {
    size_t index = begin;
    while (index < end) {
        size_t close;
        if (left_delimiter(tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(tokens, end, index, &close))
                return SIZE_MAX;
            index = close + 1U;
            continue;
        }
        if (tokens[index].kind == kind && index >= after)
            return index;
        ++index;
    }
    return SIZE_MAX;
}

static F2cSourceSpan token_range_span(const Line *line, size_t begin, size_t end) {
    F2cSourceSpan span = {0};
    if (line == NULL || begin >= end || end > line->token_count)
        return span;
    span.begin = line->tokens[begin].span.begin;
    span.end = line->tokens[end - 1U].span.end;
    return span;
}

static F2cExpr *parse_endpoint(Unit *unit, const Line *line, size_t begin, size_t end) {
    return begin < end ? f2c_parse_expression_tokens(unit, &line->tokens[begin], end - begin,
                                                     line->text, NULL)
                       : NULL;
}

static int append_range(Unit *unit, const Line *line, F2cStatement *statement, size_t begin,
                        size_t end) {
    F2cCaseRange range = {0};
    F2cCaseRange *replacement;
    size_t colon;
    size_t second_colon;
    if (begin >= end) {
        statement->case_syntax_valid = 0;
        return 1;
    }
    colon = top_level_token(line->tokens, begin, end, F2C_TOKEN_COLON, begin);
    if (colon != SIZE_MAX) {
        second_colon = top_level_token(line->tokens, colon + 1U, end, F2C_TOKEN_COLON, colon + 1U);
        if (second_colon != SIZE_MAX || (colon == begin && colon + 1U == end)) {
            statement->case_syntax_valid = 0;
            return 1;
        }
        range.has_colon = 1;
        range.lower = parse_endpoint(unit, line, begin, colon);
        range.upper = parse_endpoint(unit, line, colon + 1U, end);
        if ((begin < colon && range.lower == NULL) || (colon + 1U < end && range.upper == NULL)) {
            f2c_expr_free(range.lower);
            f2c_expr_free(range.upper);
            return 0;
        }
    } else {
        range.lower = parse_endpoint(unit, line, begin, end);
        if (range.lower == NULL)
            return 0;
    }
    range.span = token_range_span(line, begin, end);
    if (statement->case_range_count >= SIZE_MAX / sizeof(*statement->case_ranges)) {
        f2c_expr_free(range.lower);
        f2c_expr_free(range.upper);
        return 0;
    }
    replacement =
        (F2cCaseRange *)realloc(statement->case_ranges, (statement->case_range_count + 1U) *
                                                            sizeof(*statement->case_ranges));
    if (replacement == NULL) {
        f2c_expr_free(range.lower);
        f2c_expr_free(range.upper);
        return 0;
    }
    statement->case_ranges = replacement;
    statement->case_ranges[statement->case_range_count++] = range;
    return 1;
}

int f2c_statement_parse_case(Unit *unit, const Line *line, F2cStatement *statement) {
    size_t close;
    size_t begin;
    size_t separator;
    if (line == NULL || statement == NULL || line->token_count < 2U)
        return 1;
    statement->case_syntax_valid = 1;
    if (line->tokens[1].kind == F2C_TOKEN_IDENTIFIER &&
        f2c_token_equals(&line->tokens[1], "default")) {
        statement->case_default = 1;
        if (line->token_count > 3U ||
            (line->token_count == 3U && line->tokens[2].kind != F2C_TOKEN_IDENTIFIER))
            statement->case_syntax_valid = 0;
        return 1;
    }
    if (line->tokens[1].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, 1U, &close) ||
        close + 2U < line->token_count ||
        (close + 1U < line->token_count && line->tokens[close + 1U].kind != F2C_TOKEN_IDENTIFIER)) {
        statement->case_syntax_valid = 0;
        return 1;
    }
    begin = 2U;
    while (begin < close) {
        separator = top_level_token(line->tokens, begin, close, F2C_TOKEN_COMMA, begin);
        if (separator == SIZE_MAX)
            separator = close;
        if (!append_range(unit, line, statement, begin, separator))
            return 0;
        begin = separator + 1U;
    }
    if (statement->case_range_count == 0U || begin != close + 1U)
        statement->case_syntax_valid = 0;
    return 1;
}
