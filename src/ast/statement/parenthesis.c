#include "ast/statement/private.h"

F2cExpr *f2c_statement_parse_parenthesized_tokens(Unit *unit, const Line *line, size_t begin,
                                                  char **tail) {
    size_t open;
    size_t close;
    const F2cToken *last;
    if (line == NULL)
        return NULL;
    for (open = begin; open < line->token_count; ++open) {
        if (line->tokens[open].kind == F2C_TOKEN_LEFT_PAREN)
            break;
    }
    if (open == line->token_count ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, open, &close))
        return NULL;
    if (tail != NULL) {
        if (close + 1U == line->token_count) {
            *tail = f2c_strdup("");
        } else {
            const F2cToken *first = &line->tokens[close + 1U];
            last = &line->tokens[line->token_count - 1U];
            *tail =
                f2c_strdup_n(first->begin, (size_t)((last->begin + last->length) - first->begin));
        }
    }
    return f2c_parse_expression_tokens(unit, line->tokens + open + 1U, close - open - 1U,
                                       line->text, NULL);
}
