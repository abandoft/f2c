#include "ast/declaration/use.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int set_error(F2cUseStatementSyntax *syntax, F2cUseStatementError error,
                     const F2cToken *token) {
    syntax->error = error;
    syntax->error_token = token;
    return 0;
}

static int tokens_equal(const F2cToken *left, const F2cToken *right) {
    size_t index;
    if (left == NULL || right == NULL || left->kind != right->kind || left->length != right->length)
        return 0;
    for (index = 0U; index < left->length; ++index) {
        unsigned char a = (unsigned char)left->begin[index];
        unsigned char b = (unsigned char)right->begin[index];
        if (a >= (unsigned char)'A' && a <= (unsigned char)'Z')
            a = (unsigned char)(a - (unsigned char)'A' + (unsigned char)'a');
        if (b >= (unsigned char)'A' && b <= (unsigned char)'Z')
            b = (unsigned char)(b - (unsigned char)'A' + (unsigned char)'a');
        if (a != b)
            return 0;
    }
    return 1;
}

static int designators_equal(const F2cUseDesignatorSyntax *left,
                             const F2cUseDesignatorSyntax *right) {
    size_t index;
    if (left->kind != right->kind || left->range.count != right->range.count)
        return 0;
    for (index = 0U; index < left->range.count; ++index) {
        if (!tokens_equal(&left->range.tokens[index], &right->range.tokens[index]))
            return 0;
    }
    return 1;
}

static int parse_generic_designator(const Line *line, size_t begin, size_t *end,
                                    F2cUseDesignatorSyntax *designator) {
    const F2cToken *keyword = &line->tokens[begin];
    size_t close;
    if (begin + 1U >= line->token_count || line->tokens[begin + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, begin + 1U, &close) ||
        close != begin + 3U)
        return 0;
    if (f2c_token_equals(keyword, "operator")) {
        if (line->tokens[begin + 2U].kind != F2C_TOKEN_OPERATOR ||
            f2c_token_equals(&line->tokens[begin + 2U], "=>"))
            return 0;
        designator->kind = F2C_USE_DESIGNATOR_OPERATOR;
    } else if (f2c_token_equals(keyword, "assignment")) {
        if (line->tokens[begin + 2U].kind != F2C_TOKEN_OPERATOR ||
            !f2c_token_equals(&line->tokens[begin + 2U], "="))
            return 0;
        designator->kind = F2C_USE_DESIGNATOR_ASSIGNMENT;
    } else if (f2c_token_equals(keyword, "read") || f2c_token_equals(keyword, "write")) {
        if (line->tokens[begin + 2U].kind != F2C_TOKEN_IDENTIFIER ||
            (!f2c_token_equals(&line->tokens[begin + 2U], "formatted") &&
             !f2c_token_equals(&line->tokens[begin + 2U], "unformatted")))
            return 0;
        designator->kind = F2C_USE_DESIGNATOR_DEFINED_IO;
    } else {
        return 0;
    }
    *end = close + 1U;
    return 1;
}

static int parse_designator(const Line *line, size_t begin, size_t *end,
                            F2cUseDesignatorSyntax *designator) {
    memset(designator, 0, sizeof(*designator));
    if (begin >= line->token_count || line->tokens[begin].kind != F2C_TOKEN_IDENTIFIER)
        return 0;
    if ((f2c_token_equals(&line->tokens[begin], "operator") ||
         f2c_token_equals(&line->tokens[begin], "assignment") ||
         f2c_token_equals(&line->tokens[begin], "read") ||
         f2c_token_equals(&line->tokens[begin], "write")) &&
        begin + 1U < line->token_count && line->tokens[begin + 1U].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!parse_generic_designator(line, begin, end, designator))
            return 0;
    } else {
        designator->kind = F2C_USE_DESIGNATOR_NAME;
        designator->name = &line->tokens[begin];
        *end = begin + 1U;
    }
    designator->range = f2c_line_token_range(line, begin, *end);
    designator->span =
        f2c_source_span_cover(&line->tokens[begin].span, &line->tokens[*end - 1U].span);
    return 1;
}

static int rename_kinds_match(const F2cUseDesignatorSyntax *local,
                              const F2cUseDesignatorSyntax *remote) {
    if (local->kind == F2C_USE_DESIGNATOR_NAME)
        return remote->kind == F2C_USE_DESIGNATOR_NAME;
    return local->kind == F2C_USE_DESIGNATOR_OPERATOR &&
           remote->kind == F2C_USE_DESIGNATOR_OPERATOR;
}

static int append_item(F2cUseStatementSyntax *syntax, F2cUseAssociationSyntax item) {
    F2cUseAssociationSyntax *replacement;
    size_t index;
    for (index = 0U; index < syntax->item_count; ++index) {
        if (designators_equal(&syntax->items[index].local, &item.local))
            return set_error(syntax, F2C_USE_ERROR_DUPLICATE_LOCAL_NAME, item.local.range.tokens);
    }
    if (syntax->item_count == syntax->item_capacity) {
        const size_t capacity = syntax->item_capacity == 0U ? 8U : syntax->item_capacity * 2U;
        if (capacity < syntax->item_capacity || capacity > SIZE_MAX / sizeof(*replacement))
            return -1;
        replacement =
            (F2cUseAssociationSyntax *)realloc(syntax->items, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return -1;
        syntax->items = replacement;
        syntax->item_capacity = capacity;
    }
    syntax->items[syntax->item_count++] = item;
    return 1;
}

static int parse_item(const Line *line, size_t *index, int only, F2cUseStatementSyntax *syntax) {
    F2cUseAssociationSyntax item;
    size_t end;
    int appended;
    memset(&item, 0, sizeof(item));
    if (!parse_designator(line, *index, &end, &item.local))
        return set_error(syntax, F2C_USE_ERROR_ITEM,
                         *index < line->token_count ? &line->tokens[*index]
                                                    : &line->tokens[line->token_count - 1U]);
    *index = end;
    if (*index < line->token_count && line->tokens[*index].kind == F2C_TOKEN_OPERATOR &&
        f2c_token_equals(&line->tokens[*index], "=>")) {
        const F2cToken *arrow = &line->tokens[(*index)++];
        item.renamed = 1;
        if (!parse_designator(line, *index, &end, &item.remote))
            return set_error(syntax, F2C_USE_ERROR_RENAME_TARGET,
                             *index < line->token_count ? &line->tokens[*index] : arrow);
        if (!rename_kinds_match(&item.local, &item.remote))
            return set_error(syntax, F2C_USE_ERROR_RENAME_KIND, item.remote.range.tokens);
        *index = end;
    } else {
        if (!only)
            return set_error(syntax, F2C_USE_ERROR_RENAME_REQUIRED, item.local.range.tokens);
        item.remote = item.local;
    }
    item.span =
        item.renamed ? f2c_source_span_cover(&item.local.span, &item.remote.span) : item.local.span;
    appended = append_item(syntax, item);
    return appended;
}

static int parse_items(const Line *line, size_t index, int only, F2cUseStatementSyntax *syntax) {
    if (index == line->token_count)
        return only ? 1
                    : set_error(syntax, F2C_USE_ERROR_TRAILING_COMMA,
                                &line->tokens[line->token_count - 1U]);
    while (index < line->token_count) {
        int parsed = parse_item(line, &index, only, syntax);
        if (parsed <= 0)
            return parsed;
        if (index == line->token_count)
            return 1;
        if (line->tokens[index].kind != F2C_TOKEN_COMMA)
            return set_error(syntax, F2C_USE_ERROR_LIST_SEPARATOR, &line->tokens[index]);
        ++index;
        if (index == line->token_count)
            return set_error(syntax, F2C_USE_ERROR_TRAILING_COMMA, &line->tokens[index - 1U]);
    }
    return 1;
}

int f2c_use_statement_candidate(const Line *line) {
    size_t start;
    if (line == NULL || line->token_count == 0U)
        return 0;
    start = line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
    if (!f2c_line_token_equals(line, start, "use"))
        return 0;
    return start + 1U >= line->token_count ||
           !((line->tokens[start + 1U].kind == F2C_TOKEN_OPERATOR &&
              f2c_token_equals(&line->tokens[start + 1U], "=")) ||
             line->tokens[start + 1U].kind == F2C_TOKEN_LEFT_PAREN ||
             line->tokens[start + 1U].kind == F2C_TOKEN_PERCENT);
}

F2cUseStatementStatus f2c_parse_use_statement_syntax(const Line *line,
                                                     F2cUseStatementSyntax *syntax) {
    size_t start;
    size_t index;
    int parsed;
    if (syntax == NULL)
        return F2C_USE_STATEMENT_INVALID;
    memset(syntax, 0, sizeof(*syntax));
    if (!f2c_use_statement_candidate(line))
        return F2C_USE_STATEMENT_NOT_MATCHED;
    start = line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
    syntax->keyword = &line->tokens[start];
    syntax->span = f2c_source_span_cover(&line->tokens[start].span,
                                         &line->tokens[line->token_count - 1U].span);
    index = start + 1U;
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_COMMA) {
        ++index;
        if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_IDENTIFIER ||
            (!f2c_token_equals(&line->tokens[index], "intrinsic") &&
             !f2c_token_equals(&line->tokens[index], "non_intrinsic"))) {
            set_error(syntax, F2C_USE_ERROR_MODULE_NATURE,
                      index < line->token_count ? &line->tokens[index] : &line->tokens[index - 1U]);
            return F2C_USE_STATEMENT_INVALID;
        }
        syntax->nature_token = &line->tokens[index];
        syntax->nature = f2c_token_equals(syntax->nature_token, "intrinsic")
                             ? F2C_USE_NATURE_INTRINSIC
                             : F2C_USE_NATURE_NON_INTRINSIC;
        ++index;
        if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_DOUBLE_COLON) {
            set_error(syntax, F2C_USE_ERROR_DOUBLE_COLON,
                      index < line->token_count ? &line->tokens[index] : syntax->nature_token);
            return F2C_USE_STATEMENT_INVALID;
        }
        ++index;
    } else if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON) {
        ++index;
    }
    if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
        set_error(syntax, F2C_USE_ERROR_MODULE_NAME,
                  index < line->token_count ? &line->tokens[index] : syntax->keyword);
        return F2C_USE_STATEMENT_INVALID;
    }
    syntax->module_name = &line->tokens[index++];
    if (index == line->token_count)
        return F2C_USE_STATEMENT_PARSED;
    if (line->tokens[index].kind != F2C_TOKEN_COMMA) {
        set_error(syntax, F2C_USE_ERROR_LIST_SEPARATOR, &line->tokens[index]);
        return F2C_USE_STATEMENT_INVALID;
    }
    ++index;
    if (index == line->token_count) {
        set_error(syntax, F2C_USE_ERROR_TRAILING_COMMA, &line->tokens[index - 1U]);
        return F2C_USE_STATEMENT_INVALID;
    }
    if (f2c_line_token_equals(line, index, "only")) {
        syntax->only_token = &line->tokens[index++];
        if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_COLON) {
            set_error(syntax, F2C_USE_ERROR_ONLY_COLON,
                      index < line->token_count ? &line->tokens[index] : syntax->only_token);
            return F2C_USE_STATEMENT_INVALID;
        }
        ++index;
    }
    parsed = parse_items(line, index, syntax->only_token != NULL, syntax);
    if (parsed < 0)
        return F2C_USE_STATEMENT_NO_MEMORY;
    if (!parsed)
        return F2C_USE_STATEMENT_INVALID;
    return F2C_USE_STATEMENT_PARSED;
}

void f2c_use_statement_syntax_discard(F2cUseStatementSyntax *syntax) {
    if (syntax == NULL)
        return;
    free(syntax->items);
    memset(syntax, 0, sizeof(*syntax));
}
