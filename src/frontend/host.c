#include "frontend/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int is_host_internal_procedure(const Context *context, const Unit *unit,
                                      const Symbol *symbol) {
    size_t unit_index;
    if (context == NULL || unit == NULL || symbol == NULL || symbol->name == NULL)
        return 0;
    for (unit_index = 0U; unit_index < context->units.count; ++unit_index) {
        const Unit *candidate = &context->units.items[unit_index];
        if (candidate->internal && candidate->host_index == unit->host_index &&
            candidate->fortran_name != NULL && strcmp(candidate->fortran_name, symbol->name) == 0)
            return 1;
    }
    return 0;
}

static int requires_host_capture(const Symbol *symbol) {
    return symbol != NULL && !symbol->parameter && !symbol->module_entity &&
           !symbol->use_associated && (!symbol->external || symbol->procedure_pointer);
}

static char *make_capture_alias(Unit *unit, size_t host_symbol_index) {
    size_t attempt;
    /* '@' cannot occur in a Fortran identifier, including one discovered implicitly later. */
    for (attempt = 0U; attempt != SIZE_MAX; ++attempt) {
        Buffer name = {0};
        char *candidate;
        f2c_buffer_printf(&name, "@f2c_host_capture_%zu", host_symbol_index);
        if (attempt != 0U)
            f2c_buffer_printf(&name, "_%zu", attempt);
        candidate = f2c_buffer_take(&name);
        if (candidate == NULL)
            return NULL;
        if (f2c_find_symbol(unit, candidate) == NULL)
            return candidate;
        free(candidate);
    }
    return NULL;
}

int f2c_import_host_symbols(Context *context, Unit *unit) {
    Unit *host;
    size_t symbol_index;
    if (context == NULL || unit == NULL)
        return 0;
    if (!unit->internal)
        return 1;
    if (unit->host_index >= context->units.count)
        return 0;
    host = &context->units.items[unit->host_index];
    for (symbol_index = 0U; symbol_index < host->symbol_count; ++symbol_index) {
        const Symbol *source = &host->symbols[symbol_index];
        Symbol *target;
        const char *local_name;
        char *alias = NULL;
        int shadowed;
        int imported;
        if (source->name == NULL || (source->external && !source->procedure_pointer &&
                                     is_host_internal_procedure(context, unit, source)))
            continue;
        shadowed = f2c_find_symbol(unit, source->name) != NULL ||
                   (unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
                    strcmp(unit->result_name, source->name) == 0);
        if (shadowed && !requires_host_capture(source))
            continue;
        if (shadowed) {
            alias = make_capture_alias(unit, symbol_index);
            if (alias == NULL)
                return 0;
        }
        local_name = alias != NULL ? alias : source->name;
        imported = f2c_clone_associated_symbol(unit, source, local_name);
        if (imported <= 0) {
            free(alias);
            return 0;
        }
        target = f2c_find_symbol(unit, local_name);
        if (target == NULL) {
            free(alias);
            return 0;
        }
        if (alias != NULL) {
            char *c_name = f2c_strdup(alias + 1);
            if (c_name == NULL) {
                free(alias);
                return 0;
            }
            free(target->c_name);
            target->c_name = c_name;
        }
        free(alias);
        if (source->parameter || source->module_entity || source->use_associated)
            continue;
        if (source->external && !source->procedure_pointer)
            continue;
        target->module_entity = 0;
        target->use_associated = 0;
        target->argument = 1;
        target->host_associated = 1;
        target->host_capture = 0;
        target->host_symbol_index = symbol_index;
        target->saved = 0;
        target->initializer_syntax.count = 0U;
        f2c_expr_free(target->initializer_expression);
        target->initializer_expression = NULL;
        free(target->initializer);
        target->initializer = NULL;
        if (source->procedure_pointer)
            continue;
        if (target->type == TYPE_CHARACTER && !target->deferred_character) {
            char *assumed_length = f2c_strdup("*");
            if (assumed_length == NULL)
                return 0;
            free(target->character_length);
            target->character_length = assumed_length;
            memset(&target->character_length_syntax, 0, sizeof(target->character_length_syntax));
            f2c_expr_free(target->character_length_expression);
            target->character_length_expression = NULL;
            target->automatic_character = 0;
        }
    }
    return 1;
}
