#include "ast/statement/private.h"

#include <stdlib.h>

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
