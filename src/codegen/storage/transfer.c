#include "codegen/storage/private.h"

#include <stdlib.h>

static void finish_operation(Buffer *output, F2cStorageEmissionStyle style) {
    f2c_buffer_append(output, style == F2C_STORAGE_STATEMENT ? ";\n" : ", ");
}

static void indent(Buffer *output, F2cStorageEmissionStyle style, int depth) {
    if (style == F2C_STORAGE_STATEMENT)
        for (int level = 0; level < depth; ++level)
            f2c_buffer_append(output, "    ");
}

static int commit_property(Buffer *output, Unit *unit, const F2cStorageReference *reference,
                           F2cObjectStateProperty property, size_t dimension, const char *binding,
                           const char *descriptor, F2cStorageEmissionStyle style, int depth) {
    char *destination =
        f2c_storage_bound_property(unit, reference, property, dimension, binding, 1);
    if (destination == NULL)
        return 0;
    indent(output, style, depth);
    f2c_buffer_printf(output, "%s = ", destination);
    free(destination);
    switch (property) {
    case F2C_OBJECT_DATA:
        f2c_buffer_printf(output, "(%s *)(%s)->data", f2c_symbol_c_type(reference->symbol),
                          descriptor);
        break;
    case F2C_OBJECT_LOWER:
    case F2C_OBJECT_EXTENT:
    case F2C_OBJECT_STRIDE:
        f2c_buffer_printf(output, "f2c_descriptor_state_%s(%s, %zuU, 0U)",
                          property == F2C_OBJECT_LOWER    ? "lower"
                          : property == F2C_OBJECT_EXTENT ? "extent"
                                                          : "stride",
                          descriptor, dimension);
        break;
    case F2C_OBJECT_CHARACTER_LENGTH:
        f2c_buffer_printf(output, "(%s)->character_length", descriptor);
        break;
    case F2C_OBJECT_DEALLOCATABLE:
        f2c_buffer_printf(output, "(%s)->deallocatable", descriptor);
        break;
    }
    finish_operation(output, style);
    return !output->failed;
}

int f2c_storage_emit_descriptor_commit(Buffer *output, Unit *unit,
                                       const F2cStorageReference *reference,
                                       const char *physical_binding, const char *descriptor_pointer,
                                       F2cStorageEmissionStyle style, int depth) {
    const Symbol *symbol = reference != NULL ? reference->symbol : NULL;
    Buffer transaction = {0};
    int success = 0;
    if (output == NULL || unit == NULL || symbol == NULL || descriptor_pointer == NULL ||
        reference->state_source == F2C_OBJECT_STATE_NONE || symbol->procedure_pointer ||
        (style != F2C_STORAGE_STATEMENT && style != F2C_STORAGE_COMMA_EXPRESSION))
        return 0;
    if (reference->readonly_state)
        return 1;
    indent(&transaction, style, depth);
    f2c_buffer_printf(&transaction,
                      "f2c_descriptor_bridge_valid(%s, %zuU, sizeof(%s)) ? (void)0 : abort()",
                      descriptor_pointer, symbol->rank, f2c_symbol_c_type(symbol));
    finish_operation(&transaction, style);
    if (!commit_property(&transaction, unit, reference, F2C_OBJECT_DATA, 0U, physical_binding,
                         descriptor_pointer, style, depth) ||
        (symbol->pointer &&
         !commit_property(&transaction, unit, reference, F2C_OBJECT_DEALLOCATABLE, 0U,
                          physical_binding, descriptor_pointer, style, depth)) ||
        (symbol->deferred_character &&
         !commit_property(&transaction, unit, reference, F2C_OBJECT_CHARACTER_LENGTH, 0U,
                          physical_binding, descriptor_pointer, style, depth)))
        goto done;
    for (size_t dimension = 0U; dimension < symbol->rank; ++dimension) {
        if (!commit_property(&transaction, unit, reference, F2C_OBJECT_LOWER, dimension,
                             physical_binding, descriptor_pointer, style, depth) ||
            !commit_property(&transaction, unit, reference, F2C_OBJECT_EXTENT, dimension,
                             physical_binding, descriptor_pointer, style, depth) ||
            ((symbol->pointer || reference->state_source == F2C_OBJECT_STATE_DESCRIPTOR) &&
             !commit_property(&transaction, unit, reference, F2C_OBJECT_STRIDE, dimension,
                              physical_binding, descriptor_pointer, style, depth)))
            goto done;
    }
    f2c_buffer_append(output, transaction.data);
    success = !output->failed;
done:
    free(f2c_buffer_take(&transaction));
    return success;
}
