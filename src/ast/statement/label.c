#include "ast/statement/private.h"

#include "ast/format.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *f2c_statement_copy_label_token(const F2cToken *token) {
    size_t first = 0U;
    size_t index;
    if (token == NULL || token->kind != F2C_TOKEN_NUMBER || token->length == 0U ||
        token->length > 5U)
        return NULL;
    for (index = 0U; index < token->length; ++index)
        if (!isdigit((unsigned char)token->begin[index]))
            return NULL;
    while (first < token->length && token->begin[first] == '0')
        ++first;
    if (first == token->length)
        return NULL;
    return f2c_strdup_n(token->begin + first, token->length - first);
}

const char *f2c_statement_label_canonical(const char *label) {
    if (label == NULL)
        return NULL;
    while (label[0] == '0' && label[1] != '\0')
        ++label;
    return label;
}

int f2c_statement_labels_equal(const char *left, const char *right) {
    left = f2c_statement_label_canonical(left);
    right = f2c_statement_label_canonical(right);
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

void f2c_statement_parse_label(Unit *unit, const Line *line, F2cStatement *statement) {
    F2cTokenRange tail;
    F2cSourceSpan format_span = {0};
    char *text;
    if (line == NULL || line->token_count == 0U || line->tokens[0].kind != F2C_TOKEN_NUMBER) {
        statement->kind = F2C_STMT_INVALID;
        return;
    }
    statement->name = f2c_statement_copy_label_token(&line->tokens[0]);
    statement->label_span = line->tokens[0].span;
    if (statement->name == NULL) {
        statement->control_syntax_valid = 0;
        return;
    }
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
    (void)f2c_statement_parse_nested_tokens(unit, line, 1U, &statement->nested);
}
