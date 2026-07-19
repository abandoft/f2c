#include "frontend/module/access.h"

#include "ast/declaration/access.h"
#include "ast/declaration/use.h"
#include "frontend/frontend.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static F2cAccessibility lower_access(F2cAccessKind kind) {
    return kind == F2C_ACCESS_PUBLIC ? F2C_ACCESSIBILITY_PUBLIC : F2C_ACCESSIBILITY_PRIVATE;
}

static F2cAccessibility effective_access(const Unit *module, F2cAccessibility explicit_access) {
    if (explicit_access != F2C_ACCESS_UNSPECIFIED)
        return explicit_access;
    if (module != NULL && module->default_access == F2C_ACCESSIBILITY_PRIVATE)
        return F2C_ACCESSIBILITY_PRIVATE;
    return F2C_ACCESSIBILITY_PUBLIC;
}

static void report_syntax_error(Context *context, const Line *line,
                                const F2cAccessStatementSyntax *syntax) {
    const F2cToken *token = syntax->error_token != NULL ? syntax->error_token : syntax->keyword;
    const char *message = "malformed access statement";
    switch (syntax->error) {
    case F2C_ACCESS_ERROR_EMPTY_LIST:
        message = "access statement has '::' but no access identifiers";
        break;
    case F2C_ACCESS_ERROR_ITEM:
        message = "malformed access identifier";
        break;
    case F2C_ACCESS_ERROR_LIST_SEPARATOR:
        message = "access identifiers must be separated by commas";
        break;
    case F2C_ACCESS_ERROR_DUPLICATE_ITEM:
        message = "duplicate identifier in access statement";
        break;
    case F2C_ACCESS_ERROR_TRAILING_COMMA:
        message = "access identifier list cannot end with a comma";
        break;
    case F2C_ACCESS_ERROR_NONE:
        break;
    }
    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, token, 1, "%s", message);
}

static F2cModuleAccessEntry *find_access_entry(Unit *module, const char *key) {
    size_t index;
    for (index = 0U; index < module->access_entry_count; ++index) {
        if (strcmp(module->access_entries[index].key, key) == 0)
            return &module->access_entries[index];
    }
    return NULL;
}

static int append_access_entry(Context *context, Unit *module, const Line *line,
                               const F2cGenericDesignatorSyntax *item, F2cAccessibility access) {
    F2cModuleAccessEntry *replacement;
    F2cModuleAccessEntry *entry;
    char *key = f2c_generic_designator_key(item);
    size_t capacity;
    if (key == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &item->span, 1,
                                 "out of memory recording module accessibility");
        return 0;
    }
    if (find_access_entry(module, key) != NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &item->span, 1,
                                 "access identifier '%s' is specified more than once", key);
        free(key);
        return 0;
    }
    if (module->access_entry_count == module->access_entry_capacity) {
        capacity = module->access_entry_capacity == 0U ? 8U : module->access_entry_capacity * 2U;
        if (capacity < module->access_entry_capacity ||
            capacity > SIZE_MAX / sizeof(*replacement)) {
            free(key);
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                                "too many module access identifiers");
            return 0;
        }
        replacement = (F2cModuleAccessEntry *)realloc(module->access_entries,
                                                      capacity * sizeof(*replacement));
        if (replacement == NULL) {
            free(key);
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line->number, 1,
                                "out of memory recording module accessibility");
            return 0;
        }
        module->access_entries = replacement;
        module->access_entry_capacity = capacity;
    }
    entry = &module->access_entries[module->access_entry_count++];
    memset(entry, 0, sizeof(*entry));
    entry->key = key;
    entry->access = access;
    entry->span = item->span;
    return 1;
}

static void lower_access_statement(Context *context, Unit *unit, const Line *line,
                                   const F2cAccessStatementSyntax *syntax,
                                   int in_specification_part) {
    const F2cAccessibility access = lower_access(syntax->kind);
    size_t index;
    if (!in_specification_part) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax->keyword, 1,
                                  "access statement must appear in the specification part");
        return;
    }
    if (unit->kind != UNIT_MODULE) {
        f2c_diagnostic_token_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax->keyword, 1,
            "PUBLIC and PRIVATE access statements are valid only in a module");
        return;
    }
    if (syntax->item_count == 0U) {
        if (unit->default_access_explicit) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, syntax->keyword, 1,
                                      "module default accessibility is specified more than once");
            return;
        }
        unit->default_access = access;
        unit->default_access_span = syntax->keyword->span;
        unit->default_access_explicit = 1;
        return;
    }
    for (index = 0U; index < syntax->item_count; ++index)
        (void)append_access_entry(context, unit, line, &syntax->items[index], access);
}

void f2c_parse_access_statements(Context *context, Unit *unit) {
    size_t index;
    int in_specification_part = 1;
    if (context == NULL || unit == NULL)
        return;
    for (index = unit->begin + 1U; index < unit->end; ++index) {
        Line *line = &context->lines.items[index];
        F2cAccessStatementSyntax syntax;
        F2cAccessStatementStatus status;
        if (!f2c_unit_line_is_active(unit, line))
            continue;
        if (f2c_interface_start_tokens(line)) {
            while (index + 1U < unit->end &&
                   !f2c_interface_end_tokens(&context->lines.items[index + 1U]))
                ++index;
            if (index + 1U < unit->end)
                ++index;
            continue;
        }
        if (line->interface_depth != 0U || f2c_line_in_derived_type(unit, index))
            continue;
        status = f2c_parse_access_statement_syntax(line, &syntax);
        if (status == F2C_ACCESS_STATEMENT_NOT_MATCHED) {
            if (in_specification_part && !f2c_declaration_tokens(line) &&
                !f2c_use_statement_candidate(line))
                in_specification_part = 0;
            continue;
        }
        if (status == F2C_ACCESS_STATEMENT_NO_MEMORY) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, &syntax.span, 1,
                                     "out of memory parsing access statement");
        } else if (status == F2C_ACCESS_STATEMENT_INVALID) {
            report_syntax_error(context, line, &syntax);
        } else {
            lower_access_statement(context, unit, line, &syntax, in_specification_part);
        }
        f2c_access_statement_syntax_discard(&syntax);
    }
}

static Unit *find_contained_procedure(Context *context, const Unit *module, const char *name) {
    size_t index;
    for (index = 0U; index < context->units.count; ++index) {
        Unit *candidate = &context->units.items[index];
        const char *visible =
            candidate->fortran_name != NULL ? candidate->fortran_name : candidate->name;
        if (candidate->begin > module->end && candidate->begin < module->container_end &&
            strcmp(visible, name) == 0)
            return candidate;
    }
    return NULL;
}

static int interface_generic_exists(const Unit *module, const char *name) {
    size_t index;
    for (index = 0U; index < module->interface_count; ++index) {
        const Unit *candidate = &module->interfaces[index];
        if (candidate->interface_generic_name != NULL &&
            strcmp(candidate->interface_generic_name, name) == 0)
            return 1;
    }
    return 0;
}

static void duplicate_access_diagnostic(Context *context, const F2cModuleAccessEntry *entry,
                                        int *reported) {
    if (*reported)
        return;
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &entry->span, 1,
                             "accessibility of '%s' is specified more than once", entry->key);
    *reported = 1;
}

void f2c_finalize_module_accessibility(Context *context, Unit *module) {
    size_t entry_index;
    if (context == NULL || module == NULL || module->kind != UNIT_MODULE)
        return;
    for (entry_index = 0U; entry_index < module->access_entry_count; ++entry_index) {
        F2cModuleAccessEntry *entry = &module->access_entries[entry_index];
        Symbol *symbol;
        Unit *procedure;
        size_t index;
        int resolved = 0;
        int duplicate_reported = 0;
        if (strchr(entry->key, '(') != NULL) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &entry->span, 1,
                                     "generic access identifier '%s' has no generic binding",
                                     entry->key);
            continue;
        }
        symbol = f2c_find_symbol(module, entry->key);
        if (symbol != NULL) {
            resolved = 1;
            if (symbol->access != F2C_ACCESS_UNSPECIFIED)
                duplicate_access_diagnostic(context, entry, &duplicate_reported);
            else {
                symbol->access = entry->access;
                symbol->access_span = entry->span;
            }
        }
        for (index = 0U; index < module->derived_type_count; ++index) {
            F2cDerivedType *derived = &module->derived_types[index];
            if (strcmp(derived->name, entry->key) != 0)
                continue;
            resolved = 1;
            if (derived->access != F2C_ACCESS_UNSPECIFIED)
                duplicate_access_diagnostic(context, entry, &duplicate_reported);
            else {
                derived->access = entry->access;
                derived->access_span = entry->span;
            }
        }
        for (index = 0U; index < module->imported_derived_type_count; ++index) {
            F2cImportedDerivedType *derived = &module->imported_derived_types[index];
            if (strcmp(derived->local_name, entry->key) != 0)
                continue;
            resolved = 1;
            if (derived->access != F2C_ACCESS_UNSPECIFIED)
                duplicate_access_diagnostic(context, entry, &duplicate_reported);
            else {
                derived->access = entry->access;
                derived->access_span = entry->span;
            }
        }
        procedure = find_contained_procedure(context, module, entry->key);
        if (procedure != NULL) {
            resolved = 1;
            if (procedure->access != F2C_ACCESS_UNSPECIFIED)
                duplicate_access_diagnostic(context, entry, &duplicate_reported);
            else {
                procedure->access = entry->access;
                procedure->access_span = entry->span;
            }
        }
        if (interface_generic_exists(module, entry->key))
            resolved = 1;
        if (!resolved)
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &entry->span, 1,
                                     "access identifier '%s' is not declared in module '%s'",
                                     entry->key, module->name);
    }
}

int f2c_module_symbol_is_public(const Unit *module, const Symbol *symbol) {
    return module != NULL && symbol != NULL &&
           effective_access(module, symbol->access) == F2C_ACCESSIBILITY_PUBLIC;
}

int f2c_module_derived_type_is_public(const Unit *module, const char *local_name,
                                      const F2cDerivedType *derived) {
    size_t index;
    if (module == NULL || local_name == NULL || derived == NULL)
        return 0;
    for (index = 0U; index < module->derived_type_count; ++index) {
        if (&module->derived_types[index] == derived &&
            strcmp(module->derived_types[index].name, local_name) == 0)
            return effective_access(module, module->derived_types[index].access) ==
                   F2C_ACCESSIBILITY_PUBLIC;
    }
    for (index = 0U; index < module->imported_derived_type_count; ++index) {
        const F2cImportedDerivedType *imported = &module->imported_derived_types[index];
        if (imported->type == derived && strcmp(imported->local_name, local_name) == 0)
            return effective_access(module, imported->access) == F2C_ACCESSIBILITY_PUBLIC;
    }
    return 0;
}

int f2c_module_procedure_is_public(const Unit *module, const Unit *procedure) {
    return module != NULL && procedure != NULL &&
           effective_access(module, procedure->access) == F2C_ACCESSIBILITY_PUBLIC;
}
