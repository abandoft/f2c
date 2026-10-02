#include "ast/internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static F2cExpr *parse_binary_impl(AstParser *parser, int minimum_precedence);

static size_t numeric_spelling_length(const F2cToken *token) {
    const char *suffix;
    if (token == NULL || token->begin == NULL)
        return 0U;
    suffix = (const char *)memchr(token->begin, '_', token->length);
    return suffix != NULL ? (size_t)(suffix - token->begin) : token->length;
}

static int numeric_spelling_contains(const F2cToken *token, char lower, char upper) {
    const size_t length = numeric_spelling_length(token);
    return memchr(token->begin, lower, length) != NULL ||
           memchr(token->begin, upper, length) != NULL;
}

static int push_expression(AstParser *parser, F2cExpr *parent, F2cExpr *child) {
    return f2c_ast_push_expression(parser, parent, child);
}

static F2cSourceSpan expression_range_span(const AstParser *parser, const char *begin,
                                           const char *end) {
    F2cSourceSpan span = {0};
    size_t first = 0U;
    size_t past = 0U;
    size_t low = 0U;
    size_t high = parser->token_count;
    if (parser->tokens == NULL || parser->token_count == 0U) {
        span.begin.line = 1U;
        span.begin.column = (size_t)(begin - parser->source) + 1U;
        span.end.line = 1U;
        span.end.column = (size_t)(end - parser->source) + 1U;
        return span;
    }
    /* Canonical tokens are ordered in the normalized source buffer. Binary
     * searches retain physical/macro locations without rescanning or quadratic
     * work for deeply nested expression trees. */
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        const F2cToken *token = &parser->tokens[middle];
        if (token->begin + token->length <= begin)
            low = middle + 1U;
        else
            high = middle;
    }
    first = low;
    high = parser->token_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2U;
        if (parser->tokens[middle].begin < end)
            low = middle + 1U;
        else
            high = middle;
    }
    past = low;
    if (first < past)
        span = f2c_source_span_cover(&parser->tokens[first].span, &parser->tokens[past - 1U].span);
    return span;
}

void f2c_ast_set_expression_range(const AstParser *parser, F2cExpr *expression, const char *begin,
                                  const char *end) {
    if (parser == NULL || expression == NULL || parser->source == NULL || begin == NULL ||
        end == NULL || begin < parser->source || end < begin)
        return;
    while (begin < end && isspace((unsigned char)*begin))
        ++begin;
    while (end > begin && isspace((unsigned char)end[-1]))
        --end;
    expression->source_offset = (size_t)(begin - parser->source);
    expression->source_length = (size_t)(end - begin);
    expression->span = expression_range_span(parser, begin, end);
}

static void set_combined_expression_range(F2cExpr *expression, const F2cExpr *left,
                                          const F2cExpr *right) {
    size_t end;
    if (expression == NULL || left == NULL || right == NULL || left->source_offset == SIZE_MAX ||
        right->source_offset == SIZE_MAX)
        return;
    end = right->source_offset + right->source_length;
    if (end < right->source_offset || end < left->source_offset)
        return;
    expression->source_offset = left->source_offset;
    expression->source_length = end - left->source_offset;
    expression->span = f2c_source_span_cover(&left->span, &right->span);
}

static int token_begins_implied_do_control(const AstParser *parser) {
    AstParser probe;
    if (parser->token.kind != F2C_TOKEN_IDENTIFIER)
        return 0;
    probe = *parser;
    f2c_ast_next_token(&probe);
    return probe.token.kind == F2C_TOKEN_OPERATOR && f2c_token_equals(&probe.token, "=");
}

static int push_parenthesized_value(F2cExpr ***values, size_t *count, size_t *capacity,
                                    F2cExpr *value) {
    F2cExpr **replacement;
    if (*count == *capacity) {
        const size_t next = *capacity == 0U ? 2U : *capacity * 2U;
        replacement = (F2cExpr **)realloc(*values, next * sizeof(**values));
        if (replacement == NULL)
            return 0;
        *values = replacement;
        *capacity = next;
    }
    (*values)[(*count)++] = value;
    return 1;
}

static void free_parenthesized_values(F2cExpr **values, size_t count) {
    while (count != 0U)
        f2c_expr_free(values[--count]);
    free(values);
}

static F2cExpr *parse_implied_do(AstParser *parser, F2cExpr **values, size_t value_count) {
    const F2cToken iterator_token = parser->token;
    F2cExpr *initial = NULL;
    F2cExpr *limit = NULL;
    F2cExpr *step = NULL;
    F2cExpr *expression = NULL;
    char *iterator_name = NULL;
    size_t i;

    iterator_name = f2c_strdup_n(iterator_token.begin, iterator_token.length);
    if (iterator_name == NULL)
        goto failed;
    expression = f2c_expr_new(F2C_EXPR_IMPLIED_DO, TYPE_UNKNOWN, iterator_token.begin,
                              iterator_token.length);
    if (expression == NULL)
        goto failed;
    expression->symbol = f2c_find_symbol(parser->unit, iterator_name);
    free(iterator_name);
    iterator_name = NULL;

    f2c_ast_next_token(parser);
    if (parser->token.kind != F2C_TOKEN_OPERATOR || !f2c_token_equals(&parser->token, "=")) {
        f2c_ast_parser_error(parser, parser->token.begin);
        goto failed;
    }
    f2c_ast_next_token(parser);
    initial = f2c_ast_parse_binary(parser, 1);
    if (initial == NULL || parser->token.kind != F2C_TOKEN_COMMA) {
        f2c_ast_parser_error(parser, parser->token.begin);
        goto failed;
    }
    f2c_ast_next_token(parser);
    limit = f2c_ast_parse_binary(parser, 1);
    if (limit == NULL)
        goto failed;
    if (parser->token.kind == F2C_TOKEN_COMMA) {
        f2c_ast_next_token(parser);
        step = f2c_ast_parse_binary(parser, 1);
    } else {
        step = f2c_expr_new(F2C_EXPR_INTEGER_LITERAL, TYPE_INTEGER, "1", 1U);
    }
    if (step == NULL)
        goto failed;
    for (i = 0U; i < value_count; ++i) {
        expression->type = f2c_ast_common_constructor_type(expression->type, values[i]->type);
        if (values[i]->type == TYPE_DERIVED && expression->derived_type == NULL)
            expression->derived_type = values[i]->derived_type;
        if (!push_expression(parser, expression, values[i]))
            goto failed;
        values[i] = NULL;
    }
    if (!push_expression(parser, expression, initial))
        goto failed;
    initial = NULL;
    if (!push_expression(parser, expression, limit))
        goto failed;
    limit = NULL;
    if (!push_expression(parser, expression, step))
        goto failed;
    step = NULL;
    expression->rank = 1U;
    f2c_ast_set_expression_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
    return expression;

failed:
    free(iterator_name);
    f2c_expr_free(initial);
    f2c_expr_free(limit);
    f2c_expr_free(step);
    f2c_expr_free(expression);
    return NULL;
}

static F2cExpr *parse_parenthesized_expression(AstParser *parser) {
    F2cExpr **values = NULL;
    size_t value_count = 0U;
    size_t value_capacity = 0U;
    F2cExpr *result = NULL;

    f2c_ast_next_token(parser);
    for (;;) {
        F2cExpr *value = f2c_ast_parse_binary(parser, 1);
        if (value == NULL ||
            !push_parenthesized_value(&values, &value_count, &value_capacity, value)) {
            f2c_expr_free(value);
            goto failed;
        }
        if (parser->token.kind != F2C_TOKEN_COMMA)
            break;
        f2c_ast_next_token(parser);
        if (token_begins_implied_do_control(parser)) {
            result = parse_implied_do(parser, values, value_count);
            if (result == NULL)
                goto failed;
            break;
        }
    }
    if (parser->token.kind != F2C_TOKEN_RIGHT_PAREN) {
        f2c_ast_parser_error(parser, parser->token.begin);
        goto failed;
    }
    f2c_ast_next_token(parser);
    if (result != NULL) {
        free(values);
        return result;
    }
    if (value_count == 1U) {
        result = f2c_expr_new(F2C_EXPR_PARENTHESIZED, values[0]->type, NULL, 0U);
        if (result == NULL || !push_expression(parser, result, values[0]))
            goto failed;
        values[0] = NULL;
        f2c_expr_refresh_parenthesized(result);
    } else if (value_count == 2U) {
        const Type common = f2c_common_numeric_type(values[0]->type, values[1]->type);
        result = f2c_expr_new(F2C_EXPR_COMPLEX_LITERAL,
                              common == TYPE_DOUBLE ? TYPE_DOUBLE_COMPLEX : TYPE_COMPLEX, NULL, 0U);
        if (result == NULL || !push_expression(parser, result, values[0]))
            goto failed;
        values[0] = NULL;
        if (!push_expression(parser, result, values[1]))
            goto failed;
        values[1] = NULL;
        result->type_kind =
            f2c_ast_common_expression_kind(result->type, result->children[0], result->children[1]);
    } else {
        f2c_ast_parser_error(parser, parser->token.begin);
        goto failed;
    }
    free(values);
    return result;

failed:
    f2c_expr_free(result);
    free_parenthesized_values(values, value_count);
    return NULL;
}

static F2cExpr *parse_primary(AstParser *parser) {
    F2cToken token = parser->token;
    F2cExpr *expression = NULL;
    if (token.kind == F2C_TOKEN_NUMBER) {
        const int double_exponent = numeric_spelling_contains(&token, 'd', 'D');
        const int real = numeric_spelling_contains(&token, '.', '.') ||
                         numeric_spelling_contains(&token, 'e', 'E') || double_exponent ||
                         numeric_spelling_contains(&token, 'q', 'Q');
        const Type literal_type =
            real && double_exponent ? TYPE_DOUBLE : (real ? TYPE_REAL : TYPE_INTEGER);
        const int literal_kind = f2c_ast_literal_kind_value(parser, &token, literal_type);
        const int double_precision = real && literal_kind == f2c_default_kind(TYPE_DOUBLE);
        expression =
            f2c_expr_new(real ? F2C_EXPR_REAL_LITERAL : F2C_EXPR_INTEGER_LITERAL,
                         real ? (double_precision ? TYPE_DOUBLE : TYPE_REAL) : TYPE_INTEGER,
                         token.begin, token.length);
        if (expression != NULL)
            expression->type_kind = literal_kind;
        f2c_ast_next_token(parser);
    } else if (token.kind == F2C_TOKEN_STRING || token.kind == F2C_TOKEN_HOLLERITH) {
        expression =
            f2c_expr_new(F2C_EXPR_STRING_LITERAL, TYPE_CHARACTER, token.begin, token.length);
        if (expression != NULL)
            expression->type_kind = f2c_ast_literal_kind_value(parser, &token, TYPE_CHARACTER);
        f2c_ast_next_token(parser);
    } else if (token.kind == F2C_TOKEN_BOZ) {
        expression =
            f2c_expr_new(F2C_EXPR_INTEGER_LITERAL, TYPE_INTEGER, token.begin, token.length);
        f2c_ast_next_token(parser);
    } else if (token.kind == F2C_TOKEN_IDENTIFIER) {
        f2c_ast_next_token(parser);
        expression = f2c_ast_parse_name(parser, &token);
    } else if (token.kind == F2C_TOKEN_OPERATOR &&
               (f2c_token_equals(&token, ".true.") || f2c_token_equals(&token, ".false."))) {
        expression =
            f2c_expr_new(F2C_EXPR_LOGICAL_LITERAL, TYPE_LOGICAL, token.begin, token.length);
        f2c_ast_next_token(parser);
    } else if (token.kind == F2C_TOKEN_OPERATOR &&
               (f2c_token_equals(&token, "+") || f2c_token_equals(&token, "-") ||
                f2c_token_equals(&token, ".not.") || f2c_ast_is_defined_operator(&token))) {
        F2cExpr *operand;
        const int defined = f2c_ast_is_defined_operator(&token);
        f2c_ast_next_token(parser);
        operand = defined ? parse_primary(parser)
                          : (f2c_token_equals(&token, ".not.") ? f2c_ast_parse_binary(parser, 4)
                                                               : f2c_ast_parse_binary(parser, 7));
        expression =
            f2c_expr_new(F2C_EXPR_UNARY,
                         defined ? TYPE_UNKNOWN
                                 : (f2c_token_equals(&token, ".not.")
                                        ? TYPE_LOGICAL
                                        : (operand != NULL ? operand->type : TYPE_UNKNOWN)),
                         token.begin, token.length);
        if (expression == NULL || operand == NULL ||
            !push_expression(parser, expression, operand)) {
            f2c_expr_free(operand);
            f2c_expr_free(expression);
            return NULL;
        }
        expression->type_kind =
            defined ? f2c_default_kind(TYPE_UNKNOWN)
                    : (f2c_token_equals(&token, ".not.") ? f2c_default_kind(TYPE_LOGICAL)
                                                         : operand->type_kind);
        f2c_ast_copy_expression_shape(expression, &operand->shape);
    } else if (token.kind == F2C_TOKEN_ARRAY_BEGIN) {
        Type element_type = TYPE_UNKNOWN;
        F2cDerivedType *element_derived_type = NULL;
        expression = f2c_expr_new(F2C_EXPR_ARRAY_CONSTRUCTOR, TYPE_UNKNOWN, NULL, 0U);
        f2c_ast_next_token(parser);
        while (expression != NULL && parser->token.kind != F2C_TOKEN_ARRAY_END &&
               parser->token.kind != F2C_TOKEN_END) {
            F2cExpr *element = f2c_ast_parse_binary(parser, 1);
            if (element == NULL || !push_expression(parser, expression, element)) {
                f2c_expr_free(element);
                f2c_expr_free(expression);
                return NULL;
            }
            element_type = f2c_ast_common_constructor_type(element_type, element->type);
            if (element->type == TYPE_DERIVED && element_derived_type == NULL)
                element_derived_type = element->derived_type;
            if (parser->token.kind == F2C_TOKEN_COMMA)
                f2c_ast_next_token(parser);
            else
                break;
        }
        if (parser->token.kind != F2C_TOKEN_ARRAY_END)
            f2c_ast_parser_error(parser, parser->token.begin);
        else
            f2c_ast_next_token(parser);
        if (expression != NULL) {
            uint64_t constructor_extent;
            int extent_known;
            expression->type = element_type;
            expression->derived_type = element_derived_type;
            expression->type_kind = 0;
            for (size_t element_index = 0U; element_index < expression->child_count;
                 ++element_index) {
                if (expression->children[element_index]->type == element_type &&
                    expression->type_kind == 0)
                    expression->type_kind = expression->children[element_index]->type_kind;
            }
            if (expression->type_kind == 0)
                expression->type_kind = f2c_default_kind(element_type);
            f2c_ast_set_expression_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
            expression->shape.dimensions[0].kind = F2C_DIMENSION_EXPLICIT;
            expression->shape.dimensions[0].lower_known = 1;
            expression->shape.dimensions[0].lower = 1;
            extent_known =
                f2c_ast_constructor_extent(parser->unit, expression, &constructor_extent);
            expression->shape.dimensions[0].extent_known = extent_known > 0;
            expression->shape.dimensions[0].extent = extent_known > 0 ? constructor_extent : 0U;
        }
    } else if (token.kind == F2C_TOKEN_LEFT_PAREN) {
        expression = parse_parenthesized_expression(parser);
    } else {
        f2c_ast_parser_error(parser, token.begin);
        expression = f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, token.begin, token.length);
        if (token.kind != F2C_TOKEN_END)
            f2c_ast_next_token(parser);
    }
    f2c_ast_set_expression_range(parser, expression, token.begin, parser->token.begin);
    expression = f2c_ast_parse_designator(parser, expression);
    f2c_ast_set_expression_range(parser, expression, token.begin, parser->token.begin);
    return expression;
}

F2cExpr *f2c_ast_parse_binary(AstParser *parser, int minimum_precedence) {
    F2cExpr *result;
    Context *context = parser->unit != NULL ? parser->unit->context : NULL;
    if (context != NULL && context->limits.max_parse_depth != 0U &&
        parser->depth >= context->limits.max_parse_depth) {
        parser->depth_limit_exceeded = 1;
        f2c_ast_parser_error(parser, parser->token.begin);
        return NULL;
    }
    ++parser->depth;
    result = parse_binary_impl(parser, minimum_precedence);
    --parser->depth;
    return result;
}

static F2cExpr *parse_binary_impl(AstParser *parser, int minimum_precedence) {
    F2cExpr *left = parse_primary(parser);
    while (left != NULL && parser->token.kind == F2C_TOKEN_OPERATOR) {
        const F2cToken operator_token = parser->token;
        const int operator_precedence = f2c_ast_precedence(&operator_token);
        F2cExpr *right;
        F2cExpr *binary;
        Type type;
        if (operator_precedence < minimum_precedence)
            break;
        f2c_ast_next_token(parser);
        right = f2c_ast_parse_binary(parser, operator_precedence +
                                                 (f2c_token_equals(&operator_token, "**") ? 0 : 1));
        if (right == NULL) {
            f2c_expr_free(left);
            return NULL;
        }
        if (f2c_ast_is_defined_operator(&operator_token)) {
            type = TYPE_UNKNOWN;
        } else if (f2c_ast_is_comparison(&operator_token)) {
            type = TYPE_LOGICAL;
        } else if (f2c_token_equals(&operator_token, ".and.") ||
                   f2c_token_equals(&operator_token, ".or.") ||
                   f2c_token_equals(&operator_token, ".eqv.") ||
                   f2c_token_equals(&operator_token, ".neqv.")) {
            type = TYPE_LOGICAL;
        } else if (f2c_token_equals(&operator_token, "//")) {
            type = TYPE_CHARACTER;
        } else if (f2c_token_equals(&operator_token, "**")) {
            type = left->type;
        } else {
            type = f2c_common_numeric_type(left->type, right->type);
        }
        binary = f2c_expr_new(F2C_EXPR_BINARY, type, operator_token.begin, operator_token.length);
        if (binary == NULL) {
            f2c_expr_free(left);
            f2c_expr_free(right);
            return NULL;
        }
        if (!push_expression(parser, binary, left)) {
            f2c_expr_free(left);
            f2c_expr_free(right);
            f2c_expr_free(binary);
            return NULL;
        }
        left = NULL;
        if (!push_expression(parser, binary, right)) {
            f2c_expr_free(right);
            f2c_expr_free(binary);
            return NULL;
        }
        binary->type_kind =
            f2c_ast_common_expression_kind(type, binary->children[0], binary->children[1]);
        f2c_ast_set_elemental_shape(binary, binary->children[0], binary->children[1]);
        set_combined_expression_range(binary, binary->children[0], binary->children[1]);
        left = binary;
    }
    return left;
}

static int materialize_expression_sources(F2cExpr *expression, const char *source,
                                          size_t source_length, int root) {
    size_t i;
    if (expression == NULL)
        return 1;
    for (i = 0U; i < expression->child_count; ++i) {
        if (!materialize_expression_sources(expression->children[i], source, source_length, 0))
            return 0;
    }
    free(expression->source);
    expression->source = NULL;
    if (root) {
        expression->source = f2c_strdup_n(source, source_length);
        expression->source_offset = 0U;
        expression->source_length = source_length;
    } else if (expression->source_offset != SIZE_MAX &&
               expression->source_offset <= source_length &&
               expression->source_length <= source_length - expression->source_offset) {
        expression->source =
            f2c_strdup_n(source + expression->source_offset, expression->source_length);
    }
    return expression->source != NULL || (!root && expression->source_offset == SIZE_MAX);
}

static F2cExpr *parse_expression_stream(AstParser *parser, const char *source, size_t source_length,
                                        const char **error_at) {
    F2cExpr *result;
    f2c_ast_next_token(parser);
    result = f2c_ast_parse_binary(parser, 1);
    if (parser->depth_limit_exceeded && parser->unit != NULL && parser->unit->context != NULL)
        f2c_ast_report_resource_limit(parser, "expression parse-depth limit of %zu exceeded",
                                      parser->unit->context->limits.max_parse_depth,
                                      &parser->unit->context->parse_depth_limit_reported);
    if (parser->token.kind != F2C_TOKEN_END)
        f2c_ast_parser_error(parser, parser->token.begin);
    if (result == NULL && parser->error_at != NULL)
        result = f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, parser->error_at, 0U);
    if (result != NULL) {
        if (!materialize_expression_sources(result, source, source_length, 1)) {
            f2c_expr_free(result);
            result = NULL;
        } else if (parser->error_at != NULL) {
            result->parse_error_offset =
                parser->error_at >= source && parser->error_at <= source + source_length
                    ? (size_t)(parser->error_at - source)
                    : source_length;
            if (parser->token_count != 0U) {
                size_t token_index;
                for (token_index = 0U; token_index < parser->token_count; ++token_index) {
                    const F2cToken *token = &parser->tokens[token_index];
                    if (parser->error_at >= token->begin &&
                        parser->error_at <= token->begin + token->length) {
                        result->parse_error_span = token->span;
                        break;
                    }
                }
                if (result->parse_error_span.begin.line == 0U) {
                    const F2cToken *last = &parser->tokens[parser->token_count - 1U];
                    result->parse_error_span.begin = last->span.end;
                    result->parse_error_span.end = last->span.end;
                }
            }
        }
        if (result != NULL && result->source_offset != SIZE_MAX) {
            if (parser->token_count != 0U) {
                result->span = f2c_source_span_cover(
                    &parser->tokens[0].span, &parser->tokens[parser->token_count - 1U].span);
            } else {
                result->span.begin.line = 1U;
                result->span.begin.column = result->source_offset + 1U;
                result->span.end.line = 1U;
                result->span.end.column = result->source_offset + result->source_length + 1U;
            }
        }
    }
    if (result != NULL && parser->unit != NULL && parser->unit->context != NULL &&
        !f2c_ast_reserve_expression_nodes(parser->unit->context, result)) {
        f2c_ast_report_resource_limit(parser, "expression AST-node limit of %zu exceeded",
                                      parser->unit->context->limits.max_ast_nodes,
                                      &parser->unit->context->ast_node_limit_reported);
        f2c_expr_free(result);
        result = NULL;
    }
    if (error_at != NULL)
        *error_at = parser->error_at;
    return result;
}

F2cExpr *f2c_parse_expression_ast(Unit *unit, const char *expression, const char **error_at) {
    AstParser parser;
    const char *source = expression != NULL ? expression : "";
    memset(&parser, 0, sizeof(parser));
    parser.unit = unit;
    parser.source = source;
    parser.cursor = source;
    return parse_expression_stream(&parser, source, strlen(source), error_at);
}

F2cExpr *f2c_parse_expression_tokens(Unit *unit, const F2cToken *tokens, size_t token_count,
                                     const char *source, const char **error_at) {
    AstParser parser;
    const char *expression = source != NULL ? source : "";
    size_t expression_length = 0U;
    memset(&parser, 0, sizeof(parser));
    parser.unit = unit;
    parser.source = expression;
    parser.cursor = expression;
    parser.tokens = tokens;
    parser.token_count = token_count;
    if (token_count != 0U) {
        const F2cToken *last = &tokens[token_count - 1U];
        parser.source = tokens[0].begin;
        parser.cursor = tokens[0].begin;
        expression = tokens[0].begin;
        expression_length = (size_t)((last->begin + last->length) - tokens[0].begin);
    }
    return parse_expression_stream(&parser, expression, expression_length, error_at);
}
