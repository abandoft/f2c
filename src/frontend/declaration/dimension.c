#include "frontend/private.h"

#include <stdlib.h>
#include <string.h>

typedef struct ParsedDimension {
    Dimension dimension;
    F2cTokenRange lower_syntax;
    F2cTokenRange upper_syntax;
} ParsedDimension;

static int left_delimiter(F2cTokenKind kind) {
    return kind == F2C_TOKEN_LEFT_PAREN || kind == F2C_TOKEN_LEFT_BRACKET ||
           kind == F2C_TOKEN_ARRAY_BEGIN;
}

static void release_parsed_dimensions(ParsedDimension *dimensions, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        free(dimensions[index].dimension.lower);
        free(dimensions[index].dimension.upper);
    }
}

static int range_is_operator(const Line *line, size_t begin, size_t end,
                             const char *operator_text) {
    return end == begin + 1U && line->tokens[begin].kind == F2C_TOKEN_OPERATOR &&
           f2c_token_equals(&line->tokens[begin], operator_text);
}

static int assign_range_text(const Line *line, size_t begin, size_t end, char **text,
                             F2cTokenRange *syntax) {
    *syntax = f2c_line_token_range(line, begin, end);
    *text = f2c_token_range_text(*syntax);
    return *text != NULL;
}

static int parse_dimension(Context *context, const Line *line, size_t begin, size_t end,
                           int dynamic, ParsedDimension *result) {
    size_t index;
    size_t colon = SIZE_MAX;
    memset(result, 0, sizeof(*result));
    if (begin == end) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, NULL, 1,
                                  "array specification contains an empty dimension");
        return 0;
    }
    for (index = begin; index < end; ++index) {
        size_t close;
        if (left_delimiter(line->tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(line->tokens, end, index, &close)) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[index], 1,
                                          "unclosed delimiter in array specification");
                return 0;
            }
            index = close;
        } else if (line->tokens[index].kind == F2C_TOKEN_COLON) {
            if (colon != SIZE_MAX) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[index], 1,
                                          "array declaration dimension has multiple colons");
                return 0;
            }
            colon = index;
        }
    }
    if (colon == SIZE_MAX) {
        result->dimension.lower = f2c_strdup("1");
        if (!assign_range_text(line, begin, end, &result->dimension.upper, &result->upper_syntax))
            goto out_of_memory;
        result->dimension.kind = range_is_operator(line, begin, end, "*")
                                     ? F2C_DIMENSION_ASSUMED_SIZE
                                     : F2C_DIMENSION_EXPLICIT;
    } else {
        if (colon == begin) {
            result->dimension.lower = f2c_strdup("1");
        } else if (!assign_range_text(line, begin, colon, &result->dimension.lower,
                                      &result->lower_syntax)) {
            goto out_of_memory;
        }
        if (colon + 1U == end) {
            result->dimension.upper = f2c_strdup("*");
            result->dimension.kind = dynamic ? F2C_DIMENSION_DEFERRED : F2C_DIMENSION_ASSUMED_SHAPE;
        } else if (range_is_operator(line, colon + 1U, end, "*")) {
            result->dimension.upper = f2c_strdup("*");
            result->upper_syntax = f2c_line_token_range(line, colon + 1U, end);
            result->dimension.kind = F2C_DIMENSION_ASSUMED_SIZE;
        } else {
            if (!assign_range_text(line, colon + 1U, end, &result->dimension.upper,
                                   &result->upper_syntax))
                goto out_of_memory;
            result->dimension.kind = F2C_DIMENSION_EXPLICIT;
        }
    }
    if (result->dimension.lower == NULL || result->dimension.upper == NULL)
        goto out_of_memory;
    return 1;

out_of_memory:
    free(result->dimension.lower);
    free(result->dimension.upper);
    memset(result, 0, sizeof(*result));
    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line,
                              begin < line->token_count ? &line->tokens[begin] : NULL, 1,
                              "out of memory parsing array specification");
    return 0;
}

int f2c_parse_dimensions_tokens(Context *context, Unit *unit, Symbol *symbol, const Line *line,
                                size_t open, size_t close) {
    ParsedDimension parsed[F2C_MAX_RANK];
    size_t count = 0U;
    size_t begin;
    size_t index;
    size_t old_rank;
    int success = 1;
    if (context == NULL || symbol == NULL || line == NULL || open >= close ||
        close >= line->token_count || line->tokens[open].kind != F2C_TOKEN_LEFT_PAREN ||
        line->tokens[close].kind != F2C_TOKEN_RIGHT_PAREN) {
        return 0;
    }
    memset(parsed, 0, sizeof(parsed));
    begin = open + 1U;
    if (begin == close) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[open], 1,
                                  "array specification must contain at least one dimension");
        return 0;
    }
    for (index = begin; index <= close; ++index) {
        const int at_end = index == close;
        size_t nested_close;
        if (!at_end && left_delimiter(line->tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(line->tokens, close, index, &nested_close)) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[index], 1,
                                          "unclosed delimiter in array specification");
                success = 0;
                break;
            }
            index = nested_close;
            continue;
        }
        if (at_end || line->tokens[index].kind == F2C_TOKEN_COMMA) {
            if (count == F2C_MAX_RANK) {
                f2c_diagnostic_token_code(
                    context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[index], 1,
                    "array rank exceeds the Fortran maximum of %u", (unsigned int)F2C_MAX_RANK);
                success = 0;
                break;
            }
            if (!parse_dimension(context, line, begin, index,
                                 symbol->allocatable || symbol->pointer, &parsed[count])) {
                success = 0;
                break;
            }
            ++count;
            begin = index + 1U;
        }
    }
    if (success) {
        for (index = 0U; index + 1U < count; ++index) {
            if (parsed[index].dimension.kind == F2C_DIMENSION_ASSUMED_SIZE) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                          &line->tokens[open], 1,
                                          "only the final array dimension may be assumed-size");
                success = 0;
                break;
            }
        }
    }
    if (!success) {
        release_parsed_dimensions(parsed, count);
        return 0;
    }
    old_rank = symbol->rank;
    for (index = 0U; index < old_rank; ++index) {
        free(symbol->dimensions[index].lower);
        free(symbol->dimensions[index].upper);
        f2c_expr_free(symbol->dimensions[index].lower_expression);
        f2c_expr_free(symbol->dimensions[index].upper_expression);
        memset(&symbol->dimensions[index], 0, sizeof(symbol->dimensions[index]));
        memset(&symbol->dimension_lower_syntax[index], 0,
               sizeof(symbol->dimension_lower_syntax[index]));
        memset(&symbol->dimension_upper_syntax[index], 0,
               sizeof(symbol->dimension_upper_syntax[index]));
    }
    for (index = 0U; index < count; ++index) {
        symbol->dimensions[index] = parsed[index].dimension;
        symbol->dimension_lower_syntax[index] = parsed[index].lower_syntax;
        symbol->dimension_upper_syntax[index] = parsed[index].upper_syntax;
    }
    symbol->rank = count;
    f2c_shape_from_symbol(unit, &symbol->shape, symbol);
    return 1;
}
