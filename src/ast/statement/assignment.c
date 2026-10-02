#include "ast/statement/private.h"

#include <stdlib.h>

F2cStatementKind f2c_statement_classify_assignment(Unit *unit, const Line *line, size_t begin) {
    size_t cursor = begin;
    if (line == NULL || cursor >= line->token_count ||
        line->tokens[cursor].kind != F2C_TOKEN_IDENTIFIER)
        return F2C_STMT_INVALID;
    ++cursor;
    while (cursor < line->token_count) {
        const F2cToken *token = &line->tokens[cursor];
        if (token->kind == F2C_TOKEN_LEFT_PAREN) {
            size_t close;
            if (cursor == begin + 1U && unit != NULL) {
                char *name = f2c_token_text(&line->tokens[begin]);
                Symbol *symbol = name != NULL ? f2c_find_symbol(unit, name) : NULL;
                free(name);
                if (symbol == NULL)
                    return F2C_STMT_INVALID;
            }
            if (!f2c_token_matching_delimiter(line->tokens, line->token_count, cursor, &close))
                return F2C_STMT_INVALID;
            cursor = close + 1U;
        } else if (token->kind == F2C_TOKEN_PERCENT && cursor + 1U < line->token_count &&
                   line->tokens[cursor + 1U].kind == F2C_TOKEN_IDENTIFIER) {
            cursor += 2U;
        } else if (token->kind == F2C_TOKEN_OPERATOR && f2c_token_equals(token, "=")) {
            return cursor + 1U < line->token_count ? F2C_STMT_ASSIGNMENT : F2C_STMT_INVALID;
        } else if (token->kind == F2C_TOKEN_OPERATOR && f2c_token_equals(token, "=>")) {
            return cursor + 1U < line->token_count ? F2C_STMT_POINTER_ASSIGNMENT : F2C_STMT_INVALID;
        } else {
            return F2C_STMT_INVALID;
        }
    }
    return F2C_STMT_INVALID;
}

static int store_assignment(Unit *unit, const Line *line, F2cTokenRange range, size_t equals,
                            F2cStatement *statement) {
    F2cTokenRange left;
    F2cTokenRange right;
    if (equals == 0U || equals + 1U >= range.count)
        return 0;
    left = f2c_token_range_slice(range, 0U, equals);
    right = f2c_token_range_slice(range, equals + 1U, range.count);
    statement->items = (char **)calloc(2U, sizeof(*statement->items));
    statement->arguments = (F2cExpr **)calloc(2U, sizeof(*statement->arguments));
    if (statement->items == NULL || statement->arguments == NULL)
        return 0;
    statement->item_count = 2U;
    statement->operator_span = range.tokens[equals].span;
    statement->items[0] = f2c_token_range_text(left);
    statement->items[1] = f2c_token_range_text(right);
    statement->left = f2c_parse_expression_tokens(unit, left.tokens, left.count, line->text, NULL);
    statement->right =
        f2c_parse_expression_tokens(unit, right.tokens, right.count, line->text, NULL);
    return statement->items[0] != NULL && statement->items[1] != NULL && statement->left != NULL &&
           statement->right != NULL;
}

int f2c_statement_parse_assignment(Unit *unit, const Line *line, size_t body_start,
                                   F2cStatement *statement) {
    F2cTokenRange range;
    size_t equals;
    if (unit == NULL || line == NULL || statement == NULL || body_start >= line->token_count)
        return 0;
    range = f2c_line_token_range(line, body_start, line->token_count);
    equals = f2c_token_range_find_top_level(range, 0U, F2C_TOKEN_OPERATOR, "=>");
    if (equals != SIZE_MAX) {
        statement->kind = F2C_STMT_POINTER_ASSIGNMENT;
        return store_assignment(unit, line, range, equals, statement);
    }
    equals = f2c_token_range_find_top_level(range, 0U, F2C_TOKEN_OPERATOR, "=");
    if (equals == SIZE_MAX)
        return 1;
    statement->kind = F2C_STMT_ASSIGNMENT;
    return store_assignment(unit, line, range, equals, statement);
}
