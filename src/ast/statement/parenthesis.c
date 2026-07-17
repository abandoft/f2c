#include "ast/statement/private.h"

#include <stdlib.h>
#include <string.h>

char *f2c_statement_matching_parenthesis(char *open) {
    int depth = 1;
    int quote = 0;
    char *cursor;
    for (cursor = open + 1; *cursor != '\0'; ++cursor) {
        if ((*cursor == '\'' || *cursor == '"') &&
            (quote == 0 || quote == (unsigned char)*cursor)) {
            quote = quote == 0 ? (unsigned char)*cursor : 0;
        } else if (quote == 0 && *cursor == '(') {
            ++depth;
        } else if (quote == 0 && *cursor == ')' && --depth == 0) {
            return cursor;
        }
    }
    return NULL;
}

F2cExpr *f2c_statement_parse_parenthesized(Unit *unit, char *text, char **tail) {
    char *open = strchr(text, '(');
    char *close = open != NULL ? f2c_statement_matching_parenthesis(open) : NULL;
    F2cExpr *expression;
    char *inside;
    if (open == NULL || close == NULL)
        return NULL;
    inside = f2c_strdup_n(open + 1, (size_t)(close - open - 1));
    if (inside == NULL)
        return NULL;
    expression = f2c_parse_expression_ast(unit, inside, NULL);
    free(inside);
    if (tail != NULL)
        *tail = f2c_strdup(f2c_trim(close + 1));
    return expression;
}
