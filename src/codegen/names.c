#include "codegen/names.h"

#include <stdlib.h>
#include <string.h>

static int name_conflicts(Unit *unit, const char *name) {
    size_t index;
    for (index = 0U; index < unit->symbol_count; ++index) {
        if (strcmp(f2c_symbol_c_name(unit, &unit->symbols[index]), name) == 0)
            return 1;
    }
    if (unit->context != NULL) {
        for (index = 0U; index < unit->context->units.count; ++index) {
            const Unit *procedure = &unit->context->units.items[index];
            if (procedure->name != NULL && strcmp(procedure->name, name) == 0)
                return 1;
        }
    }
    return 0;
}

char *f2c_codegen_local_name(Unit *unit, const char *base) {
    size_t suffix = 0U;
    do {
        Buffer name = {0};
        char *result;
        f2c_buffer_printf(&name, "%s_%zu", base, suffix);
        result = f2c_buffer_take(&name);
        if (result == NULL || !name_conflicts(unit, result))
            return result;
        free(result);
        if (suffix == SIZE_MAX)
            return NULL;
        ++suffix;
    } while (1);
}
