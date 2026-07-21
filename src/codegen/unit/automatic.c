#include "codegen/unit/private.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int automatic_array_candidate(const Unit *unit, const Symbol *symbol) {
    return unit != NULL && symbol != NULL && symbol->rank != 0U && !symbol->argument &&
           !symbol->parameter && !symbol->external && !symbol->module_entity &&
           !symbol->host_associated && symbol->common_block == NULL &&
           !symbol->equivalence_associated && symbol->alias_to == NULL &&
           !symbol->statement_function && !symbol->allocatable && !symbol->pointer &&
           !symbol->automatic_character && !unit->save_all && !symbol->saved &&
           symbol->initializer == NULL && symbol->data_element_initializers == NULL &&
           !(unit->kind == UNIT_FUNCTION && unit->result_name != NULL &&
             strcmp(symbol->name, unit->result_name) == 0);
}

int f2c_symbol_is_automatic_array(Unit *unit, const Symbol *symbol) {
    size_t dimension;
    int64_t value;
    if (!automatic_array_candidate(unit, symbol))
        return 0;
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        const Dimension *shape = &symbol->dimensions[dimension];
        if (shape->kind != F2C_DIMENSION_EXPLICIT || shape->upper_expression == NULL)
            return 0;
        if (shape->lower_expression == NULL ||
            !f2c_evaluate_integer_constant(unit, shape->lower_expression, &value) ||
            !f2c_evaluate_integer_constant(unit, shape->upper_expression, &value))
            return 1;
    }
    return 0;
}

static void emit_metadata_declarations(Buffer *output, Unit *unit, Symbol *symbol, int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    size_t dimension;
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "int64_t f2c_auto_lower_%s_%zu = 1;\n", name, dimension + 1U);
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "size_t f2c_auto_extent_%s_%zu = 0U;\n", name, dimension + 1U);
    }
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "size_t f2c_auto_count_%s = 0U;\n", name);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "%s *%s = NULL;\n", f2c_symbol_c_type(symbol), name);
}

void f2c_unit_emit_automatic_array_allocation(Buffer *output, Unit *unit, Symbol *symbol,
                                              int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    const char *storage_count_prefix =
        symbol->type == TYPE_CHARACTER ? "f2c_auto_storage_count_" : "f2c_auto_count_";
    size_t dimension;
    char *character_length = NULL;
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "f2c_auto_count_%s = 1U;\n", name);
    for (dimension = 0U; dimension < symbol->rank; ++dimension) {
        char *lower =
            f2c_emit_typed_expression(unit, symbol->dimensions[dimension].lower_expression);
        char *upper =
            f2c_emit_typed_expression(unit, symbol->dimensions[dimension].upper_expression);
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "f2c_auto_lower_%s_%zu = (int64_t)(%s);\n", name, dimension + 1U,
                          lower != NULL ? lower : "1");
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output,
                          "f2c_auto_extent_%s_%zu = f2c_section_extent("
                          "f2c_auto_lower_%s_%zu, (int64_t)(%s), INT64_C(1));\n",
                          name, dimension + 1U, name, dimension + 1U, upper != NULL ? upper : "0");
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output,
                          "if (!f2c_size_multiply(f2c_auto_count_%s, "
                          "f2c_auto_extent_%s_%zu, &f2c_auto_count_%s)) abort();\n",
                          name, name, dimension + 1U, name);
        free(lower);
        free(upper);
    }
    if (symbol->type == TYPE_CHARACTER) {
        character_length = f2c_symbol_character_length(unit, symbol);
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output, "size_t f2c_auto_storage_count_%s = 0U;\n", name);
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output,
                          "if (!f2c_size_multiply(f2c_auto_count_%s, (size_t)(%s), "
                          "&f2c_auto_storage_count_%s)) abort();\n",
                          name, character_length != NULL ? character_length : "0U", name);
    }
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "if (%s%s > SIZE_MAX / sizeof(*%s)) abort();\n", storage_count_prefix,
                      name, name);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "%s = (%s *)calloc(%s%s == 0U ? 1U : %s%s, sizeof(*%s));\n", name,
                      f2c_symbol_c_type(symbol), storage_count_prefix, name, storage_count_prefix,
                      name, name);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "if (%s == NULL) abort();\n", name);
    if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        f2c_unit_indent(output, depth);
        f2c_buffer_printf(output,
                          "for (size_t f2c_auto_index_%s = 0U; "
                          "f2c_auto_index_%s < f2c_auto_count_%s; ++f2c_auto_index_%s) "
                          "f2c_initialize_%s(&%s[f2c_auto_index_%s]);\n",
                          name, name, name, name, symbol->derived_type->c_name, name, name);
    }
    free(character_length);
}

void f2c_unit_emit_automatic_array_declaration(Buffer *output, Unit *unit, Symbol *symbol,
                                               int depth) {
    emit_metadata_declarations(output, unit, symbol, depth);
    if (symbol->scope_begin_line == 0U)
        f2c_unit_emit_automatic_array_allocation(output, unit, symbol, depth);
}

void f2c_unit_emit_automatic_array_cleanup(Buffer *output, Unit *unit, Symbol *symbol, int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "if (%s != NULL) {\n", name);
    if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
        f2c_unit_indent(output, depth + 1);
        f2c_buffer_printf(output, "f2c_destroy_array_%s(%s, f2c_auto_count_%s, %zuU);\n",
                          symbol->derived_type->c_name, name, name, symbol->rank);
    }
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output, "free(%s); %s = NULL;\n", name, name);
    f2c_unit_indent(output, depth);
    f2c_buffer_append(output, "}\n");
}
