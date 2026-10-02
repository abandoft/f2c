#include "ast/statement/private.h"

int f2c_statement_parse_where(Unit *unit, const Line *line, size_t body_start,
                              F2cStatement *statement) {
    const size_t open = body_start + 1U;
    size_t close;
    F2cTokenRange mask;
    F2cTokenRange tail;
    size_t equals;
    const int is_where = statement->kind == F2C_STMT_WHERE;

    statement->block = is_where;
    if (open >= line->token_count || line->tokens[open].kind != F2C_TOKEN_LEFT_PAREN) {
        if (is_where)
            statement->control_syntax_valid = 0;
        return 1;
    }
    if (!f2c_token_matching_delimiter(line->tokens, line->token_count, open, &close) ||
        close == open + 1U) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    mask = f2c_line_token_range(line, open + 1U, close);
    statement->expression =
        f2c_parse_expression_tokens(unit, mask.tokens, mask.count, line->text, NULL);
    if (statement->expression == NULL) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    if (!is_where)
        return 1;

    tail = f2c_line_token_range(line, close + 1U, line->token_count);
    statement->tail = f2c_token_range_text(tail);
    if (statement->tail == NULL)
        return 0;
    if (tail.count == 0U)
        return 1;
    statement->block = 0;
    equals = f2c_token_range_find_top_level(tail, 0U, F2C_TOKEN_OPERATOR, "=");
    if (!f2c_token_range_balanced(tail.tokens, tail.count) || equals == SIZE_MAX || equals == 0U ||
        equals + 1U == tail.count) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    return f2c_statement_parse_nested_tokens(unit, line, close + 1U, &statement->nested);
}
