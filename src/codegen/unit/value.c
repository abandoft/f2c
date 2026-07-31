#include "codegen/unit/private.h"

#include <stdlib.h>

static void emit_intrinsic_copy(Buffer *output, Unit *unit, const Symbol *symbol, int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "%s f2c_value_storage_%s = {0};\n", f2c_symbol_c_type(symbol), name);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output,
                      "if (f2c_value_argument_%s != NULL) "
                      "f2c_value_storage_%s = *f2c_value_argument_%s;\n",
                      name, name, name);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output,
                      "%s *%s = f2c_value_argument_%s != NULL "
                      "? &f2c_value_storage_%s : NULL;\n",
                      f2c_symbol_c_type(symbol), name, name, name);
}

static void emit_character_copy(Buffer *output, Unit *unit, const Symbol *symbol, int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    char *length = f2c_symbol_character_length(unit, symbol);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "const size_t f2c_value_length_%s = (size_t)(%s);\n", name,
                      length != NULL ? length : "0U");
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "char *%s = NULL;\n", name);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "if (f2c_value_argument_%s != NULL) {\n", name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output, "if (f2c_value_length_%s == SIZE_MAX) abort();\n", name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output, "%s = (char *)malloc(f2c_value_length_%s + 1U);\n", name, name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output, "if (%s == NULL) abort();\n", name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "const size_t f2c_value_copy_%s = "
                      "F2C_MIN(f2c_value_length_%s, f2c_len_%s);\n",
                      name, name, name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "if (f2c_value_copy_%s != 0U) "
                      "memmove(%s, f2c_value_argument_%s, f2c_value_copy_%s);\n",
                      name, name, name, name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "if (f2c_value_length_%s > f2c_value_copy_%s) "
                      "memset(%s + f2c_value_copy_%s, ' ', "
                      "f2c_value_length_%s - f2c_value_copy_%s);\n",
                      name, name, name, name, name, name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output, "%s[f2c_value_length_%s] = '\\0';\n", name, name);
    f2c_unit_indent(output, depth);
    f2c_buffer_append(output, "}\n");
    free(length);
}

static void emit_derived_copy(Buffer *output, Unit *unit, const Symbol *symbol, int depth) {
    const char *name = f2c_symbol_c_name(unit, symbol);
    const char *type = symbol->derived_type->c_name;
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "%s f2c_value_storage_%s = {0};\n", type, name);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "%s *%s = NULL;\n", type, name);
    f2c_unit_indent(output, depth);
    f2c_buffer_printf(output, "if (f2c_value_argument_%s != NULL) {\n", name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output,
                      "f2c_clone_%s(&f2c_value_storage_%s, "
                      "f2c_value_argument_%s);\n",
                      type, name, name);
    f2c_unit_indent(output, depth + 1);
    f2c_buffer_printf(output, "%s = &f2c_value_storage_%s;\n", name, name);
    f2c_unit_indent(output, depth);
    f2c_buffer_append(output, "}\n");
}

void f2c_unit_emit_value_copies(Buffer *output, Unit *unit, int depth) {
    size_t argument;
    for (argument = 0U; argument < unit->argument_count; ++argument) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[argument]);
        if (symbol == NULL || !symbol->value)
            continue;
        if (symbol->type == TYPE_CHARACTER)
            emit_character_copy(output, unit, symbol, depth);
        else if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL)
            emit_derived_copy(output, unit, symbol, depth);
        else
            emit_intrinsic_copy(output, unit, symbol, depth);
    }
}

void f2c_unit_emit_value_cleanup(Buffer *output, Unit *unit, int depth) {
    size_t argument = unit->argument_count;
    while (argument != 0U) {
        Symbol *symbol = f2c_find_symbol(unit, unit->arguments[--argument]);
        const char *name;
        if (symbol == NULL || !symbol->value)
            continue;
        name = f2c_symbol_c_name(unit, symbol);
        f2c_unit_indent(output, depth);
        if (symbol->type == TYPE_CHARACTER)
            f2c_buffer_printf(output, "free(%s);\n", name);
        else if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL)
            f2c_buffer_printf(output, "if (%s != NULL) f2c_destroy_%s(%s);\n", name,
                              symbol->derived_type->c_name, name);
    }
}
