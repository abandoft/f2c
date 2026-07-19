#include "ast/statement/private.h"

#include "ast/format.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void f2c_statement_parse_label(Unit *unit, const Line *line, F2cStatement *statement) {
    F2cTokenRange tail;
    F2cSourceSpan format_span = {0};
    char *text;
    if (line == NULL || line->token_count == 0U || line->tokens[0].kind != F2C_TOKEN_NUMBER) {
        statement->kind = F2C_STMT_INVALID;
        return;
    }
    statement->name = f2c_token_text(&line->tokens[0]);
    if (statement->name == NULL)
        return;
    if (line->token_count == 1U)
        return;
    if (line->tokens[1].kind == F2C_TOKEN_IDENTIFIER &&
        f2c_token_equals(&line->tokens[1], "format")) {
        statement->kind = F2C_STMT_FORMAT;
        if (line->token_count < 3U) {
            statement->format_span = line->tokens[1].span;
            statement->format_error.code = F2C_FORMAT_ERROR_EXPECTED_LEFT_PARENTHESIS;
            return;
        }
        tail = f2c_line_token_range(line, 2U, line->token_count);
        format_span.begin = tail.tokens[0].span.begin;
        format_span.end = tail.tokens[tail.count - 1U].span.end;
        format_span.spelling_begin = tail.tokens[0].span.spelling_begin;
        format_span.spelling_end = tail.tokens[tail.count - 1U].span.spelling_end;
        format_span.has_spelling =
            tail.tokens[0].span.has_spelling || tail.tokens[tail.count - 1U].span.has_spelling;
        statement->format_span = format_span;
        text = f2c_token_range_text(tail);
        if (text == NULL) {
            statement->format_error.code = F2C_FORMAT_ERROR_MEMORY;
            return;
        }
        statement->format =
            f2c_format_parse(text, strlen(text), &format_span, &statement->format_error);
        free(text);
        statement->format_syntax_valid = statement->format != NULL;
        return;
    }
    tail = f2c_line_token_range(line, 1U, line->token_count);
    text = f2c_token_range_text(tail);
    if (text == NULL)
        return;
    statement->nested = (F2cStatement *)calloc(1U, sizeof(*statement->nested));
    if (statement->nested != NULL &&
        !f2c_parse_statement(unit, text, statement->line, statement->nested)) {
        free(statement->nested);
        statement->nested = NULL;
    }
    free(text);
}

int f2c_statement_parse_arithmetic_labels(F2cStatement *statement) {
    char **labels;
    size_t count = 0U;
    size_t index;
    if (statement->tail == NULL)
        return 0;
    labels = f2c_statement_split_arguments(statement->tail, &count);
    if (labels == NULL || count != 3U)
        goto failed;
    for (index = 0U; index < count; ++index) {
        const char *cursor = labels[index];
        if (*cursor == '\0')
            goto failed;
        while (isdigit((unsigned char)*cursor))
            ++cursor;
        if (*cursor != '\0')
            goto failed;
    }
    statement->labels = labels;
    statement->label_count = count;
    statement->kind = F2C_STMT_ARITHMETIC_IF;
    return 1;

failed:
    while (count != 0U)
        free(labels[--count]);
    free(labels);
    return 0;
}
