#include "ast/unit.h"

#include <stdlib.h>
#include <string.h>

typedef enum PrefixKind {
    PREFIX_NONE,
    PREFIX_RECURSIVE,
    PREFIX_PURE,
    PREFIX_ELEMENTAL,
    PREFIX_IMPURE,
    PREFIX_MODULE
} PrefixKind;

static PrefixKind prefix_kind(const F2cToken *token) {
    if (token == NULL || token->kind != F2C_TOKEN_IDENTIFIER)
        return PREFIX_NONE;
    if (f2c_token_equals(token, "recursive"))
        return PREFIX_RECURSIVE;
    if (f2c_token_equals(token, "pure"))
        return PREFIX_PURE;
    if (f2c_token_equals(token, "elemental"))
        return PREFIX_ELEMENTAL;
    if (f2c_token_equals(token, "impure"))
        return PREFIX_IMPURE;
    if (f2c_token_equals(token, "module"))
        return PREFIX_MODULE;
    return PREFIX_NONE;
}

static int type_prefix_start(const F2cToken *token) {
    return token != NULL && token->kind == F2C_TOKEN_IDENTIFIER &&
           (f2c_token_equals(token, "double") || f2c_token_equals(token, "integer") ||
            f2c_token_equals(token, "real") || f2c_token_equals(token, "complex") ||
            f2c_token_equals(token, "logical") || f2c_token_equals(token, "character") ||
            f2c_token_equals(token, "type") || f2c_token_equals(token, "class"));
}

static int unit_kind(const F2cToken *token, F2cUnitSyntaxKind *kind) {
    if (token == NULL || token->kind != F2C_TOKEN_IDENTIFIER)
        return 0;
    if (f2c_token_equals(token, "program"))
        *kind = F2C_UNIT_SYNTAX_PROGRAM;
    else if (f2c_token_equals(token, "subroutine"))
        *kind = F2C_UNIT_SYNTAX_SUBROUTINE;
    else if (f2c_token_equals(token, "function"))
        *kind = F2C_UNIT_SYNTAX_FUNCTION;
    else
        return 0;
    return 1;
}

static size_t find_unit_keyword(const Line *line) {
    size_t index = 0U;
    while (index < line->token_count) {
        size_t close;
        F2cUnitSyntaxKind kind;
        if (unit_kind(&line->tokens[index], &kind))
            return index;
        if (line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN ||
            line->tokens[index].kind == F2C_TOKEN_LEFT_BRACKET ||
            line->tokens[index].kind == F2C_TOKEN_ARRAY_BEGIN) {
            if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close))
                return SIZE_MAX;
            index = close + 1U;
        } else {
            ++index;
        }
    }
    return SIZE_MAX;
}

static const F2cToken **prefix_slot(F2cUnitHeaderSyntax *syntax, PrefixKind kind) {
    switch (kind) {
    case PREFIX_RECURSIVE:
        return &syntax->recursive_prefix;
    case PREFIX_PURE:
        return &syntax->pure_prefix;
    case PREFIX_ELEMENTAL:
        return &syntax->elemental_prefix;
    case PREFIX_IMPURE:
        return &syntax->impure_prefix;
    case PREFIX_MODULE:
        return &syntax->module_prefix;
    case PREFIX_NONE:
        break;
    }
    return NULL;
}

static int set_error(F2cUnitHeaderSyntax *syntax, F2cUnitHeaderError error, const F2cToken *token) {
    syntax->error = error;
    syntax->error_token = token;
    return 0;
}

static int parse_prefixes(const Line *line, size_t keyword, F2cUnitHeaderSyntax *syntax) {
    size_t index = 0U;
    size_t type_begin = SIZE_MAX;
    size_t type_end = SIZE_MAX;
    while (index < keyword) {
        const PrefixKind kind = prefix_kind(&line->tokens[index]);
        if (kind != PREFIX_NONE) {
            const F2cToken **slot = prefix_slot(syntax, kind);
            if (*slot != NULL)
                return set_error(syntax, F2C_UNIT_HEADER_ERROR_DUPLICATE_PREFIX,
                                 &line->tokens[index]);
            *slot = &line->tokens[index++];
            continue;
        }
        if (type_begin != SIZE_MAX)
            return set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_PREFIX, &line->tokens[index]);
        type_begin = index;
        while (index < keyword && prefix_kind(&line->tokens[index]) == PREFIX_NONE) {
            size_t close;
            if (line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN ||
                line->tokens[index].kind == F2C_TOKEN_LEFT_BRACKET ||
                line->tokens[index].kind == F2C_TOKEN_ARRAY_BEGIN) {
                if (!f2c_token_matching_delimiter(line->tokens, keyword, index, &close))
                    return set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_PREFIX,
                                     &line->tokens[index]);
                index = close + 1U;
            } else {
                ++index;
            }
        }
        type_end = index;
    }
    if (type_begin != SIZE_MAX)
        syntax->type_spec = f2c_line_token_range(line, type_begin, type_end);
    return 1;
}

static int tokens_equal(const F2cToken *left, const F2cToken *right) {
    size_t index;
    if (left == NULL || right == NULL || left->length != right->length)
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

static int append_argument(F2cUnitHeaderSyntax *syntax, const F2cToken *token,
                           int alternate_return) {
    F2cUnitDummySyntax *replacement;
    size_t index;
    if (!alternate_return) {
        for (index = 0U; index < syntax->argument_count; ++index) {
            if (!syntax->arguments[index].alternate_return &&
                tokens_equal(syntax->arguments[index].token, token))
                return set_error(syntax, F2C_UNIT_HEADER_ERROR_DUPLICATE_ARGUMENT, token);
        }
    }
    if (syntax->argument_count == SIZE_MAX / sizeof(*replacement))
        return -1;
    replacement = (F2cUnitDummySyntax *)realloc(syntax->arguments, (syntax->argument_count + 1U) *
                                                                       sizeof(*replacement));
    if (replacement == NULL)
        return -1;
    syntax->arguments = replacement;
    syntax->arguments[syntax->argument_count].token = token;
    syntax->arguments[syntax->argument_count].alternate_return = alternate_return;
    ++syntax->argument_count;
    return 1;
}

static int parse_arguments(const Line *line, size_t open, size_t close,
                           F2cUnitHeaderSyntax *syntax) {
    size_t index = open + 1U;
    if (index == close)
        return 1;
    while (index < close) {
        const F2cToken *token = &line->tokens[index];
        int alternate_return = 0;
        int appended;
        if (token->kind == F2C_TOKEN_OPERATOR && f2c_token_equals(token, "*")) {
            alternate_return = 1;
        } else if (token->kind != F2C_TOKEN_IDENTIFIER) {
            return set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_ARGUMENT, token);
        }
        if (alternate_return && syntax->kind != F2C_UNIT_SYNTAX_SUBROUTINE)
            return set_error(syntax, F2C_UNIT_HEADER_ERROR_ALTERNATE_RETURN, token);
        appended = append_argument(syntax, token, alternate_return);
        if (appended <= 0)
            return appended;
        ++index;
        if (index == close)
            return 1;
        if (line->tokens[index].kind != F2C_TOKEN_COMMA)
            return set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_ARGUMENT_LIST,
                             &line->tokens[index]);
        ++index;
        if (index == close)
            return set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_ARGUMENT_LIST,
                             &line->tokens[index - 1U]);
    }
    return 1;
}

static int parse_result(const Line *line, size_t index, F2cUnitHeaderSyntax *syntax) {
    size_t close;
    if (index == line->token_count)
        return 1;
    if (syntax->kind != F2C_UNIT_SYNTAX_FUNCTION || !f2c_line_token_equals(line, index, "result"))
        return set_error(syntax, F2C_UNIT_HEADER_ERROR_TRAILING_TOKENS, &line->tokens[index]);
    if (index + 1U >= line->token_count || line->tokens[index + 1U].kind != F2C_TOKEN_LEFT_PAREN)
        return set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_RESULT, &line->tokens[index]);
    if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index + 1U, &close))
        return set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_RESULT, &line->tokens[index + 1U]);
    if (close != index + 3U || line->tokens[index + 2U].kind != F2C_TOKEN_IDENTIFIER)
        return set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_RESULT,
                         index + 2U < line->token_count ? &line->tokens[index + 2U]
                                                        : &line->tokens[index]);
    if (close + 1U != line->token_count)
        return set_error(syntax, F2C_UNIT_HEADER_ERROR_TRAILING_TOKENS, &line->tokens[close + 1U]);
    syntax->result_name = &line->tokens[index + 2U];
    return 1;
}

F2cUnitHeaderParseStatus f2c_parse_unit_header_syntax(const Line *line,
                                                      F2cUnitHeaderSyntax *syntax) {
    size_t keyword;
    size_t index;
    size_t close;
    int parsed;
    if (syntax == NULL)
        return F2C_UNIT_HEADER_INVALID;
    memset(syntax, 0, sizeof(*syntax));
    if (line == NULL || line->token_count == 0U)
        return F2C_UNIT_HEADER_NOT_MATCHED;
    if (f2c_line_token_equals(line, 0U, "end"))
        return F2C_UNIT_HEADER_NOT_MATCHED;
    keyword = find_unit_keyword(line);
    if (keyword == SIZE_MAX)
        return F2C_UNIT_HEADER_NOT_MATCHED;
    if (keyword != 0U && prefix_kind(&line->tokens[0]) == PREFIX_NONE &&
        !type_prefix_start(&line->tokens[0]))
        return F2C_UNIT_HEADER_NOT_MATCHED;
    (void)unit_kind(&line->tokens[keyword], &syntax->kind);
    syntax->span =
        f2c_source_span_cover(&line->tokens[0].span, &line->tokens[line->token_count - 1U].span);
    if (!parse_prefixes(line, keyword, syntax))
        return F2C_UNIT_HEADER_INVALID;
    index = keyword + 1U;
    if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
        set_error(syntax, F2C_UNIT_HEADER_ERROR_MISSING_NAME,
                  index < line->token_count ? &line->tokens[index] : &line->tokens[keyword]);
        return F2C_UNIT_HEADER_INVALID;
    }
    syntax->name = &line->tokens[index++];
    if (syntax->kind == F2C_UNIT_SYNTAX_PROGRAM) {
        if (index != line->token_count) {
            set_error(syntax, F2C_UNIT_HEADER_ERROR_TRAILING_TOKENS, &line->tokens[index]);
            return F2C_UNIT_HEADER_INVALID;
        }
        return F2C_UNIT_HEADER_PARSED;
    }
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close)) {
            set_error(syntax, F2C_UNIT_HEADER_ERROR_MALFORMED_ARGUMENT_LIST, &line->tokens[index]);
            return F2C_UNIT_HEADER_INVALID;
        }
        parsed = parse_arguments(line, index, close, syntax);
        if (parsed < 0)
            return F2C_UNIT_HEADER_NO_MEMORY;
        if (!parsed)
            return F2C_UNIT_HEADER_INVALID;
        index = close + 1U;
    }
    if (!parse_result(line, index, syntax))
        return F2C_UNIT_HEADER_INVALID;
    return F2C_UNIT_HEADER_PARSED;
}

void f2c_unit_header_syntax_discard(F2cUnitHeaderSyntax *syntax) {
    if (syntax == NULL)
        return;
    free(syntax->arguments);
    memset(syntax, 0, sizeof(*syntax));
}

static int separated_end_kind(const F2cToken *token, F2cUnitSyntaxKind *kind) {
    return unit_kind(token, kind) ||
           (token != NULL && token->kind == F2C_TOKEN_IDENTIFIER &&
            f2c_token_equals(token, "module") && (*kind = F2C_UNIT_SYNTAX_MODULE, 1));
}

static int joined_end_kind(const F2cToken *token, F2cUnitSyntaxKind *kind) {
    if (token == NULL || token->kind != F2C_TOKEN_IDENTIFIER)
        return 0;
    if (f2c_token_equals(token, "endprogram"))
        *kind = F2C_UNIT_SYNTAX_PROGRAM;
    else if (f2c_token_equals(token, "endsubroutine"))
        *kind = F2C_UNIT_SYNTAX_SUBROUTINE;
    else if (f2c_token_equals(token, "endfunction"))
        *kind = F2C_UNIT_SYNTAX_FUNCTION;
    else if (f2c_token_equals(token, "endmodule"))
        *kind = F2C_UNIT_SYNTAX_MODULE;
    else
        return 0;
    return 1;
}

F2cUnitEndParseStatus f2c_parse_unit_end_syntax(const Line *line, F2cUnitEndSyntax *syntax) {
    size_t start;
    size_t index;
    if (syntax == NULL)
        return F2C_UNIT_END_INVALID;
    memset(syntax, 0, sizeof(*syntax));
    if (line == NULL || line->token_count == 0U)
        return F2C_UNIT_END_NOT_MATCHED;
    start = line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
    if (start == line->token_count)
        return F2C_UNIT_END_NOT_MATCHED;
    syntax->span = f2c_source_span_cover(&line->tokens[start].span,
                                         &line->tokens[line->token_count - 1U].span);
    index = start;
    if (f2c_line_token_equals(line, index, "end")) {
        ++index;
        if (index == line->token_count)
            return F2C_UNIT_END_PARSED;
        if (!separated_end_kind(&line->tokens[index], &syntax->kind))
            return F2C_UNIT_END_NOT_MATCHED;
        syntax->has_kind = 1;
        syntax->kind_token = &line->tokens[index++];
    } else if (joined_end_kind(&line->tokens[index], &syntax->kind)) {
        syntax->has_kind = 1;
        syntax->kind_token = &line->tokens[index++];
    } else {
        return F2C_UNIT_END_NOT_MATCHED;
    }
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_IDENTIFIER)
        syntax->name = &line->tokens[index++];
    if (index != line->token_count) {
        syntax->error_token = &line->tokens[index];
        return F2C_UNIT_END_INVALID;
    }
    return F2C_UNIT_END_PARSED;
}
