#include "ast/statement/private.h"

#include "ast/internal.h"

#include <stdlib.h>
#include <string.h>

static F2cExpr *parse_expression_range(Unit *unit, const Line *line, size_t begin, size_t end) {
    if (unit == NULL || line == NULL || begin >= end || end > line->token_count)
        return NULL;
    return f2c_parse_expression_tokens(unit, line->tokens + begin, end - begin, line->text, NULL);
}

static int append_label(F2cStatement *statement, const F2cToken *token) {
    char **labels;
    F2cSourceSpan *spans;
    char *label = f2c_statement_copy_label_token(token);
    if (label == NULL)
        return 0;
    if (statement->label_count == SIZE_MAX / sizeof(*labels)) {
        free(label);
        return -1;
    }
    spans = (F2cSourceSpan *)realloc(statement->label_spans,
                                     (statement->label_count + 1U) * sizeof(*spans));
    if (spans == NULL) {
        free(label);
        return -1;
    }
    statement->label_spans = spans;
    labels = (char **)realloc(statement->labels, (statement->label_count + 1U) * sizeof(*labels));
    if (labels == NULL) {
        free(label);
        return -1;
    }
    statement->labels = labels;
    statement->label_spans[statement->label_count] = token->span;
    statement->labels[statement->label_count++] = label;
    return 1;
}

static int parse_label_list(F2cStatement *statement, const F2cToken *tokens, size_t count) {
    size_t index = 0U;
    if (count == 0U)
        return 0;
    while (index < count) {
        const int appended = append_label(statement, &tokens[index]);
        if (appended <= 0)
            return appended;
        ++index;
        if (index == count)
            return 1;
        if (tokens[index].kind != F2C_TOKEN_COMMA)
            return 0;
        ++index;
        if (index == count)
            return 0;
    }
    return 1;
}

static int parse_do(Unit *unit, const Line *line, size_t begin, F2cStatement *statement) {
    size_t index = begin + 1U;
    size_t equals;
    F2cTokenRange controls;
    F2cTokenRange bounds[4];
    size_t bound_count = 0U;
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_NUMBER) {
        statement->terminal_label = f2c_statement_copy_label_token(&line->tokens[index]);
        statement->terminal_label_span = line->tokens[index].span;
        if (statement->terminal_label == NULL) {
            statement->control_syntax_valid = 0;
            return 1;
        }
        ++index;
    }
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_COMMA)
        ++index;
    if (index == line->token_count)
        return 1;
    if (line->tokens[index].kind == F2C_TOKEN_IDENTIFIER &&
        f2c_token_equals(&line->tokens[index], "while")) {
        size_t open = index + 1U;
        size_t close;
        statement->kind = F2C_STMT_DO_WHILE;
        if (open >= line->token_count || line->tokens[open].kind != F2C_TOKEN_LEFT_PAREN ||
            !f2c_token_matching_delimiter(line->tokens, line->token_count, open, &close) ||
            close + 1U != line->token_count) {
            statement->control_syntax_valid = 0;
            return 1;
        }
        statement->expression = parse_expression_range(unit, line, open + 1U, close);
        if (statement->expression == NULL)
            statement->control_syntax_valid = 0;
        return 1;
    }
    controls = f2c_line_token_range(line, index, line->token_count);
    equals = f2c_token_range_find_top_level(controls, 0U, F2C_TOKEN_OPERATOR, "=");
    if (equals == SIZE_MAX || equals == 0U || equals + 1U == controls.count) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    statement->left = parse_expression_range(unit, line, index, index + equals);
    controls = f2c_token_range_slice(controls, equals + 1U, controls.count);
    if (!f2c_token_range_balanced(controls.tokens, controls.count)) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    {
        size_t bound_begin = 0U;
        while (bound_begin < controls.count && bound_count < 4U) {
            const F2cTokenRange tail = f2c_token_range_slice(controls, bound_begin, controls.count);
            const size_t comma = f2c_token_range_find_top_level(tail, 0U, F2C_TOKEN_COMMA, NULL);
            const size_t bound_end = comma == SIZE_MAX ? controls.count : bound_begin + comma;
            if (bound_end == bound_begin)
                break;
            bounds[bound_count++] = f2c_token_range_slice(controls, bound_begin, bound_end);
            if (comma == SIZE_MAX) {
                bound_begin = controls.count;
            } else {
                bound_begin = bound_end + 1U;
                if (bound_begin == controls.count)
                    bound_count = 4U;
            }
        }
    }
    if (bound_count != 2U && bound_count != 3U) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    statement->right =
        f2c_parse_expression_tokens(unit, bounds[0].tokens, bounds[0].count, line->text, NULL);
    statement->limit =
        f2c_parse_expression_tokens(unit, bounds[1].tokens, bounds[1].count, line->text, NULL);
    statement->step =
        bound_count == 3U
            ? f2c_parse_expression_tokens(unit, bounds[2].tokens, bounds[2].count, line->text, NULL)
            : f2c_expr_new(F2C_EXPR_INTEGER_LITERAL, TYPE_INTEGER, "1", 1U);
    if (statement->left == NULL || statement->right == NULL || statement->limit == NULL ||
        statement->step == NULL)
        statement->control_syntax_valid = 0;
    return 1;
}

static int parse_arithmetic_if(const Line *line, size_t close, F2cStatement *statement) {
    const int labels =
        parse_label_list(statement, line->tokens + close + 1U, line->token_count - close - 1U);
    statement->kind = F2C_STMT_ARITHMETIC_IF;
    if (labels < 0)
        return 0;
    if (labels == 0 || statement->label_count != 3U) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    return 1;
}

static int parse_if(Unit *unit, const Line *line, size_t begin, F2cStatement *statement) {
    const size_t open = begin + 1U;
    size_t close;
    F2cTokenRange tail;
    if (open >= line->token_count || line->tokens[open].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, open, &close)) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    statement->expression = parse_expression_range(unit, line, open + 1U, close);
    if (statement->expression == NULL)
        statement->control_syntax_valid = 0;
    tail = f2c_line_token_range(line, close + 1U, line->token_count);
    statement->tail = f2c_token_range_text(tail);
    if (statement->tail == NULL)
        return 0;
    if (tail.count == 1U && tail.tokens[0].kind == F2C_TOKEN_IDENTIFIER &&
        f2c_token_equals(&tail.tokens[0], "then")) {
        statement->block = 1;
        return 1;
    }
    if (tail.count != 0U && tail.tokens[0].kind == F2C_TOKEN_NUMBER)
        return parse_arithmetic_if(line, close, statement);
    if (tail.count == 0U) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    if (!f2c_statement_parse_nested_tokens(unit, line, close + 1U, &statement->nested))
        return 0;
    if (statement->nested == NULL)
        statement->control_syntax_valid = 0;
    return 1;
}

static int parse_assign(Unit *unit, const Line *line, size_t begin, F2cStatement *statement) {
    if (begin + 4U != line->token_count || line->tokens[begin + 1U].kind != F2C_TOKEN_NUMBER ||
        line->tokens[begin + 2U].kind != F2C_TOKEN_IDENTIFIER ||
        !f2c_token_equals(&line->tokens[begin + 2U], "to") ||
        line->tokens[begin + 3U].kind != F2C_TOKEN_IDENTIFIER) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    {
        const int appended = append_label(statement, &line->tokens[begin + 1U]);
        if (appended < 0)
            return 0;
        if (appended == 0) {
            statement->control_syntax_valid = 0;
            return 1;
        }
    }
    statement->name = f2c_token_text(&line->tokens[begin + 3U]);
    statement->expression = parse_expression_range(unit, line, begin + 3U, begin + 4U);
    if (statement->name == NULL || statement->expression == NULL)
        statement->control_syntax_valid = 0;
    return 1;
}

static int parse_goto(Unit *unit, const Line *line, size_t begin, F2cStatement *statement) {
    size_t index = begin + 1U;
    if (f2c_token_equals(&line->tokens[begin], "go"))
        index = begin + 2U;
    if (index >= line->token_count) {
        statement->control_syntax_valid = 0;
        return 1;
    }
    if (line->tokens[index].kind == F2C_TOKEN_NUMBER) {
        if (index + 1U != line->token_count) {
            statement->control_syntax_valid = 0;
            return 1;
        }
        statement->name = f2c_statement_copy_label_token(&line->tokens[index]);
        statement->label_span = line->tokens[index].span;
        if (statement->name == NULL)
            statement->control_syntax_valid = 0;
        return 1;
    }
    if (line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN) {
        size_t close;
        int labels;
        if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close)) {
            statement->control_syntax_valid = 0;
            return 1;
        }
        labels = parse_label_list(statement, line->tokens + index + 1U, close - index - 1U);
        if (labels < 0)
            return 0;
        index = close + 1U;
        if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_COMMA)
            ++index;
        if (labels == 0 || index == line->token_count) {
            statement->control_syntax_valid = 0;
            return 1;
        }
        statement->expression = parse_expression_range(unit, line, index, line->token_count);
        if (statement->expression == NULL)
            statement->control_syntax_valid = 0;
        return 1;
    }
    if (line->tokens[index].kind == F2C_TOKEN_IDENTIFIER) {
        int labels = 1;
        statement->kind = F2C_STMT_ASSIGNED_GOTO;
        statement->name = f2c_token_text(&line->tokens[index]);
        statement->expression = parse_expression_range(unit, line, index, index + 1U);
        ++index;
        if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_COMMA)
            ++index;
        if (index < line->token_count) {
            size_t close;
            if (line->tokens[index].kind != F2C_TOKEN_LEFT_PAREN ||
                !f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close) ||
                close + 1U != line->token_count) {
                statement->control_syntax_valid = 0;
                return 1;
            }
            labels = parse_label_list(statement, line->tokens + index + 1U, close - index - 1U);
            if (labels < 0)
                return 0;
        }
        if (labels == 0 || statement->name == NULL || statement->expression == NULL)
            statement->control_syntax_valid = 0;
        return 1;
    }
    statement->control_syntax_valid = 0;
    return 1;
}

int f2c_statement_parse_control(Unit *unit, const Line *line, size_t body_start,
                                F2cStatement *statement) {
    if (unit == NULL || line == NULL || statement == NULL || body_start >= line->token_count)
        return 0;
    switch (statement->kind) {
    case F2C_STMT_IF:
        return parse_if(unit, line, body_start, statement);
    case F2C_STMT_DO:
        return parse_do(unit, line, body_start, statement);
    case F2C_STMT_ASSIGN_LABEL:
        return parse_assign(unit, line, body_start, statement);
    case F2C_STMT_GOTO:
        return parse_goto(unit, line, body_start, statement);
    default:
        return 1;
    }
}
