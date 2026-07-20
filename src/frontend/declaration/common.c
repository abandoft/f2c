#include "frontend/declaration/private.h"

#include <stdlib.h>
#include <string.h>

static int slash_token(const F2cToken *token) {
    return token != NULL && token->kind == F2C_TOKEN_OPERATOR && f2c_token_equals(token, "/");
}

static int blank_block_token(const F2cToken *token) {
    return token != NULL && token->kind == F2C_TOKEN_OPERATOR && f2c_token_equals(token, "//");
}

static const char *common_storage_name(const char *block) {
    return block != NULL && block[0] == '\0' ? "f2c_blank_common" : NULL;
}

static size_t next_common_index(const Unit *unit, const char *block) {
    size_t index;
    size_t next = 0U;
    for (index = 0U; index < unit->symbol_count; ++index) {
        const Symbol *symbol = &unit->symbols[index];
        if (symbol->common_block != NULL && strcmp(symbol->common_block, block) == 0 &&
            symbol->common_index >= next)
            next = symbol->common_index == SIZE_MAX ? SIZE_MAX : symbol->common_index + 1U;
    }
    return next;
}

static char *parse_common_block(Context *context, const Line *line, size_t *index,
                                int allow_implicit_blank) {
    char *block;
    if (*index < line->token_count && blank_block_token(&line->tokens[*index])) {
        ++*index;
        return f2c_strdup("");
    }
    if (*index >= line->token_count || !slash_token(&line->tokens[*index])) {
        if (!allow_implicit_blank)
            return NULL;
        return f2c_strdup("");
    }
    ++*index;
    if (*index < line->token_count && slash_token(&line->tokens[*index])) {
        ++*index;
        return f2c_strdup("");
    }
    if (*index >= line->token_count || line->tokens[*index].kind != F2C_TOKEN_IDENTIFIER ||
        *index + 1U >= line->token_count || !slash_token(&line->tokens[*index + 1U])) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                  *index < line->token_count ? &line->tokens[*index]
                                                             : &line->tokens[*index - 1U],
                                  1, "malformed COMMON block name");
        return NULL;
    }
    block = f2c_token_text(&line->tokens[*index]);
    *index += 2U;
    return block;
}

static int assign_common_member(Context *context, Unit *unit, Line *line, size_t *index,
                                const char *block, size_t *member_index) {
    const F2cToken *name_token;
    char *name;
    Symbol *symbol;
    Buffer c_name = {0};
    if (*index >= line->token_count || line->tokens[*index].kind != F2C_TOKEN_IDENTIFIER ||
        *member_index == SIZE_MAX) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                  *index < line->token_count ? &line->tokens[*index]
                                                             : &line->tokens[*index - 1U],
                                  1, "malformed COMMON member");
        return 0;
    }
    name_token = &line->tokens[*index];
    name = f2c_token_text(name_token);
    ++*index;
    symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
    free(name);
    if (symbol == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, name_token, 1,
                                  "out of memory parsing COMMON member");
        return 0;
    }
    if (symbol->common_block != NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, name_token, 1,
                                  "COMMON entity '%s' is already in a COMMON block", symbol->name);
        return 0;
    }
    if (*index < line->token_count && line->tokens[*index].kind == F2C_TOKEN_LEFT_PAREN) {
        size_t close;
        if (!f2c_token_matching_delimiter(line->tokens, line->token_count, *index, &close)) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[*index],
                                      1, "malformed COMMON array declarator");
            return 0;
        }
        if (symbol->rank != 0U) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[*index],
                                      1, "duplicate array specification for '%s'", symbol->name);
        } else if (!f2c_parse_dimensions_tokens(context, unit, symbol, line, *index, close)) {
            return 0;
        }
        *index = close + 1U;
    }
    symbol->common_block = f2c_strdup(block);
    symbol->common_index = (*member_index)++;
    symbol->common_span = name_token->span;
    if (common_storage_name(block) != NULL)
        f2c_buffer_printf(&c_name, "%s.field_%zu", common_storage_name(block),
                          symbol->common_index);
    else
        f2c_buffer_printf(&c_name, "f2c_common_%s.field_%zu", block, symbol->common_index);
    free(symbol->c_name);
    symbol->c_name = f2c_buffer_take(&c_name);
    if (symbol->common_block == NULL || symbol->c_name == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, name_token, 1,
                                  "out of memory recording COMMON member");
        return 0;
    }
    return 1;
}

void f2c_parse_common_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = source_line != NULL && source_line->token_count > 1U &&
                           source_line->tokens[0].kind == F2C_TOKEN_NUMBER
                       ? 1U
                       : 0U;
    int first_block = 1;
    if (!f2c_line_token_equals(source_line, index, "common"))
        return;
    ++index;
    if (index >= source_line->token_count) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                  &source_line->tokens[index - 1U], 1,
                                  "COMMON requires at least one entity");
        return;
    }
    while (index < source_line->token_count) {
        char *block;
        size_t member_index;
        size_t members = 0U;
        const size_t errors_before = context->result.error_count;
        if (source_line->tokens[index].kind == F2C_TOKEN_COMMA)
            ++index;
        if (index >= source_line->token_count) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      &source_line->tokens[index - 1U], 1,
                                      "COMMON has a trailing separator");
            return;
        }
        block = parse_common_block(context, source_line, &index, first_block);
        if (block == NULL) {
            if (context->result.error_count == errors_before)
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line,
                                          index < source_line->token_count
                                              ? &source_line->tokens[index]
                                              : &source_line->tokens[index - 1U],
                                          1, "out of memory parsing COMMON block");
            return;
        }
        first_block = 0;
        member_index = next_common_index(unit, block);
        while (index < source_line->token_count) {
            if (source_line->tokens[index].kind == F2C_TOKEN_COMMA) {
                ++index;
                if (index >= source_line->token_count) {
                    free(block);
                    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                              &source_line->tokens[index - 1U], 1,
                                              "COMMON has a trailing separator");
                    return;
                }
                if (slash_token(&source_line->tokens[index]) ||
                    blank_block_token(&source_line->tokens[index]))
                    break;
            } else if (slash_token(&source_line->tokens[index]) ||
                       blank_block_token(&source_line->tokens[index])) {
                break;
            } else if (members != 0U) {
                free(block);
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                          &source_line->tokens[index], 1,
                                          "COMMON members must be separated by commas");
                return;
            }
            if (!assign_common_member(context, unit, source_line, &index, block, &member_index)) {
                free(block);
                return;
            }
            ++members;
        }
        if (members == 0U) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      index < source_line->token_count
                                          ? &source_line->tokens[index]
                                          : &source_line->tokens[index - 1U],
                                      1, "COMMON block requires at least one entity");
            free(block);
            return;
        }
        free(block);
    }
}
