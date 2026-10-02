#include "semantic/scope.h"

#include "internal/context.h"

static Unit *find_scope(Unit *unit, size_t identity) {
    size_t index;
    if (unit->begin != SIZE_MAX && unit->begin + 1U == identity)
        return unit;
    for (index = 0U; index < unit->interface_count; ++index) {
        Unit *found = find_scope(&unit->interfaces[index], identity);
        if (found != NULL)
            return found;
    }
    return NULL;
}

Unit *f2c_symbol_declaration_scope(Unit *current, const Symbol *symbol) {
    Context *context;
    size_t index;
    Unit *found;
    if (current == NULL || symbol == NULL || symbol->declaration_scope_id == 0U)
        return current;
    found = find_scope(current, symbol->declaration_scope_id);
    if (found != NULL)
        return found;
    context = current->context;
    if (context == NULL)
        return current;
    for (index = 0U; index < context->modules.count; ++index) {
        found = find_scope(&context->modules.items[index], symbol->declaration_scope_id);
        if (found != NULL)
            return found;
    }
    for (index = 0U; index < context->units.count; ++index) {
        found = find_scope(&context->units.items[index], symbol->declaration_scope_id);
        if (found != NULL)
            return found;
    }
    /* Interface/component staging scopes can precede their registration. */
    for (found = current->signature_host; found != NULL; found = found->signature_host)
        if (found->begin != SIZE_MAX && found->begin + 1U == symbol->declaration_scope_id)
            return found;
    return NULL;
}

Unit *f2c_symbol_specification_scope(Unit *current, const Symbol *symbol) {
    if (symbol == NULL || (!symbol->parameter && !symbol->module_entity))
        return current;
    return f2c_symbol_declaration_scope(current, symbol);
}
