#include "ast/statement/private.h"

#include <stdlib.h>
#include <string.h>

void f2c_statement_free_io_item(F2cIoItem *item) {
    size_t index;
    if (item == NULL)
        return;
    free(item->text);
    f2c_expr_free(item->expression);
    for (index = 0U; index < item->child_count; ++index)
        f2c_statement_free_io_item(&item->children[index]);
    free(item->children);
    f2c_expr_free(item->iterator);
    f2c_expr_free(item->initial);
    f2c_expr_free(item->limit);
    f2c_expr_free(item->step);
    memset(item, 0, sizeof(*item));
}

static F2cExpr *parse_range(Unit *unit, F2cTokenRange range) {
    if (range.count == 0U)
        return NULL;
    return f2c_parse_expression_tokens(unit, range.tokens, range.count, range.source, NULL);
}

static int parse_implied_do(Unit *unit, F2cTokenRange range, F2cIoItem *item) {
    F2cTokenRange interior;
    F2cTokenRange *parts = NULL;
    F2cTokenRange iterator_range;
    F2cTokenRange initial_range;
    size_t part_count = 0U;
    size_t close;
    size_t control;
    size_t equals = SIZE_MAX;
    size_t index;
    if (range.count < 5U || range.tokens[0].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(range.tokens, range.count, 0U, &close) ||
        close + 1U != range.count)
        return 0;
    interior = f2c_token_range_slice(range, 1U, close);
    if (!f2c_token_range_split_top_level(interior, F2C_TOKEN_COMMA, NULL, &parts, &part_count))
        return 0;
    for (control = 0U; control < part_count; ++control) {
        equals = f2c_token_range_find_top_level(parts[control], 0U, F2C_TOKEN_OPERATOR, "=");
        if (equals != SIZE_MAX)
            break;
    }
    if (control == 0U || control >= part_count ||
        (part_count != control + 2U && part_count != control + 3U) || equals == 0U ||
        equals + 1U == parts[control].count) {
        free(parts);
        return 0;
    }
    iterator_range = f2c_token_range_slice(parts[control], 0U, equals);
    initial_range =
        f2c_token_range_slice(parts[control], equals + 1U, parts[control].count);
    item->children = (F2cIoItem *)calloc(control, sizeof(*item->children));
    if (item->children == NULL) {
        free(parts);
        return 0;
    }
    item->implied_do = 1;
    for (index = 0U; index < control; ++index) {
        if (!f2c_statement_parse_io_item_tokens(unit, parts[index], &item->children[index]))
            goto failed;
        ++item->child_count;
    }
    item->iterator = parse_range(unit, iterator_range);
    item->initial = parse_range(unit, initial_range);
    item->limit = parse_range(unit, parts[control + 1U]);
    item->step = part_count == control + 3U ? parse_range(unit, parts[control + 2U])
                                            : f2c_parse_expression_ast(unit, "1", NULL);
    free(parts);
    return item->iterator != NULL && item->initial != NULL && item->limit != NULL &&
           item->step != NULL;

failed:
    free(parts);
    return 0;
}

int f2c_statement_parse_io_item_tokens(Unit *unit, F2cTokenRange range, F2cIoItem *item) {
    if (item == NULL || range.count == 0U || range.tokens == NULL ||
        !f2c_token_range_balanced(range.tokens, range.count))
        return 0;
    memset(item, 0, sizeof(*item));
    item->text = f2c_token_range_text(range);
    if (item->text == NULL)
        return 0;
    if (range.tokens[0].kind == F2C_TOKEN_LEFT_PAREN &&
        parse_implied_do(unit, range, item))
        return 1;
    f2c_statement_free_io_item(item);
    item->text = f2c_token_range_text(range);
    item->expression = parse_range(unit, range);
    if (item->text != NULL && item->expression != NULL)
        return 1;
    f2c_statement_free_io_item(item);
    return 0;
}

int f2c_statement_parse_io_item(Unit *unit, const char *text, F2cIoItem *item) {
    Line line;
    F2cTokenRange range;
    int result;
    if (text == NULL || item == NULL || !f2c_statement_tokenize_transient(text, 1U, &line))
        return 0;
    range = f2c_line_token_range(&line, 0U, line.token_count);
    result = f2c_statement_parse_io_item_tokens(unit, range, item);
    f2c_statement_release_transient(&line);
    return result;
}
