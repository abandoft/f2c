#include "ast/interface/header.h"

#include <string.h>

static size_t statement_start(const Line *line) {
    return line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
}

static F2cSourceSpan statement_span(const Line *line, size_t start) {
    return f2c_source_span_cover(&line->tokens[start].span,
                                 &line->tokens[line->token_count - 1U].span);
}

F2cInterfaceHeaderStatus f2c_parse_interface_header_syntax(const Line *line,
                                                           F2cInterfaceHeaderSyntax *syntax) {
    size_t index;
    size_t start;
    F2cGenericDesignatorStatus generic_status;
    if (syntax == NULL)
        return F2C_INTERFACE_HEADER_INVALID;
    memset(syntax, 0, sizeof(*syntax));
    if (line == NULL || line->token_count == 0U)
        return F2C_INTERFACE_HEADER_NOT_MATCHED;
    start = statement_start(line);
    index = start;
    if (f2c_line_token_equals(line, index, "abstract"))
        syntax->abstract_keyword = &line->tokens[index++];
    if (!f2c_line_token_equals(line, index, "interface"))
        return F2C_INTERFACE_HEADER_NOT_MATCHED;
    syntax->interface_keyword = &line->tokens[index++];
    syntax->span = statement_span(line, start);
    if (index == line->token_count)
        return F2C_INTERFACE_HEADER_PARSED;
    generic_status = f2c_parse_generic_designator_syntax(line, index, &index, &syntax->generic);
    if (generic_status != F2C_GENERIC_DESIGNATOR_PARSED) {
        syntax->error = F2C_INTERFACE_HEADER_ERROR_GENERIC;
        syntax->error_token = &line->tokens[index];
        return F2C_INTERFACE_HEADER_INVALID;
    }
    syntax->has_generic = 1;
    if (syntax->abstract_keyword != NULL) {
        syntax->error = F2C_INTERFACE_HEADER_ERROR_ABSTRACT_GENERIC;
        syntax->error_token = syntax->generic.range.tokens;
        return F2C_INTERFACE_HEADER_INVALID;
    }
    if (index != line->token_count) {
        syntax->error = F2C_INTERFACE_HEADER_ERROR_TRAILING_TOKEN;
        syntax->error_token = &line->tokens[index];
        return F2C_INTERFACE_HEADER_INVALID;
    }
    return F2C_INTERFACE_HEADER_PARSED;
}

F2cInterfaceHeaderStatus f2c_parse_end_interface_syntax(const Line *line,
                                                        F2cEndInterfaceSyntax *syntax) {
    size_t index;
    size_t start;
    F2cGenericDesignatorStatus generic_status;
    if (syntax == NULL)
        return F2C_INTERFACE_HEADER_INVALID;
    memset(syntax, 0, sizeof(*syntax));
    if (line == NULL || line->token_count == 0U)
        return F2C_INTERFACE_HEADER_NOT_MATCHED;
    start = statement_start(line);
    index = start;
    if (f2c_line_token_equals(line, index, "endinterface")) {
        syntax->end_keyword = &line->tokens[index];
        syntax->interface_keyword = &line->tokens[index++];
    } else {
        if (!f2c_line_token_equals(line, index, "end") ||
            !f2c_line_token_equals(line, index + 1U, "interface"))
            return F2C_INTERFACE_HEADER_NOT_MATCHED;
        syntax->end_keyword = &line->tokens[index++];
        syntax->interface_keyword = &line->tokens[index++];
    }
    syntax->span = statement_span(line, start);
    if (index == line->token_count)
        return F2C_INTERFACE_HEADER_PARSED;
    generic_status = f2c_parse_generic_designator_syntax(line, index, &index, &syntax->generic);
    if (generic_status != F2C_GENERIC_DESIGNATOR_PARSED) {
        syntax->error = F2C_INTERFACE_HEADER_ERROR_GENERIC;
        syntax->error_token = &line->tokens[index];
        return F2C_INTERFACE_HEADER_INVALID;
    }
    syntax->has_generic = 1;
    if (index != line->token_count) {
        syntax->error = F2C_INTERFACE_HEADER_ERROR_TRAILING_TOKEN;
        syntax->error_token = &line->tokens[index];
        return F2C_INTERFACE_HEADER_INVALID;
    }
    return F2C_INTERFACE_HEADER_PARSED;
}
