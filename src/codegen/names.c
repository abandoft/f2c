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

static int family_conflicts(Unit *unit, const char *name, const char *const *suffixes,
                            size_t count) {
    if (name_conflicts(unit, name))
        return 1;
    for (size_t index = 0U; index < count; ++index) {
        Buffer member = {0};
        f2c_buffer_printf(&member, "%s_%s", name, suffixes[index]);
        if (member.data == NULL || member.failed) {
            free(member.data);
            return -1;
        }
        const int conflict = name_conflicts(unit, member.data);
        free(member.data);
        if (conflict)
            return 1;
    }
    return 0;
}

char *f2c_codegen_local_family(Unit *unit, const char *preferred, const char *const *suffixes,
                               size_t count) {
    if (unit == NULL || preferred == NULL || (count != 0U && suffixes == NULL))
        return NULL;
    for (size_t index = 0U; index < count; ++index)
        if (suffixes[index] == NULL)
            return NULL;
    size_t suffix = 0U;
    char *candidate = f2c_strdup(preferred);
    while (candidate != NULL) {
        const int conflict = family_conflicts(unit, candidate, suffixes, count);
        if (conflict == 0)
            return candidate;
        free(candidate);
        if (conflict < 0 || suffix == SIZE_MAX)
            return NULL;
        Buffer name = {0};
        f2c_buffer_printf(&name, "%s_%zu", preferred, suffix++);
        candidate = f2c_buffer_take(&name);
    }
    return NULL;
}
