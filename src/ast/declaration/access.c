#include "ast/declaration/access.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int set_error(F2cAccessStatementSyntax *syntax, F2cAccessStatementError error,
                     const F2cToken *token) {
    syntax->error = error;
    syntax->error_token = token;
    return 0;
}

static size_t statement_start(const Line *line) {
    return line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
}

int f2c_access_statement_candidate(const Line *line) {
    size_t start;
    if (line == NULL || line->token_count == 0U)
        return 0;
    start = statement_start(line);
    if (!f2c_line_token_equals(line, start, "public") &&
        !f2c_line_token_equals(line, start, "private"))
        return 0;
    if (start + 1U >= line->token_count)
        return 1;
    return !((line->tokens[start + 1U].kind == F2C_TOKEN_OPERATOR &&
              f2c_token_equals(&line->tokens[start + 1U], "=")) ||
             line->tokens[start + 1U].kind == F2C_TOKEN_LEFT_PAREN ||
             line->tokens[start + 1U].kind == F2C_TOKEN_PERCENT);
}

static int append_item(F2cAccessStatementSyntax *syntax, F2cGenericDesignatorSyntax item) {
    F2cGenericDesignatorSyntax *replacement;
    size_t index;
    for (index = 0U; index < syntax->item_count; ++index) {
        if (f2c_generic_designators_equal(&syntax->items[index], &item))
            return set_error(syntax, F2C_ACCESS_ERROR_DUPLICATE_ITEM, item.range.tokens);
    }
    if (syntax->item_count == syntax->item_capacity) {
        const size_t capacity = syntax->item_capacity == 0U ? 8U : syntax->item_capacity * 2U;
        if (capacity < syntax->item_capacity || capacity > SIZE_MAX / sizeof(*replacement))
            return -1;
        replacement =
            (F2cGenericDesignatorSyntax *)realloc(syntax->items, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return -1;
        syntax->items = replacement;
        syntax->item_capacity = capacity;
    }
    syntax->items[syntax->item_count++] = item;
    return 1;
}

F2cAccessStatementStatus f2c_parse_access_statement_syntax(const Line *line,
                                                           F2cAccessStatementSyntax *syntax) {
    size_t start;
    size_t index;
    if (syntax == NULL)
        return F2C_ACCESS_STATEMENT_INVALID;
    memset(syntax, 0, sizeof(*syntax));
    if (!f2c_access_statement_candidate(line))
        return F2C_ACCESS_STATEMENT_NOT_MATCHED;
    start = statement_start(line);
    syntax->keyword = &line->tokens[start];
    syntax->kind =
        f2c_token_equals(syntax->keyword, "public") ? F2C_ACCESS_PUBLIC : F2C_ACCESS_PRIVATE;
    syntax->span = f2c_source_span_cover(&line->tokens[start].span,
                                         &line->tokens[line->token_count - 1U].span);
    index = start + 1U;
    if (index == line->token_count)
        return F2C_ACCESS_STATEMENT_PARSED;
    if (line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON) {
        syntax->double_colon = &line->tokens[index++];
        if (index == line->token_count) {
            set_error(syntax, F2C_ACCESS_ERROR_EMPTY_LIST, syntax->double_colon);
            return F2C_ACCESS_STATEMENT_INVALID;
        }
    }
    while (index < line->token_count) {
        F2cGenericDesignatorSyntax item;
        size_t end;
        int appended;
        if (f2c_parse_generic_designator_syntax(line, index, &end, &item) !=
            F2C_GENERIC_DESIGNATOR_PARSED) {
            set_error(syntax, F2C_ACCESS_ERROR_ITEM, &line->tokens[index]);
            return F2C_ACCESS_STATEMENT_INVALID;
        }
        appended = append_item(syntax, item);
        if (appended < 0)
            return F2C_ACCESS_STATEMENT_NO_MEMORY;
        if (!appended)
            return F2C_ACCESS_STATEMENT_INVALID;
        index = end;
        if (index == line->token_count)
            return F2C_ACCESS_STATEMENT_PARSED;
        if (line->tokens[index].kind != F2C_TOKEN_COMMA) {
            set_error(syntax, F2C_ACCESS_ERROR_LIST_SEPARATOR, &line->tokens[index]);
            return F2C_ACCESS_STATEMENT_INVALID;
        }
        ++index;
        if (index == line->token_count) {
            set_error(syntax, F2C_ACCESS_ERROR_TRAILING_COMMA, &line->tokens[index - 1U]);
            return F2C_ACCESS_STATEMENT_INVALID;
        }
    }
    return F2C_ACCESS_STATEMENT_PARSED;
}

void f2c_access_statement_syntax_discard(F2cAccessStatementSyntax *syntax) {
    if (syntax == NULL)
        return;
    free(syntax->items);
    memset(syntax, 0, sizeof(*syntax));
}
