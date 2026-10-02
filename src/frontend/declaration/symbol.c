#include "frontend/declaration/symbol.h"

#include "frontend/frontend.h"
#include "semantic/symbol.h"

#include <stdlib.h>
#include <string.h>

int f2c_reset_associated_symbol(Unit *unit, Symbol *symbol) {
    Unit temporary = {0};
    Symbol *replacement;
    size_t index;
    int argument = 0;
    /* Hidden HOST ABI parameters are not source-level dummy declarations. */
    for (index = 0U; index < unit->argument_count; ++index)
        if (strcmp(unit->arguments[index], symbol->name) == 0)
            argument = 1;
    replacement = f2c_ensure_symbol(&temporary, symbol->name);
    if (replacement == NULL) {
        for (index = 0U; index < temporary.symbol_count; ++index)
            f2c_discard_symbol(&temporary.symbols[index]);
        free(temporary.symbols);
        return 0;
    }
    f2c_discard_symbol(symbol);
    *symbol = *replacement;
    symbol->argument = argument;
    symbol->declaration_scope_id =
        unit->context != NULL && unit->begin != SIZE_MAX ? unit->begin + 1U : 0U;
    free(temporary.symbols);
    return 1;
}

Symbol *f2c_declaration_symbol(Context *context, Unit *unit, const Line *line,
                               const F2cToken *name_token) {
    char *name = f2c_token_text(name_token);
    Symbol *symbol = name != NULL ? f2c_find_symbol(unit, name) : NULL;
    if (symbol != NULL && symbol->association == F2C_ASSOCIATION_USE) {
        f2c_diagnostic_token_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, line, name_token, 1,
            "local declaration of '%s' conflicts with a USE-associated entity", name);
        free(name);
        return NULL;
    }
    if (symbol != NULL && symbol->association == F2C_ASSOCIATION_HOST &&
        !f2c_reset_associated_symbol(unit, symbol))
        symbol = NULL;
    else if (symbol == NULL && name != NULL)
        symbol = f2c_ensure_symbol(unit, name);
    if (symbol == NULL)
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, name_token, 1,
                                  "out of memory recording local declaration");
    free(name);
    return symbol;
}
