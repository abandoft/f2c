#include "frontend/declaration/private.h"

#include <stdlib.h>
#include <string.h>

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

void f2c_parse_common_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = source_line != NULL && source_line->token_count > 1U &&
                           source_line->tokens[0].kind == F2C_TOKEN_NUMBER
                       ? 1U
                       : 0U;
    if (!f2c_line_token_equals(source_line, index, "common"))
        return;
    ++index;
    if (index >= source_line->token_count ||
        source_line->tokens[index].kind != F2C_TOKEN_OPERATOR ||
        !f2c_token_equals(&source_line->tokens[index], "/")) {
        f2c_diagnostic(context, source_line->number, 1, "blank COMMON is not yet supported");
        return;
    }
    while (index < source_line->token_count) {
        char *block;
        size_t member_index;
        if (source_line->tokens[index].kind == F2C_TOKEN_COMMA)
            ++index;
        if (index + 2U >= source_line->token_count ||
            source_line->tokens[index].kind != F2C_TOKEN_OPERATOR ||
            !f2c_token_equals(&source_line->tokens[index], "/") ||
            source_line->tokens[index + 1U].kind != F2C_TOKEN_IDENTIFIER ||
            source_line->tokens[index + 2U].kind != F2C_TOKEN_OPERATOR ||
            !f2c_token_equals(&source_line->tokens[index + 2U], "/")) {
            f2c_diagnostic(context, source_line->number, 1, "malformed COMMON block name");
            return;
        }
        block = f2c_token_text(&source_line->tokens[index + 1U]);
        if (block == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory parsing COMMON");
            return;
        }
        member_index = next_common_index(unit, block);
        index += 3U;
        while (index < source_line->token_count) {
            char *name;
            Symbol *symbol;
            Buffer c_name = {0};
            if (source_line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
                f2c_token_equals(&source_line->tokens[index], "/"))
                break;
            if (source_line->tokens[index].kind == F2C_TOKEN_COMMA) {
                ++index;
                if (index < source_line->token_count &&
                    source_line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
                    f2c_token_equals(&source_line->tokens[index], "/"))
                    break;
            }
            if (index >= source_line->token_count ||
                source_line->tokens[index].kind != F2C_TOKEN_IDENTIFIER ||
                member_index == SIZE_MAX) {
                free(block);
                f2c_diagnostic(context, source_line->number, 1, "malformed COMMON member");
                return;
            }
            name = f2c_token_text(&source_line->tokens[index++]);
            symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
            if (symbol == NULL) {
                free(name);
                free(block);
                f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                    "out of memory parsing COMMON member");
                return;
            }
            if (index < source_line->token_count &&
                source_line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN) {
                size_t close;
                if (!f2c_token_matching_delimiter(source_line->tokens, source_line->token_count,
                                                  index, &close)) {
                    free(name);
                    free(block);
                    f2c_diagnostic(context, source_line->number, 1,
                                   "malformed COMMON array declarator");
                    return;
                }
                if (symbol->rank != 0U) {
                    f2c_diagnostic_token_code(
                        context, F2C_DIAGNOSTIC_SEMANTIC, source_line, &source_line->tokens[index],
                        1, "duplicate array specification for '%s'", symbol->name);
                } else {
                    (void)f2c_parse_dimensions_tokens(context, unit, symbol, source_line, index,
                                                      close);
                }
                index = close + 1U;
            }
            free(symbol->common_block);
            symbol->common_block = f2c_strdup(block);
            symbol->common_index = member_index++;
            f2c_buffer_printf(&c_name, "f2c_common_%s.field_%zu", block, symbol->common_index);
            free(symbol->c_name);
            symbol->c_name = f2c_buffer_take(&c_name);
            free(name);
            if (symbol->common_block == NULL || symbol->c_name == NULL) {
                free(block);
                f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                    "out of memory recording COMMON member");
                return;
            }
            if (index < source_line->token_count &&
                source_line->tokens[index].kind != F2C_TOKEN_COMMA &&
                !(source_line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
                  f2c_token_equals(&source_line->tokens[index], "/"))) {
                free(block);
                f2c_diagnostic(context, source_line->number, 1, "malformed COMMON member list");
                return;
            }
        }
        free(block);
    }
}
