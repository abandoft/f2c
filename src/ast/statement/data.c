#include "ast/statement/private.h"

#include <stdlib.h>
#include <string.h>

static F2cSourceSpan range_span(F2cTokenRange range) {
    F2cSourceSpan span = {0};
    if (range.count != 0U)
        span = f2c_source_span_cover(&range.tokens[0].span, &range.tokens[range.count - 1U].span);
    return span;
}

void f2c_statement_free_data_group(F2cDataGroup *group) {
    size_t index;
    if (group == NULL)
        return;
    for (index = 0U; index < group->target_count; ++index)
        f2c_statement_free_io_item(&group->targets[index]);
    free(group->targets);
    for (index = 0U; index < group->value_count; ++index) {
        free(group->values[index].text);
        f2c_expr_free(group->values[index].expression);
        f2c_expr_free(group->values[index].repeat);
    }
    free(group->values);
    memset(group, 0, sizeof(*group));
}

static F2cExpr *parse_expression(Unit *unit, F2cTokenRange range) {
    if (range.count == 0U)
        return NULL;
    return f2c_parse_expression_tokens(unit, range.tokens, range.count, range.source, NULL);
}

static int parse_targets(Unit *unit, F2cTokenRange range, F2cDataGroup *group) {
    F2cTokenRange *targets = NULL;
    size_t count = 0U;
    size_t index;
    if (!f2c_token_range_split_top_level(range, F2C_TOKEN_COMMA, NULL, &targets, &count))
        return 0;
    group->targets = (F2cIoItem *)calloc(count, sizeof(*group->targets));
    if (group->targets == NULL) {
        free(targets);
        return 0;
    }
    for (index = 0U; index < count; ++index) {
        if (!f2c_statement_parse_io_item_tokens(unit, targets[index], &group->targets[index])) {
            free(targets);
            return 0;
        }
        ++group->target_count;
    }
    free(targets);
    return 1;
}

static int parse_value(Unit *unit, F2cTokenRange range, F2cDataValue *value) {
    const size_t separator =
        f2c_token_range_find_top_level(range, 0U, F2C_TOKEN_OPERATOR, "*");
    F2cTokenRange expression_range = range;
    memset(value, 0, sizeof(*value));
    value->span = range_span(range);
    value->repeat_count = 1U;
    if (separator != SIZE_MAX) {
        const F2cTokenRange repeat_range = f2c_token_range_slice(range, 0U, separator);
        expression_range = f2c_token_range_slice(range, separator + 1U, range.count);
        if (repeat_range.count == 0U || expression_range.count == 0U)
            return 0;
        value->repeat = parse_expression(unit, repeat_range);
        if (value->repeat == NULL)
            return 0;
    }
    value->text = f2c_token_range_text(expression_range);
    value->expression = parse_expression(unit, expression_range);
    return value->text != NULL && value->expression != NULL;
}

static int parse_values(Unit *unit, F2cTokenRange range, F2cDataGroup *group) {
    F2cTokenRange *values = NULL;
    size_t count = 0U;
    size_t index;
    if (!f2c_token_range_split_top_level(range, F2C_TOKEN_COMMA, NULL, &values, &count))
        return 0;
    group->values = (F2cDataValue *)calloc(count, sizeof(*group->values));
    if (group->values == NULL) {
        free(values);
        return 0;
    }
    for (index = 0U; index < count; ++index) {
        if (!parse_value(unit, values[index], &group->values[index])) {
            free(values);
            return 0;
        }
        ++group->value_count;
    }
    free(values);
    return 1;
}

static int append_group(F2cStatement *statement, F2cDataGroup *group) {
    F2cDataGroup *replacement;
    const size_t next = statement->data_group_count + 1U;
    if (next == 0U || next > SIZE_MAX / sizeof(*replacement))
        return 0;
    replacement =
        (F2cDataGroup *)realloc(statement->data_groups, next * sizeof(*replacement));
    if (replacement == NULL)
        return 0;
    statement->data_groups = replacement;
    statement->data_groups[statement->data_group_count++] = *group;
    memset(group, 0, sizeof(*group));
    return 1;
}

static int mark_invalid(F2cStatement *statement, F2cDataGroup *group) {
    f2c_statement_free_data_group(group);
    statement->data_syntax_valid = 0;
    return 1;
}

int f2c_statement_parse_data(Unit *unit, const Line *line, size_t body_start,
                             F2cStatement *statement) {
    size_t position;
    F2cDataGroup group = {0};
    if (line == NULL || statement == NULL)
        return 0;
    statement->data_syntax_valid = 1;
    position = body_start + 1U;
    if (body_start >= line->token_count || position >= line->token_count)
        return mark_invalid(statement, &group);
    while (position < line->token_count) {
        F2cTokenRange remainder = f2c_line_token_range(line, position, line->token_count);
        const size_t first =
            f2c_token_range_find_top_level(remainder, 0U, F2C_TOKEN_OPERATOR, "/");
        F2cTokenRange targets;
        F2cTokenRange value_tail;
        F2cTokenRange values;
        size_t second;
        size_t close;
        if (first == SIZE_MAX || first == 0U)
            return mark_invalid(statement, &group);
        targets = f2c_token_range_slice(remainder, 0U, first);
        value_tail = f2c_token_range_slice(remainder, first + 1U, remainder.count);
        second = f2c_token_range_find_top_level(value_tail, 0U, F2C_TOKEN_OPERATOR, "/");
        if (second == SIZE_MAX || second == 0U)
            return mark_invalid(statement, &group);
        values = f2c_token_range_slice(value_tail, 0U, second);
        close = position + first + 1U + second;
        group.span = f2c_source_span_cover(&line->tokens[position].span, &line->tokens[close].span);
        if (!parse_targets(unit, targets, &group) || !parse_values(unit, values, &group))
            return mark_invalid(statement, &group);
        if (!append_group(statement, &group)) {
            f2c_statement_free_data_group(&group);
            return 0;
        }
        position = close + 1U;
        if (position == line->token_count)
            break;
        if (line->tokens[position].kind != F2C_TOKEN_COMMA ||
            ++position == line->token_count)
            return mark_invalid(statement, &group);
    }
    return statement->data_group_count != 0U ? 1 : mark_invalid(statement, &group);
}
