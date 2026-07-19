#include "frontend/declaration/private.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static size_t declaration_start(const Line *line, const char *keyword) {
    size_t index =
        line != NULL && line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U
                                                                                           : 0U;
    size_t cursor;
    size_t depth = 0U;
    if (!f2c_line_token_equals(line, index, keyword))
        return SIZE_MAX;
    ++index;
    for (cursor = index; cursor < line->token_count; ++cursor) {
        const F2cToken *token = &line->tokens[cursor];
        if (token->kind == F2C_TOKEN_LEFT_PAREN || token->kind == F2C_TOKEN_LEFT_BRACKET ||
            token->kind == F2C_TOKEN_ARRAY_BEGIN) {
            ++depth;
        } else if (token->kind == F2C_TOKEN_RIGHT_PAREN || token->kind == F2C_TOKEN_RIGHT_BRACKET ||
                   token->kind == F2C_TOKEN_ARRAY_END) {
            if (depth != 0U)
                --depth;
        } else if (depth == 0U && token->kind == F2C_TOKEN_OPERATOR &&
                   (f2c_token_equals(token, "=") || f2c_token_equals(token, "=>"))) {
            return SIZE_MAX;
        }
    }
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON)
        ++index;
    return index;
}

static int consume_identifier_list_separator(Context *context, const Line *line, size_t *index,
                                             const char *declaration) {
    if (*index == line->token_count)
        return 1;
    if (line->tokens[*index].kind != F2C_TOKEN_COMMA) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[*index], 1,
                                  "malformed %s declaration entity list", declaration);
        return 0;
    }
    ++*index;
    return *index < line->token_count;
}

void f2c_parse_external_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = declaration_start(source_line, "external");
    if (index == SIZE_MAX)
        return;
    while (index < source_line->token_count) {
        char *name;
        Symbol *symbol;
        if (source_line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      &source_line->tokens[index], 1,
                                      "malformed EXTERNAL declaration entity");
            return;
        }
        name = f2c_token_text(&source_line->tokens[index++]);
        symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
        if (symbol == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory in EXTERNAL declaration");
            free(name);
            return;
        }
        symbol->external = 1;
        symbol->external_declared = 1;
        if (symbol->type == TYPE_UNKNOWN)
            symbol->external_subroutine = 1;
        free(name);
        if (!consume_identifier_list_separator(context, source_line, &index, "EXTERNAL"))
            return;
    }
}

void f2c_parse_dimension_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = declaration_start(source_line, "dimension");
    if (index == SIZE_MAX)
        return;
    while (index < source_line->token_count) {
        size_t close;
        char *name;
        Symbol *symbol;
        if (source_line->tokens[index].kind != F2C_TOKEN_IDENTIFIER ||
            index + 1U >= source_line->token_count ||
            source_line->tokens[index + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
            !f2c_token_matching_delimiter(source_line->tokens, source_line->token_count, index + 1U,
                                          &close)) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      &source_line->tokens[index], 1,
                                      "malformed DIMENSION declaration");
            return;
        }
        name = f2c_token_text(&source_line->tokens[index]);
        symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
        if (symbol == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory in DIMENSION declaration");
        } else if (symbol->rank != 0U) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, source_line,
                                      &source_line->tokens[index], 1,
                                      "duplicate array specification for '%s'", symbol->name);
        } else {
            (void)f2c_parse_dimensions_tokens(context, unit, symbol, source_line, index + 1U,
                                              close);
        }
        free(name);
        index = close + 1U;
        if (!consume_identifier_list_separator(context, source_line, &index, "DIMENSION"))
            return;
    }
}

void f2c_parse_parameter_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = declaration_start(source_line, "parameter");
    size_t close;
    if (index == SIZE_MAX)
        return;
    if (index >= source_line->token_count ||
        source_line->tokens[index].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(source_line->tokens, source_line->token_count, index,
                                      &close) ||
        close + 1U != source_line->token_count) {
        f2c_diagnostic(context, source_line->number, 1, "malformed PARAMETER declaration");
        return;
    }
    ++index;
    while (index < close) {
        const F2cToken *name_token;
        size_t expression_begin;
        size_t expression_end;
        char *name;
        char *initializer;
        Symbol *symbol;
        if (source_line->tokens[index].kind != F2C_TOKEN_IDENTIFIER || index + 1U >= close ||
            source_line->tokens[index + 1U].kind != F2C_TOKEN_OPERATOR ||
            !f2c_token_equals(&source_line->tokens[index + 1U], "=")) {
            f2c_diagnostic(context, source_line->number, 1, "malformed PARAMETER initializer");
            return;
        }
        name_token = &source_line->tokens[index];
        expression_begin = index + 2U;
        expression_end = expression_begin;
        while (expression_end < close) {
            size_t nested_close;
            if (source_line->tokens[expression_end].kind == F2C_TOKEN_LEFT_PAREN ||
                source_line->tokens[expression_end].kind == F2C_TOKEN_LEFT_BRACKET ||
                source_line->tokens[expression_end].kind == F2C_TOKEN_ARRAY_BEGIN) {
                if (!f2c_token_matching_delimiter(source_line->tokens, close, expression_end,
                                                  &nested_close)) {
                    f2c_diagnostic(context, source_line->number, 1,
                                   "malformed PARAMETER initializer");
                    return;
                }
                expression_end = nested_close + 1U;
            } else if (source_line->tokens[expression_end].kind == F2C_TOKEN_COMMA) {
                break;
            } else {
                ++expression_end;
            }
        }
        if (expression_begin == expression_end) {
            f2c_diagnostic(context, source_line->number, 1, "malformed PARAMETER initializer");
            return;
        }
        name = f2c_token_text(name_token);
        initializer = f2c_token_range_text(
            f2c_line_token_range(source_line, expression_begin, expression_end));
        symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
        if (symbol == NULL || initializer == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory in PARAMETER declaration");
            free(name);
            free(initializer);
            return;
        }
        symbol->parameter = 1;
        free(symbol->initializer);
        symbol->initializer = initializer;
        symbol->initializer_syntax =
            f2c_line_token_range(source_line, expression_begin, expression_end);
        symbol->kind_type =
            f2c_kind_type_from_tokens(unit, source_line, expression_begin, expression_end);
        free(name);
        index = expression_end;
        if (index < close)
            ++index;
    }
}

void f2c_parse_save_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = declaration_start(source_line, "save");
    if (index == SIZE_MAX)
        return;
    if (index == source_line->token_count) {
        unit->save_all = 1;
        return;
    }
    if (source_line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
        f2c_token_equals(&source_line->tokens[index], "/")) {
        /* COMMON storage is already emitted at file scope. */
        return;
    }
    while (index < source_line->token_count) {
        char *name;
        Symbol *symbol;
        if (source_line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic(context, source_line->number, 1, "malformed SAVE declaration");
            return;
        }
        name = f2c_token_text(&source_line->tokens[index++]);
        symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
        if (symbol == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory in SAVE declaration");
            free(name);
            return;
        }
        symbol->saved = 1;
        free(name);
        if (!consume_identifier_list_separator(context, source_line, &index, "SAVE"))
            return;
    }
}

typedef struct EquivalenceMemberRange {
    size_t begin;
    size_t end;
} EquivalenceMemberRange;

static int evaluate_dimension_bound(Unit *unit, const Symbol *symbol, size_t dimension_index,
                                    int upper, int64_t *value) {
    const Dimension *dimension = &symbol->dimensions[dimension_index];
    const F2cExpr *expression = upper ? dimension->upper_expression : dimension->lower_expression;
    const F2cTokenRange syntax = upper ? symbol->dimension_upper_syntax[dimension_index]
                                       : symbol->dimension_lower_syntax[dimension_index];
    if (expression != NULL)
        return f2c_evaluate_integer_constant(unit, expression, value);
    if (syntax.count != 0U)
        return f2c_evaluate_integer_syntax(unit, syntax, value);
    if (!upper) {
        *value = 1;
        return 1;
    }
    return 0;
}

static int nonnegative_difference(int64_t upper, int64_t lower, int64_t *difference) {
    if (upper < lower || (lower < 0 && upper > INT64_MAX + lower))
        return 0;
    *difference = upper - lower;
    return 1;
}

static int next_equivalence_member(const Line *line, size_t group_close, size_t *position,
                                   EquivalenceMemberRange *range) {
    size_t cursor;
    if (*position >= group_close)
        return 0;
    range->begin = *position;
    for (cursor = *position; cursor < group_close; ++cursor) {
        const F2cTokenKind kind = line->tokens[cursor].kind;
        size_t nested_close;
        if (kind == F2C_TOKEN_LEFT_PAREN || kind == F2C_TOKEN_LEFT_BRACKET ||
            kind == F2C_TOKEN_ARRAY_BEGIN) {
            if (!f2c_token_matching_delimiter(line->tokens, group_close, cursor, &nested_close))
                return -1;
            cursor = nested_close;
            continue;
        }
        if (kind == F2C_TOKEN_COMMA) {
            if (range->begin == cursor)
                return -1;
            range->end = cursor;
            *position = cursor + 1U;
            return 1;
        }
    }
    if (range->begin == group_close)
        return -1;
    range->end = group_close;
    *position = group_close;
    return 1;
}

static int equivalence_designator(Context *context, Unit *unit, const Line *line,
                                  EquivalenceMemberRange range, Symbol **symbol_out,
                                  int64_t *offset_out) {
    const F2cToken *name_token;
    char *name;
    Symbol *symbol;
    size_t open;
    size_t close;
    size_t item_begin;
    size_t cursor;
    size_t dimension = 0U;
    int64_t offset = 0;
    int64_t stride = 1;

    if (range.begin >= range.end || line->tokens[range.begin].kind != F2C_TOKEN_IDENTIFIER) {
        const F2cToken *token = range.begin < line->token_count ? &line->tokens[range.begin] : NULL;
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1,
                                  "malformed EQUIVALENCE designator");
        return 0;
    }
    name_token = &line->tokens[range.begin];
    name = f2c_token_text(name_token);
    symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
    free(name);
    if (symbol == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, name_token, 1,
                                  "out of memory parsing EQUIVALENCE designator");
        return 0;
    }
    open = range.begin + 1U;
    if (open == range.end) {
        *symbol_out = symbol;
        *offset_out = 0;
        return 1;
    }
    if (line->tokens[open].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, range.end, open, &close) ||
        close + 1U != range.end) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[open], 1,
                                  "malformed EQUIVALENCE array designator");
        return 0;
    }
    item_begin = open + 1U;
    for (cursor = item_begin; cursor <= close; ++cursor) {
        const int separator = cursor == close || line->tokens[cursor].kind == F2C_TOKEN_COMMA;
        if (!separator && (line->tokens[cursor].kind == F2C_TOKEN_LEFT_PAREN ||
                           line->tokens[cursor].kind == F2C_TOKEN_LEFT_BRACKET ||
                           line->tokens[cursor].kind == F2C_TOKEN_ARRAY_BEGIN)) {
            size_t nested_close;
            if (!f2c_token_matching_delimiter(line->tokens, close, cursor, &nested_close)) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[cursor], 1,
                                          "malformed EQUIVALENCE subscript");
                return 0;
            }
            cursor = nested_close;
            continue;
        }
        if (separator) {
            const char *error_at = NULL;
            F2cExpr *expression;
            int64_t index_value;
            int64_t lower;
            int64_t upper;
            int64_t extent;
            int64_t delta;
            int64_t term;
            const F2cToken *diagnostic_token =
                item_begin < close ? &line->tokens[item_begin] : &line->tokens[open];
            if (item_begin == cursor || dimension >= symbol->rank) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, diagnostic_token, 1,
                                          "EQUIVALENCE subscript count does not match rank");
                return 0;
            }
            expression = f2c_parse_expression_tokens(unit, &line->tokens[item_begin],
                                                     cursor - item_begin, line->text, &error_at);
            if (expression == NULL || error_at != NULL ||
                !f2c_evaluate_integer_constant(unit, expression, &index_value) ||
                !evaluate_dimension_bound(unit, symbol, dimension, 0, &lower) ||
                !evaluate_dimension_bound(unit, symbol, dimension, 1, &upper) ||
                upper < lower || index_value < lower || index_value > upper) {
                f2c_expr_free(expression);
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, diagnostic_token,
                                          1, "EQUIVALENCE subscript must be an in-bounds constant");
                return 0;
            }
            f2c_expr_free(expression);
            if (!nonnegative_difference(index_value, lower, &delta) ||
                !nonnegative_difference(upper, lower, &extent) || extent == INT64_MAX) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, diagnostic_token,
                                          1, "EQUIVALENCE storage offset exceeds supported range");
                return 0;
            }
            ++extent;
            if ((delta != 0 && stride > INT64_MAX / delta) ||
                (term = delta * stride) > INT64_MAX - offset ||
                (dimension + 1U < symbol->rank && extent != 0 && stride > INT64_MAX / extent)) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, diagnostic_token,
                                          1, "EQUIVALENCE storage offset exceeds supported range");
                return 0;
            }
            offset += term;
            stride *= extent;
            ++dimension;
            item_begin = cursor + 1U;
        }
    }
    if (dimension != symbol->rank) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, name_token, 1,
                                  "EQUIVALENCE subscript count does not match rank");
        return 0;
    }
    *symbol_out = symbol;
    *offset_out = offset;
    return 1;
}

static int resolve_equivalence_root(Unit *unit, Symbol *symbol, int64_t designator_offset,
                                    Symbol **root_out, int64_t *offset_out) {
    size_t traversed = 0U;
    int64_t offset = designator_offset;
    while (symbol != NULL && symbol->alias_to != NULL) {
        if (++traversed > unit->symbol_count ||
            (symbol->alias_offset > 0 && offset > INT64_MAX - symbol->alias_offset) ||
            (symbol->alias_offset < 0 && offset < INT64_MIN - symbol->alias_offset))
            return 0;
        offset += symbol->alias_offset;
        symbol = f2c_find_symbol(unit, symbol->alias_to);
    }
    if (symbol == NULL)
        return 0;
    *root_out = symbol;
    *offset_out = offset;
    return 1;
}

static char *equivalence_alias_c_name(const Symbol *root, const Symbol *alias,
                                      int64_t element_offset) {
    Buffer result = {0};

    if (root == NULL || alias == NULL || root->c_name == NULL || element_offset < 0)
        return NULL;

    if (alias->rank != 0U) {
        if (root->rank == 0U) {
            if (element_offset != 0)
                return NULL;
            f2c_buffer_printf(&result, "(&%s)", root->c_name);
        } else if (element_offset == 0) {
            f2c_buffer_append(&result, root->c_name);
        } else {
            f2c_buffer_printf(&result, "(&%s[%lld])", root->c_name, (long long)element_offset);
        }
    } else if (root->rank != 0U) {
        f2c_buffer_printf(&result, "(%s[%lld])", root->c_name, (long long)element_offset);
    } else if (element_offset == 0) {
        f2c_buffer_append(&result, root->c_name);
    } else {
        return NULL;
    }
    return f2c_buffer_take(&result);
}

void f2c_parse_equivalence_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = declaration_start(source_line, "equivalence");
    if (index == SIZE_MAX)
        return;
    while (index < source_line->token_count) {
        size_t close;
        size_t cursor;
        size_t count = 0U;
        int member_status;
        EquivalenceMemberRange range;
        if (source_line->tokens[index].kind == F2C_TOKEN_COMMA)
            ++index;
        if (index >= source_line->token_count ||
            source_line->tokens[index].kind != F2C_TOKEN_LEFT_PAREN ||
            !f2c_token_matching_delimiter(source_line->tokens, source_line->token_count, index,
                                          &close)) {
            const F2cToken *token =
                index < source_line->token_count ? &source_line->tokens[index] : NULL;
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line, token, 1,
                                      "unclosed EQUIVALENCE group");
            return;
        }
        cursor = index + 1U;
        while ((member_status = next_equivalence_member(source_line, close, &cursor, &range)) > 0)
            ++count;
        if (member_status < 0) {
            const F2cToken *token =
                cursor < close ? &source_line->tokens[cursor] : &source_line->tokens[index];
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line, token, 1,
                                      "malformed EQUIVALENCE designator list");
            index = close + 1U;
            continue;
        }
        if (count < 2U) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      &source_line->tokens[index], 1,
                                      "EQUIVALENCE group requires at least two designators");
        } else {
            Symbol *first = NULL;
            Symbol *root;
            int64_t first_offset = 0;
            int64_t root_offset;
            cursor = index + 1U;
            (void)next_equivalence_member(source_line, close, &cursor, &range);
            if (equivalence_designator(context, unit, source_line, range, &first, &first_offset)) {
                if (!resolve_equivalence_root(unit, first, first_offset, &root, &root_offset)) {
                    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, source_line,
                                              &source_line->tokens[range.begin], 1,
                                              "EQUIVALENCE storage association is cyclic or "
                                              "out of range");
                } else {
                    size_t member;
                    for (member = 1U; member < count; ++member) {
                        Symbol *alias = NULL;
                        int64_t alias_designator_offset = 0;
                        int64_t alias_offset;
                        Symbol *alias_root;
                        int64_t existing_offset;
                        (void)next_equivalence_member(source_line, close, &cursor, &range);
                        if (!equivalence_designator(context, unit, source_line, range, &alias,
                                                    &alias_designator_offset))
                            continue;
                        if (alias->alias_to != NULL || alias == root) {
                            if (!resolve_equivalence_root(unit, alias, alias_designator_offset,
                                                          &alias_root, &existing_offset) ||
                                alias_root != root || existing_offset != root_offset) {
                                f2c_diagnostic_token_code(
                                    context, F2C_DIAGNOSTIC_SEMANTIC, source_line,
                                    &source_line->tokens[range.begin], 1,
                                    "conflicting EQUIVALENCE storage associations");
                            }
                            continue;
                        }
                        alias_offset = root_offset - alias_designator_offset;
                        free(alias->alias_to);
                        alias->alias_to = f2c_strdup(root->name);
                        alias->alias_offset = alias_offset;
                        free(alias->c_name);
                        alias->c_name = equivalence_alias_c_name(root, alias, alias_offset);
                        if (alias->alias_to == NULL)
                            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY,
                                                      source_line,
                                                      &source_line->tokens[range.begin], 1,
                                                      "out of memory in EQUIVALENCE declaration");
                        else if (alias->c_name == NULL)
                            f2c_diagnostic_token_code(
                                context, F2C_DIAGNOSTIC_UNSUPPORTED, source_line,
                                &source_line->tokens[range.begin], 1,
                                "unsupported EQUIVALENCE storage layout for '%s'", alias->name);
                    }
                }
            }
        }
        index = close + 1U;
    }
}
