#include "ast/interface/specific.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t statement_start(const Line *line) {
    return line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
}

static int set_error(F2cInterfaceSpecificSyntax *syntax, F2cInterfaceSpecificError error,
                     const F2cToken *token) {
    syntax->error = error;
    syntax->error_token = token;
    return 0;
}

static unsigned char ascii_lower(unsigned char value) {
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z')
        return (unsigned char)(value - (unsigned char)'A' + (unsigned char)'a');
    return value;
}

static int names_equal(const F2cToken *left, const F2cToken *right) {
    size_t index;
    if (left->length != right->length)
        return 0;
    for (index = 0U; index < left->length; ++index) {
        if (ascii_lower((unsigned char)left->begin[index]) !=
            ascii_lower((unsigned char)right->begin[index]))
            return 0;
    }
    return 1;
}

static int append_name(F2cInterfaceSpecificSyntax *syntax, const F2cToken *name) {
    const F2cToken **replacement;
    size_t capacity;
    size_t index;
    for (index = 0U; index < syntax->name_count; ++index) {
        if (names_equal(syntax->names[index], name))
            return set_error(syntax, F2C_INTERFACE_SPECIFIC_ERROR_DUPLICATE_NAME, name);
    }
    if (syntax->name_count == syntax->name_capacity) {
        capacity = syntax->name_capacity == 0U ? 8U : syntax->name_capacity * 2U;
        if (capacity < syntax->name_capacity || capacity > SIZE_MAX / sizeof(*replacement))
            return -1;
        replacement = (const F2cToken **)realloc(syntax->names, capacity * sizeof(*replacement));
        if (replacement == NULL)
            return -1;
        syntax->names = replacement;
        syntax->name_capacity = capacity;
    }
    syntax->names[syntax->name_count++] = name;
    return 1;
}

F2cInterfaceSpecificStatus f2c_parse_interface_specific_syntax(const Line *line,
                                                               F2cInterfaceSpecificSyntax *syntax) {
    size_t index;
    size_t start;
    if (syntax == NULL)
        return F2C_INTERFACE_SPECIFIC_INVALID;
    memset(syntax, 0, sizeof(*syntax));
    if (line == NULL || line->token_count == 0U)
        return F2C_INTERFACE_SPECIFIC_NOT_MATCHED;
    start = statement_start(line);
    index = start;
    if (f2c_line_token_equals(line, index, "module"))
        syntax->module_keyword = &line->tokens[index++];
    if (!f2c_line_token_equals(line, index, "procedure"))
        return F2C_INTERFACE_SPECIFIC_NOT_MATCHED;
    syntax->procedure_keyword = &line->tokens[index++];
    if (syntax->module_keyword == NULL && index < line->token_count &&
        line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN)
        return F2C_INTERFACE_SPECIFIC_NOT_MATCHED;
    syntax->span = f2c_source_span_cover(&line->tokens[start].span,
                                         &line->tokens[line->token_count - 1U].span);
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON)
        syntax->double_colon = &line->tokens[index++];
    if (index == line->token_count) {
        set_error(syntax, F2C_INTERFACE_SPECIFIC_ERROR_EMPTY_LIST,
                  syntax->double_colon != NULL ? syntax->double_colon : syntax->procedure_keyword);
        return F2C_INTERFACE_SPECIFIC_INVALID;
    }
    while (index < line->token_count) {
        int appended;
        if (line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
            set_error(syntax, F2C_INTERFACE_SPECIFIC_ERROR_NAME, &line->tokens[index]);
            return F2C_INTERFACE_SPECIFIC_INVALID;
        }
        appended = append_name(syntax, &line->tokens[index]);
        if (appended < 0)
            return F2C_INTERFACE_SPECIFIC_NO_MEMORY;
        if (!appended)
            return F2C_INTERFACE_SPECIFIC_INVALID;
        ++index;
        if (index == line->token_count)
            return F2C_INTERFACE_SPECIFIC_PARSED;
        if (line->tokens[index].kind != F2C_TOKEN_COMMA) {
            set_error(syntax, F2C_INTERFACE_SPECIFIC_ERROR_SEPARATOR, &line->tokens[index]);
            return F2C_INTERFACE_SPECIFIC_INVALID;
        }
        ++index;
        if (index == line->token_count) {
            set_error(syntax, F2C_INTERFACE_SPECIFIC_ERROR_TRAILING_COMMA,
                      &line->tokens[index - 1U]);
            return F2C_INTERFACE_SPECIFIC_INVALID;
        }
    }
    return F2C_INTERFACE_SPECIFIC_PARSED;
}

void f2c_interface_specific_syntax_discard(F2cInterfaceSpecificSyntax *syntax) {
    if (syntax == NULL)
        return;
    free(syntax->names);
    memset(syntax, 0, sizeof(*syntax));
}
