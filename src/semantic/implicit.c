#include "internal/f2c.h"
#include "semantic/implicit/private.h"

#include <ctype.h>

void f2c_prepare_implicit_map(Context *context, Unit *unit) {
    size_t letter;
    size_t line_index;
    if (unit->implicit_map_initialized)
        return;
    for (letter = 0U; letter < 26U; ++letter) {
        unit->implicit_types[letter] =
            letter >= (size_t)('i' - 'a') && letter <= (size_t)('n' - 'a') ? TYPE_INTEGER
                                                                           : TYPE_REAL;
        unit->implicit_kinds[letter] = f2c_default_kind(unit->implicit_types[letter]);
    }
    unit->implicit_map_initialized = 1;
    for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
        if (f2c_unit_line_is_active(unit, &context->lines.items[line_index]))
            f2c_parse_implicit_statement(context, unit, &context->lines.items[line_index]);
    }
}

void f2c_discover_implicit_symbols(Context *context, Unit *unit) {
    size_t line_index;
    for (line_index = unit->begin + 1U; line_index < unit->end; ++line_index) {
        if (f2c_unit_line_is_active(unit, &context->lines.items[line_index]))
            f2c_discover_implicit_line_symbols(context, unit, &context->lines.items[line_index]);
    }
}

static int initial_letter(const char *name) {
    int first;
    if (name == NULL || name[0] == '\0')
        return -1;
    first = tolower((unsigned char)name[0]);
    return first >= 'a' && first <= 'z' ? first - 'a' : -1;
}

Type f2c_implicit_type_for_name(const Unit *unit, const char *name) {
    const int letter = initial_letter(name);
    return unit != NULL && !unit->implicit_none && letter >= 0 ? unit->implicit_types[letter]
                                                               : TYPE_UNKNOWN;
}

int f2c_implicit_kind_for_name(const Unit *unit, const char *name) {
    const int letter = initial_letter(name);
    return unit != NULL && !unit->implicit_none && letter >= 0 ? unit->implicit_kinds[letter] : 0;
}

const char *f2c_implicit_character_length_for_name(const Unit *unit, const char *name) {
    const int letter = initial_letter(name);
    return unit != NULL && letter >= 0 ? unit->implicit_character_lengths[letter] : NULL;
}

F2cTokenRange f2c_implicit_character_length_syntax_for_name(const Unit *unit, const char *name) {
    const int letter = initial_letter(name);
    const F2cTokenRange empty = {0};
    return unit != NULL && letter >= 0 ? unit->implicit_character_length_syntax[letter] : empty;
}

void f2c_validate_implicit_external(Context *context, Unit *unit) {
    size_t symbol_index;
    if (!unit->implicit_none_external)
        return;
    for (symbol_index = 0U; symbol_index < unit->symbol_count; ++symbol_index) {
        const Symbol *symbol = &unit->symbols[symbol_index];
        if (symbol->external && !symbol->external_declared) {
            f2c_diagnostic(context,
                           symbol->first_seen_line != 0U ? symbol->first_seen_line
                                                         : context->lines.items[unit->begin].number,
                           1,
                           "procedure '%s' requires an EXTERNAL declaration or explicit "
                           "interface under IMPLICIT NONE(EXTERNAL)",
                           symbol->name);
        }
    }
}
