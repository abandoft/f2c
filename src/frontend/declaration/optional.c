#include "frontend/declaration/private.h"

#include <stdlib.h>

void f2c_parse_optional_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = source_line != NULL && source_line->token_count > 1U &&
                           source_line->tokens[0].kind == F2C_TOKEN_NUMBER
                       ? 1U
                       : 0U;
    if (!f2c_line_token_equals(source_line, index, "optional"))
        return;
    ++index;
    if (index < source_line->token_count &&
        source_line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON)
        ++index;
    if (index == source_line->token_count) {
        f2c_diagnostic(context, source_line->number, 1, "OPTIONAL declaration has no entities");
        return;
    }
    while (index < source_line->token_count) {
        char *name;
        Symbol *symbol;
        if (source_line->tokens[index].kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      &source_line->tokens[index], 1,
                                      "malformed OPTIONAL declaration entity");
            return;
        }
        name = f2c_token_text(&source_line->tokens[index++]);
        symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
        if (symbol == NULL) {
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line->number, 1,
                                "out of memory in OPTIONAL declaration");
            free(name);
            return;
        }
        symbol->optional = 1;
        symbol->declaration_line = source_line->number;
        free(name);
        if (index == source_line->token_count)
            break;
        if (source_line->tokens[index].kind != F2C_TOKEN_COMMA ||
            ++index == source_line->token_count) {
            f2c_diagnostic(context, source_line->number, 1,
                           "malformed OPTIONAL declaration entity list");
            return;
        }
    }
}
