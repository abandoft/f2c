#include "codegen/unit/private.h"

#include <stdlib.h>

static int block_scoped_symbol(const Unit *unit, const Symbol *symbol) {
    return symbol->scope_begin_line != 0U && !unit->save_all && !symbol->saved &&
           symbol->initializer == NULL && !symbol->argument && !symbol->module_entity;
}

static void emit_block_symbol_cleanup(Buffer *output, Unit *unit, Symbol *symbol, int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    size_t dimension;
    if (!block_scoped_symbol(unit, symbol))
        return;
    if (f2c_symbol_is_automatic_array(unit, symbol)) {
        f2c_unit_emit_automatic_array_cleanup(output, unit, symbol, depth);
    } else if (symbol->allocatable) {
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "if (%s != NULL) {\n", name);
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            free(count);
        }
        f2c_unit_indent(output, depth + 1);
        f2c_buffer_printf(output, "free(%s); %s = NULL;\n", name, name);
        if (symbol->deferred_character) {
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_char_len_%s = 0U;\n", name);
        }
        for (dimension = 0U; dimension < symbol->rank; ++dimension) {
            f2c_unit_indent(output, depth + 1);
            f2c_buffer_printf(output, "%s_lower_%zu = 1; %s_extent_%zu = 0;\n", name,
                              dimension + 1U, name, dimension + 1U);
        }
        f2c_unit_indent(output, depth);
        f2c_buffer_append(output, "}\n");
    } else if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "if (f2c_scope_live_%s) {\n", name);
        f2c_unit_indent(output, depth + 1);
        if (symbol->rank == 0U) {
            f2c_buffer_printf(output, "f2c_destroy_%s(&%s);\n", symbol->derived_type->c_name, name);
        } else {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              symbol->derived_type->c_name, name, count != NULL ? count : "0U",
                              symbol->rank);
            free(count);
        }
        f2c_unit_indent(output, depth + 1);
        f2c_buffer_printf(output, "f2c_scope_live_%s = false;\n", name);
        f2c_unit_indent(output, depth);
        f2c_buffer_append(output, "}\n");
    }
}

void f2c_emit_block_scope_begin(Buffer *output, Unit *unit, size_t line, int depth) {
    size_t i;
    for (i = 0U; i < unit->symbol_count; ++i) {
        Symbol *symbol = &unit->symbols[i];
        const char *name;
        if (!block_scoped_symbol(unit, symbol) || symbol->scope_begin_line != line)
            continue;
        if (f2c_symbol_is_automatic_array(unit, symbol)) {
            f2c_unit_emit_automatic_array_allocation(output, unit, symbol, depth);
            continue;
        }
        if (symbol->allocatable || symbol->type != TYPE_DERIVED || symbol->derived_type == NULL)
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        f2c_unit_indent(output, depth);
        if (symbol->rank == 0U) {
            f2c_buffer_printf(output, "f2c_initialize_%s(&%s);\n", symbol->derived_type->c_name,
                              name);
        } else {
            char *count = f2c_symbol_element_count(unit, symbol);
            f2c_buffer_printf(output,
                              "for (size_t f2c_scope_index = 0U; "
                              "f2c_scope_index < (size_t)(%s); ++f2c_scope_index) "
                              "f2c_initialize_%s(&%s[f2c_scope_index]);\n",
                              count != NULL ? count : "0U", symbol->derived_type->c_name, name);
            free(count);
        }
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_scope_live_%s = true;\n", name);
    }
}

void f2c_emit_block_scope_end(Buffer *output, Unit *unit, size_t line, int depth) {
    size_t i = unit->symbol_count;
    while (i != 0U) {
        Symbol *symbol = &unit->symbols[--i];
        if (symbol->scope_end_line == line)
            emit_block_symbol_cleanup(output, unit, symbol, depth);
    }
}

void f2c_emit_scope_cleanup_plan(Buffer *output, Unit *unit, const F2cScopeCleanupPlan *plan,
                                 int depth) {
    size_t index;
    if (plan == NULL)
        return;
    for (index = 0U; index < plan->symbol_count; ++index)
        emit_block_symbol_cleanup(output, unit, plan->symbols[index], depth);
}
