#include "frontend/declaration/symbol.h"

#include "ast/declaration/bindings.h"
#include "frontend/frontend.h"

#include <stdlib.h>

int f2c_predeclare_local_bindings(Context *context, Unit *unit) {
    size_t line_index;
    int in_derived_type = 0;
    for (line_index = 0U; line_index < unit->argument_count; ++line_index) {
        Symbol *argument = f2c_ensure_symbol(unit, unit->arguments[line_index]);
        if (argument == NULL)
            return 0;
        argument->argument = 1;
    }
    if (unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
        f2c_ensure_symbol(unit, unit->result_name) == NULL)
        return 0;
    for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
        const Line *line = &context->lines.items[line_index];
        F2cDeclarationBindingsSyntax syntax;
        size_t name_index;
        int status;
        if (!f2c_unit_line_is_active(unit, line))
            continue;
        if (f2c_derived_type_start_tokens(line)) {
            in_derived_type = 1;
            continue;
        }
        if (in_derived_type) {
            if (f2c_derived_type_end_tokens(line))
                in_derived_type = 0;
            continue;
        }
        if (f2c_line_in_derived_type(unit, line_index))
            continue;
        status = f2c_parse_declaration_bindings_syntax(line, &syntax);
        if (status < 0) {
            f2c_declaration_bindings_syntax_discard(&syntax);
            return 0;
        }
        for (name_index = 0U; name_index < syntax.count; ++name_index) {
            char *name = f2c_token_text(syntax.names[name_index]);
            Symbol *symbol = name != NULL ? f2c_ensure_symbol(unit, name) : NULL;
            free(name);
            if (symbol == NULL) {
                f2c_declaration_bindings_syntax_discard(&syntax);
                return 0;
            }
        }
        f2c_declaration_bindings_syntax_discard(&syntax);
    }
    return 1;
}
