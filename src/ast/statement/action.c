#include "ast/statement/private.h"

#include "ast/internal.h"

#include <stdlib.h>
#include <string.h>

static F2cSourceSpan range_span(F2cTokenRange range) {
    F2cSourceSpan span = {0};
    if (range.count != 0U) {
        span = f2c_source_span_cover(&range.tokens[0].span, &range.tokens[range.count - 1U].span);
    }
    return span;
}

static F2cExpr *parse_range(Unit *unit, F2cTokenRange range) {
    return range.count != 0U
               ? f2c_parse_expression_tokens(unit, range.tokens, range.count, range.source, NULL)
               : NULL;
}

static F2cExpr *parse_actual(Unit *unit, F2cTokenRange range) {
    const size_t equals = f2c_token_range_find_top_level(range, 0U, F2C_TOKEN_OPERATOR, "=");
    F2cExpr *value;
    F2cExpr *argument;
    if (equals == SIZE_MAX)
        return parse_range(unit, range);
    if (equals != 1U || range.tokens[0].kind != F2C_TOKEN_IDENTIFIER || equals + 1U == range.count)
        return parse_range(unit, range);
    value = parse_range(unit, f2c_token_range_slice(range, equals + 1U, range.count));
    argument = f2c_expr_new(F2C_EXPR_KEYWORD_ARGUMENT, value != NULL ? value->type : TYPE_UNKNOWN,
                            range.tokens[0].begin, range.tokens[0].length);
    if (value == NULL || argument == NULL || !f2c_expr_push(argument, value)) {
        f2c_expr_free(value);
        f2c_expr_free(argument);
        return NULL;
    }
    argument->rank = value->rank;
    argument->definable = value->definable;
    argument->type_kind = value->type_kind;
    argument->value_category = value->value_category;
    argument->shape = value->shape;
    argument->span = range_span(range);
    argument->source = f2c_token_range_text(range);
    if (argument->source == NULL) {
        f2c_expr_free(argument);
        return NULL;
    }
    if (unit->context != NULL) {
        if (unit->context->limits.max_ast_nodes != 0U &&
            unit->context->ast_node_count >= unit->context->limits.max_ast_nodes) {
            f2c_expr_free(argument);
            return NULL;
        }
        ++unit->context->ast_node_count;
    }
    return argument;
}

static int top_level_parts(F2cTokenRange range, int preserve_empty, F2cTokenRange **parts,
                           size_t *part_count) {
    size_t count = 1U;
    size_t begin = 0U;
    size_t index;
    size_t output = 0U;
    F2cTokenRange *result;
    *parts = NULL;
    *part_count = 0U;
    if (!f2c_token_range_balanced(range.tokens, range.count))
        return 0;
    if (range.count == 0U)
        return preserve_empty;
    for (index = 0U; index < range.count; ++index) {
        size_t close;
        if (range.tokens[index].kind == F2C_TOKEN_LEFT_PAREN ||
            range.tokens[index].kind == F2C_TOKEN_LEFT_BRACKET ||
            range.tokens[index].kind == F2C_TOKEN_ARRAY_BEGIN) {
            if (!f2c_token_matching_delimiter(range.tokens, range.count, index, &close))
                return 0;
            index = close;
        } else if (range.tokens[index].kind == F2C_TOKEN_COMMA) {
            ++count;
        }
    }
    if (count > SIZE_MAX / sizeof(*result))
        return 0;
    result = (F2cTokenRange *)calloc(count, sizeof(*result));
    if (result == NULL)
        return 0;
    for (index = 0U; index <= range.count; ++index) {
        size_t close;
        const int at_end = index == range.count;
        if (!at_end && (range.tokens[index].kind == F2C_TOKEN_LEFT_PAREN ||
                        range.tokens[index].kind == F2C_TOKEN_LEFT_BRACKET ||
                        range.tokens[index].kind == F2C_TOKEN_ARRAY_BEGIN)) {
            if (!f2c_token_matching_delimiter(range.tokens, range.count, index, &close)) {
                free(result);
                return 0;
            }
            index = close;
            continue;
        }
        if (at_end || range.tokens[index].kind == F2C_TOKEN_COMMA) {
            if (!preserve_empty && begin == index) {
                free(result);
                return 0;
            }
            result[output++] = f2c_token_range_slice(range, begin, index);
            begin = index + 1U;
        }
    }
    *parts = result;
    *part_count = output;
    return 1;
}

static int parse_argument_list(Unit *unit, F2cTokenRange range, int preserve_empty,
                               F2cStatement *statement) {
    F2cTokenRange *parts = NULL;
    size_t count = 0U;
    size_t index;
    if (range.count == 0U)
        return 1;
    if (!top_level_parts(range, preserve_empty, &parts, &count) || count == 0U ||
        count > SIZE_MAX / sizeof(*statement->items) ||
        count > SIZE_MAX / sizeof(*statement->arguments)) {
        free(parts);
        return 0;
    }
    statement->items = (char **)calloc(count, sizeof(*statement->items));
    statement->arguments = (F2cExpr **)calloc(count, sizeof(*statement->arguments));
    if (statement->items == NULL || statement->arguments == NULL) {
        free(parts);
        return 0;
    }
    statement->item_count = count;
    for (index = 0U; index < count; ++index) {
        statement->items[index] = f2c_token_range_text(parts[index]);
        if (parts[index].count == 0U) {
            statement->arguments[index] = f2c_expr_new_absent(TYPE_UNKNOWN, 0U);
            if (statement->arguments[index] != NULL)
                statement->arguments[index]->source = f2c_strdup("");
        } else {
            statement->arguments[index] = parse_actual(unit, parts[index]);
        }
        if (statement->items[index] == NULL || statement->arguments[index] == NULL) {
            free(parts);
            return 0;
        }
    }
    free(parts);
    return 1;
}

static size_t final_argument_open(const Line *line, size_t begin) {
    size_t index;
    if (begin >= line->token_count ||
        line->tokens[line->token_count - 1U].kind != F2C_TOKEN_RIGHT_PAREN)
        return SIZE_MAX;
    for (index = begin; index + 1U < line->token_count; ++index) {
        size_t close;
        if (line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN &&
            f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close) &&
            close + 1U == line->token_count)
            return index;
    }
    return SIZE_MAX;
}

static int parse_call(Unit *unit, const Line *line, size_t begin, F2cStatement *statement) {
    const size_t designator_begin = begin + 1U;
    const size_t open = final_argument_open(line, designator_begin);
    const size_t designator_end = open == SIZE_MAX ? line->token_count : open;
    F2cTokenRange designator;
    if (designator_begin >= designator_end)
        return 0;
    designator = f2c_line_token_range(line, designator_begin, designator_end);
    if (designator.count == 1U && designator.tokens[0].kind == F2C_TOKEN_IDENTIFIER) {
        statement->name = f2c_token_text(&designator.tokens[0]);
        if (statement->name == NULL)
            return 0;
        if (f2c_token_equals(&designator.tokens[0], "move_alloc"))
            statement->kind = F2C_STMT_MOVE_ALLOC;
    } else {
        statement->expression = parse_range(unit, designator);
        if (statement->expression == NULL)
            return 0;
    }
    if (open != SIZE_MAX &&
        !parse_argument_list(unit, f2c_line_token_range(line, open + 1U, line->token_count - 1U), 1,
                             statement))
        return 0;
    return 1;
}

static F2cExpr *default_character_length(void) {
    return f2c_expr_new(F2C_EXPR_INTEGER_LITERAL, TYPE_INTEGER, "1", 1U);
}

static F2cExpr *parse_character_length(Unit *unit, F2cTokenRange specification) {
    F2cTokenRange selector;
    F2cTokenRange *parts = NULL;
    size_t count = 0U;
    size_t close;
    size_t index;
    F2cExpr *length = NULL;
    if (specification.count == 1U)
        return default_character_length();
    if (specification.count < 3U || specification.tokens[1].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(specification.tokens, specification.count, 1U, &close) ||
        close + 1U != specification.count)
        return NULL;
    selector = f2c_token_range_slice(specification, 2U, close);
    if (!top_level_parts(selector, 0, &parts, &count))
        return NULL;
    for (index = 0U; index < count; ++index) {
        F2cTokenRange value = parts[index];
        size_t equals = f2c_token_range_find_top_level(value, 0U, F2C_TOKEN_OPERATOR, "=");
        if (equals != SIZE_MAX) {
            if (equals != 1U || value.tokens[0].kind != F2C_TOKEN_IDENTIFIER ||
                equals + 1U == value.count) {
                length = NULL;
                break;
            }
            if (!f2c_token_equals(&value.tokens[0], "len"))
                continue;
            value = f2c_token_range_slice(value, equals + 1U, value.count);
        } else if (index != 0U) {
            continue;
        }
        if (value.count == 1U && ((value.tokens[0].kind == F2C_TOKEN_OPERATOR &&
                                   f2c_token_equals(&value.tokens[0], "*")) ||
                                  value.tokens[0].kind == F2C_TOKEN_COLON)) {
            length = NULL;
            break;
        }
        length = parse_range(unit, value);
        break;
    }
    free(parts);
    return length != NULL ? length : (count != 0U ? NULL : default_character_length());
}

static Type allocation_type(F2cTokenRange specification) {
    if (specification.count == 0U || specification.tokens[0].kind != F2C_TOKEN_IDENTIFIER)
        return TYPE_UNKNOWN;
    if (f2c_token_equals(&specification.tokens[0], "character"))
        return TYPE_CHARACTER;
    if (f2c_token_equals(&specification.tokens[0], "integer"))
        return TYPE_INTEGER;
    if (f2c_token_equals(&specification.tokens[0], "real"))
        return TYPE_REAL;
    if (f2c_token_equals(&specification.tokens[0], "complex"))
        return TYPE_COMPLEX;
    if (f2c_token_equals(&specification.tokens[0], "logical"))
        return TYPE_LOGICAL;
    if (specification.count >= 2U && f2c_token_equals(&specification.tokens[0], "double") &&
        f2c_token_equals(&specification.tokens[1], "precision"))
        return TYPE_DOUBLE;
    return TYPE_UNKNOWN;
}

static int parse_allocation(Unit *unit, const Line *line, size_t begin, F2cStatement *statement) {
    size_t close;
    F2cTokenRange inside;
    F2cTokenRange entities;
    size_t separator;
    if (begin + 1U >= line->token_count || line->tokens[begin + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, begin + 1U, &close) ||
        close + 1U != line->token_count)
        return 0;
    inside = f2c_line_token_range(line, begin + 2U, close);
    if (inside.count == 0U)
        return 0;
    separator = f2c_token_range_find_top_level(inside, 0U, F2C_TOKEN_DOUBLE_COLON, NULL);
    entities = inside;
    if (separator != SIZE_MAX) {
        F2cTokenRange specification;
        if (statement->kind != F2C_STMT_ALLOCATE || separator == 0U ||
            separator + 1U == inside.count)
            return 0;
        specification = f2c_token_range_slice(inside, 0U, separator);
        statement->allocation_has_type_spec = 1;
        statement->allocation_type = allocation_type(specification);
        statement->allocation_type_span = range_span(specification);
        statement->tail = f2c_token_range_text(specification);
        if (statement->tail == NULL)
            return 0;
        if (statement->allocation_type == TYPE_CHARACTER)
            statement->allocation_character_length = parse_character_length(unit, specification);
        entities = f2c_token_range_slice(inside, separator + 1U, inside.count);
    }
    return parse_argument_list(unit, entities, 0, statement);
}

static int parse_stop(Unit *unit, const Line *line, size_t begin, F2cStatement *statement) {
    size_t code_begin = begin + 1U;
    if (f2c_token_equals(&line->tokens[begin], "error")) {
        statement->error_stop = 1;
        code_begin = begin + 2U;
    }
    if (code_begin < line->token_count) {
        statement->expression =
            parse_range(unit, f2c_line_token_range(line, code_begin, line->token_count));
        if (statement->expression == NULL)
            return 0;
    }
    return 1;
}

int f2c_statement_parse_action(Unit *unit, const Line *line, size_t body_start,
                               F2cStatement *statement) {
    if (unit == NULL || line == NULL || statement == NULL || body_start >= line->token_count)
        return 0;
    statement->action_syntax_valid = 1;
    switch (statement->kind) {
    case F2C_STMT_CALL:
        statement->action_syntax_valid = parse_call(unit, line, body_start, statement);
        break;
    case F2C_STMT_ALLOCATE:
    case F2C_STMT_DEALLOCATE:
    case F2C_STMT_NULLIFY:
        statement->action_syntax_valid = parse_allocation(unit, line, body_start, statement);
        break;
    case F2C_STMT_STOP:
        statement->action_syntax_valid = parse_stop(unit, line, body_start, statement);
        break;
    case F2C_STMT_RETURN:
        if (body_start + 1U < line->token_count) {
            statement->expression =
                parse_range(unit, f2c_line_token_range(line, body_start + 1U, line->token_count));
            statement->action_syntax_valid = statement->expression != NULL;
        }
        break;
    default:
        break;
    }
    return 1;
}
