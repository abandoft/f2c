#include "codegen/storage/private.h"

#include <stdlib.h>

static void indent(Buffer *output, int depth) {
    for (int level = 0; level < depth; ++level)
        f2c_buffer_append(output, "    ");
}

int f2c_storage_emit_dummy_entry(Buffer *output, Unit *unit, Symbol *symbol, int depth) {
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(symbol);
    if (reference.state_source != F2C_OBJECT_STATE_DESCRIPTOR || symbol->intent != F2C_INTENT_OUT)
        return 0;
    indent(output, depth);
    f2c_buffer_printf(output, "if (f2c_descriptor_%s != NULL) {\n",
                      f2c_symbol_c_name(unit, symbol));
    if (symbol->allocatable) {
        char *data = f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
        if (data == NULL)
            return 0;
        if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL) {
            char *count =
                symbol->rank == 0U ? f2c_strdup("1U") : f2c_symbol_element_count(unit, symbol);
            if (count == NULL) {
                free(data);
                return 0;
            }
            indent(output, depth + 1);
            f2c_buffer_printf(output, "if (%s != NULL) %s_%s(%s, (size_t)(%s), %zuU);\n", data,
                              symbol->polymorphic ? "f2c_destroy_dynamic" : "f2c_destroy_array",
                              symbol->derived_type->c_name, data, count, symbol->rank);
            free(count);
        }
        indent(output, depth + 1);
        f2c_buffer_printf(output, "free(%s);\n", data);
        free(data);
    }
    if (!f2c_storage_emit_store(output, unit, &reference, F2C_OBJECT_DATA, 0U, NULL, "NULL",
                                depth + 1))
        return 0;
    if ((symbol->pointer || symbol->allocatable) &&
        !f2c_storage_emit_store(output, unit, &reference, F2C_OBJECT_DEALLOCATABLE, 0U, NULL,
                                "false", depth + 1))
        return 0;
    if (symbol->deferred_character &&
        !f2c_storage_emit_store(output, unit, &reference, F2C_OBJECT_CHARACTER_LENGTH, 0U, NULL,
                                "0U", depth + 1))
        return 0;
    for (size_t dimension = 0U; dimension < symbol->rank; ++dimension) {
        if (!f2c_storage_emit_store(output, unit, &reference, F2C_OBJECT_LOWER, dimension, NULL,
                                    "1", depth + 1) ||
            !f2c_storage_emit_store(output, unit, &reference, F2C_OBJECT_EXTENT, dimension, NULL,
                                    "0", depth + 1) ||
            !f2c_storage_emit_store(output, unit, &reference, F2C_OBJECT_STRIDE, dimension, NULL,
                                    "0", depth + 1))
            return 0;
    }
    indent(output, depth);
    f2c_buffer_append(output, "}\n");
    return !output->failed;
}
