#include "frontend/private.h"

#include <stdlib.h>
#include <string.h>

int f2c_unit_line_is_active(const Unit *unit, const Line *line) {
    return unit != NULL && line != NULL && (unit->interface_body || line->interface_depth == 0U);
}

static int units_push(Units *units, Unit unit) {
    Unit *replacement;
    size_t capacity;
    if (units->count == units->capacity) {
        capacity = units->capacity == 0U ? 8U : units->capacity * 2U;
        replacement = (Unit *)realloc(units->items, capacity * sizeof(*replacement));
        if (replacement == NULL) {
            return 0;
        }
        units->items = replacement;
        units->capacity = capacity;
    }
    units->items[units->count++] = unit;
    return 1;
}

int f2c_discover_units(Context *context) {
    size_t i;
    size_t active_index = (size_t)-1;
    size_t host_index = (size_t)-1;
    size_t interface_depth = 0U;
    size_t derived_type_depth = 0U;
    for (i = 0U; i < context->lines.count; ++i) {
        Unit unit;
        Line *line = &context->lines.items[i];
        if (active_index == (size_t)-1 && host_index == (size_t)-1) {
            size_t module_index;
            int in_module_specification = 0;
            for (module_index = 0U; module_index < context->modules.count; ++module_index) {
                Unit *module = &context->modules.items[module_index];
                if (i >= module->begin && i <= module->end) {
                    in_module_specification = 1;
                    break;
                }
            }
            if (in_module_specification)
                continue;
        }
        if (active_index != (size_t)-1) {
            Unit *active = &context->units.items[active_index];
            if (f2c_interface_start_tokens(line)) {
                ++interface_depth;
                context->lines.items[i].interface_depth = interface_depth;
                continue;
            }
            if (interface_depth != 0U) {
                context->lines.items[i].interface_depth = interface_depth;
                if (f2c_interface_end_tokens(line))
                    --interface_depth;
                continue;
            }
            if (derived_type_depth != 0U && f2c_derived_type_end_tokens(line)) {
                --derived_type_depth;
                continue;
            }
            if (f2c_derived_type_start_tokens(line)) {
                ++derived_type_depth;
                continue;
            }
            if (derived_type_depth != 0U)
                continue;
            if (!active->internal && f2c_contains_tokens(line)) {
                active->end = i;
                host_index = active_index;
                active_index = (size_t)-1;
            } else if (f2c_program_unit_end_tokens(line, active->kind)) {
                active->end = i;
                active_index = (size_t)-1;
            }
            continue;
        }
        if (host_index != (size_t)-1) {
            if (f2c_parse_unit_header_tokens(line, &unit)) {
                Buffer mangled = {0};
                char *source_name = unit.name;
                f2c_buffer_printf(&mangled, "%s__%s", context->units.items[host_index].name,
                                  source_name);
                unit.name = f2c_buffer_take(&mangled);
                unit.fortran_name = source_name;
                unit.internal = 1;
                unit.context = context;
                unit.host_index = host_index;
                unit.begin = i;
                unit.end = context->lines.count;
                unit.options.source_name = f2c_strdup(context->lines.items[i].source_name);
                unit.options.source_form = F2C_SOURCE_AUTO;
                unit.options.emit_source_comments = context->lines.items[i].emit_source_comments;
                if (unit.name == NULL || unit.options.source_name == NULL)
                    return 0;
                if (!units_push(&context->units, unit)) {
                    free(unit.name);
                    free(unit.fortran_name);
                    free((char *)unit.options.source_name);
                    return 0;
                }
                active_index = context->units.count - 1U;
                interface_depth = 0U;
                derived_type_depth = 0U;
            } else if (f2c_program_unit_end_tokens(line, context->units.items[host_index].kind)) {
                host_index = (size_t)-1;
            }
            continue;
        }
        if (f2c_parse_unit_header_tokens(line, &unit)) {
            size_t module_index;
            Unit *containing_module = NULL;
            for (module_index = 0U; module_index < context->modules.count; ++module_index) {
                Unit *module = &context->modules.items[module_index];
                if (i > module->end && i < module->container_end) {
                    containing_module = module;
                    break;
                }
            }
            if (containing_module != NULL) {
                Buffer mangled = {0};
                char *source_name = unit.name;
                f2c_buffer_printf(&mangled, "f2c_module_%s_%s", containing_module->name,
                                  source_name);
                unit.name = f2c_buffer_take(&mangled);
                unit.fortran_name = source_name;
                if (unit.name == NULL) {
                    free(unit.fortran_name);
                    return 0;
                }
            }
            unit.begin = i;
            unit.context = context;
            unit.end = context->lines.count;
            unit.host_index = (size_t)-1;
            unit.options.source_name = f2c_strdup(context->lines.items[i].source_name);
            unit.options.source_form = F2C_SOURCE_AUTO;
            unit.options.emit_source_comments = context->lines.items[i].emit_source_comments;
            if (unit.options.source_name == NULL) {
                free(unit.name);
                free(unit.fortran_name);
                return 0;
            }
            if (!units_push(&context->units, unit)) {
                free(unit.name);
                free(unit.fortran_name);
                free((char *)unit.options.source_name);
                return 0;
            }
            active_index = context->units.count - 1U;
            interface_depth = 0U;
            derived_type_depth = 0U;
        }
    }
    if (active_index != (size_t)-1) {
        Unit *active = &context->units.items[active_index];
        f2c_diagnostic(context, context->lines.items[active->begin].number, 1,
                       "unterminated program unit '%s'", active->name);
    } else if (host_index != (size_t)-1) {
        Unit *host = &context->units.items[host_index];
        f2c_diagnostic(context, context->lines.items[host->begin].number, 1,
                       "unterminated host program unit '%s'", host->name);
    }
    if (context->units.count == 0U && !f2c_has_supported_module(context)) {
        f2c_diagnostic(context, context->lines.count == 0U ? 1U : context->lines.items[0].number, 1,
                       "no PROGRAM, SUBROUTINE, or FUNCTION unit found");
    }
    return 1;
}

Symbol *f2c_find_symbol(Unit *unit, const char *name) {
    size_t i;
    if (unit == NULL || name == NULL)
        return NULL;
    for (i = 0U; i < unit->symbol_count; ++i) {
        if (strcmp(unit->symbols[i].name, name) == 0) {
            return &unit->symbols[i];
        }
    }
    return NULL;
}

F2cNamelistGroup *f2c_find_namelist(Unit *unit, const char *name) {
    size_t i;
    if (unit == NULL || name == NULL)
        return NULL;
    for (i = 0U; i < unit->namelist_count; ++i) {
        if (strcmp(unit->namelists[i].name, name) == 0)
            return &unit->namelists[i];
    }
    return NULL;
}

F2cDerivedType *f2c_find_derived_type(Unit *unit, const char *name) {
    size_t i;
    if (unit == NULL || name == NULL)
        return NULL;
    for (i = 0U; i < unit->derived_type_count; ++i) {
        if (strcmp(unit->derived_types[i].name, name) == 0)
            return &unit->derived_types[i];
    }
    for (i = 0U; i < unit->imported_derived_type_count; ++i) {
        if (strcmp(unit->imported_derived_types[i]->name, name) == 0)
            return unit->imported_derived_types[i];
    }
    return NULL;
}

Symbol *f2c_ensure_symbol_impl(Unit *unit, const char *name) {
    Symbol *symbol;
    Symbol *replacement;
    size_t capacity;
    if (name == NULL || name[0] == '\0')
        return NULL;
    symbol = f2c_find_symbol(unit, name);
    if (symbol != NULL) {
        return symbol;
    }
    if (unit->symbol_count == unit->symbol_capacity) {
        capacity = unit->symbol_capacity == 0U ? 16U : unit->symbol_capacity * 2U;
        replacement = (Symbol *)realloc(unit->symbols, capacity * sizeof(*replacement));
        if (replacement == NULL) {
            return NULL;
        }
        unit->symbols = replacement;
        unit->symbol_capacity = capacity;
    }
    symbol = &unit->symbols[unit->symbol_count++];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = f2c_strdup(name);
    {
        static const char *const keywords[] = {
            "auto",       "break",     "case",           "char",
            "const",      "continue",  "default",        "do",
            "double",     "else",      "enum",           "extern",
            "float",      "for",       "goto",           "if",
            "inline",     "int",       "long",           "register",
            "restrict",   "return",    "short",          "signed",
            "sizeof",     "static",    "struct",         "switch",
            "typedef",    "union",     "unsigned",       "void",
            "volatile",   "while",     "_alignas",       "_alignof",
            "_atomic",    "_bool",     "_complex",       "_generic",
            "_imaginary", "_noreturn", "_static_assert", "_thread_local",
            "abs",        "abort",     "calloc",         "cabs",
            "cabsf",      "cexp",      "cexpf",          "clog",
            "clogf",      "conj",      "conjf",          "cos",
            "cosf",       "csin",      "csinf",          "exit",
            "exp",        "expf",      "fabs",           "fabsf",
            "fclose",     "fopen",     "fprintf",        "free",
            "fscanf",     "log",       "logf",           "malloc",
            "memcpy",     "memmove",   "memset",         "pow",
            "powf",       "sin",       "sinf",           "sqrt",
            "sqrtf",      "strtod",    "strtof",         "strtol"};
        size_t keyword;
        int reserved = 0;
        for (keyword = 0U; keyword < sizeof(keywords) / sizeof(keywords[0]); ++keyword) {
            if (strcmp(name, keywords[keyword]) == 0) {
                reserved = 1;
                break;
            }
        }
        if (reserved) {
            Buffer c_name = {0};
            f2c_buffer_printf(&c_name, "%s_value", name);
            symbol->c_name = f2c_buffer_take(&c_name);
        } else {
            symbol->c_name = f2c_strdup(name);
        }
    }
    symbol->type = TYPE_UNKNOWN;
    symbol->value_category = F2C_VALUE_VARIABLE;
    symbol->shape.kind = F2C_SHAPE_SCALAR;
    return symbol->name == NULL || symbol->c_name == NULL ? NULL : symbol;
}

Symbol *f2c_ensure_symbol(Unit *unit, const char *name) {
    return f2c_ensure_symbol_impl(unit, name);
}

const char *f2c_symbol_c_name(Unit *unit, const Symbol *symbol) {
    (void)unit;
    return symbol->c_name;
}
