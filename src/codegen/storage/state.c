#include "codegen/storage/private.h"

#include "codegen/descriptor/private.h"

#include <stdlib.h>

static const char *property_name(F2cObjectStateProperty property) {
    switch (property) {
    case F2C_OBJECT_DATA:
        return "data";
    case F2C_OBJECT_LOWER:
        return "lower";
    case F2C_OBJECT_EXTENT:
        return "extent";
    case F2C_OBJECT_STRIDE:
        return "stride";
    case F2C_OBJECT_CHARACTER_LENGTH:
        return "character_length";
    case F2C_OBJECT_DEALLOCATABLE:
        return "deallocatable";
    }
    return NULL;
}

static int dimension_property(F2cObjectStateProperty property) {
    return property == F2C_OBJECT_LOWER || property == F2C_OBJECT_EXTENT ||
           property == F2C_OBJECT_STRIDE;
}

static char *local_property(Unit *unit, const F2cStorageReference *reference,
                            F2cObjectStateProperty property, size_t dimension,
                            const char *physical_binding, int writable) {
    const Symbol *symbol = reference->symbol;
    Buffer result = {0};
    char *storage = physical_binding != NULL ? f2c_strdup(physical_binding)
                    : reference->owner != NULL
                        ? f2c_descriptor_storage_designator(unit, reference->expression)
                        : f2c_strdup(f2c_symbol_c_name(unit, symbol));
    if (storage == NULL)
        return NULL;
    if (property == F2C_OBJECT_DATA)
        f2c_buffer_append(&result, storage);
    else if (dimension_property(property))
        f2c_buffer_printf(&result, "%s_%s_%zu", storage, property_name(property), dimension + 1U);
    else if (property == F2C_OBJECT_CHARACTER_LENGTH)
        f2c_buffer_printf(
            &result, reference->owner != NULL ? "%s_character_length" : "f2c_char_len_%s", storage);
    else
        f2c_buffer_printf(&result, "%s_deallocatable", storage);
    free(storage);
    if (result.failed)
        return f2c_buffer_take(&result);
    if ((reference->state_qualifiers & F2C_STORAGE_VOLATILE) != 0U) {
        Buffer qualified = {0};
        const char *type = property == F2C_OBJECT_CHARACTER_LENGTH ? "size_t"
                           : property == F2C_OBJECT_DEALLOCATABLE  ? "bool"
                           : property == F2C_OBJECT_STRIDE         ? "ptrdiff_t"
                                                                   : "int32_t";
        if (property == F2C_OBJECT_DATA)
            f2c_buffer_printf(&qualified, "(*(%s *%svolatile *)&(%s))", f2c_symbol_c_type(symbol),
                              writable ? "" : "const ", result.data);
        else
            f2c_buffer_printf(&qualified, "(*(%svolatile %s *)&(%s))", writable ? "" : "const ",
                              type, result.data);
        free(f2c_buffer_take(&result));
        return f2c_buffer_take(&qualified);
    }
    return f2c_buffer_take(&result);
}

char *f2c_storage_bound_property(Unit *unit, const F2cStorageReference *reference,
                                 F2cObjectStateProperty property, size_t dimension,
                                 const char *physical_binding, int writable) {
    Buffer result = {0};
    const Symbol *symbol = reference != NULL ? reference->symbol : NULL;
    const char *field = property_name(property);
    if (symbol == NULL || field == NULL || (writable && reference->readonly_state) ||
        (dimension_property(property) && dimension >= symbol->rank))
        return NULL;
    if (reference->state_source != F2C_OBJECT_STATE_DESCRIPTOR)
        return local_property(unit, reference, property, dimension, physical_binding, writable);
    if (!writable)
        return f2c_storage_read_property(unit, reference, property, dimension);
    const int qualified = (reference->state_qualifiers & F2C_STORAGE_VOLATILE) != 0U;
    const char *type = property == F2C_OBJECT_DATA               ? "void *"
                       : property == F2C_OBJECT_CHARACTER_LENGTH ? "size_t "
                       : property == F2C_OBJECT_DEALLOCATABLE    ? "bool "
                       : property == F2C_OBJECT_STRIDE           ? "ptrdiff_t "
                                                                 : "int64_t ";
    if (qualified)
        f2c_buffer_printf(&result, "(*(%svolatile *)&(", type);
    f2c_buffer_printf(&result, "f2c_descriptor_%s->%s", f2c_symbol_c_name(unit, symbol), field);
    if (dimension_property(property))
        f2c_buffer_printf(&result, "[%zu]", dimension);
    if (qualified)
        f2c_buffer_append(&result, "))");
    return f2c_buffer_take(&result);
}

char *f2c_storage_write_property(Unit *unit, const F2cStorageReference *reference,
                                 F2cObjectStateProperty property, size_t dimension) {
    return f2c_storage_bound_property(unit, reference, property, dimension, NULL, 1);
}

int f2c_storage_emit_store(Buffer *output, Unit *unit, const F2cStorageReference *reference,
                           F2cObjectStateProperty property, size_t dimension,
                           const char *physical_binding, const char *value, int depth) {
    char *destination =
        f2c_storage_bound_property(unit, reference, property, dimension, physical_binding, 1);
    if (destination == NULL || value == NULL) {
        free(destination);
        return 0;
    }
    for (int level = 0; level < depth; ++level)
        f2c_buffer_append(output, "    ");
    f2c_buffer_printf(output, "%s = %s;\n", destination, value);
    free(destination);
    return !output->failed;
}

char *f2c_storage_read_property(Unit *unit, const F2cStorageReference *reference,
                                F2cObjectStateProperty property, size_t dimension) {
    Buffer result = {0};
    const Symbol *symbol = reference != NULL ? reference->symbol : NULL;
    const char *field = property_name(property);
    if (symbol == NULL || field == NULL ||
        (dimension_property(property) && dimension >= symbol->rank))
        return NULL;
    if (reference->state_source != F2C_OBJECT_STATE_DESCRIPTOR)
        return local_property(unit, reference, property, dimension, NULL, 0);
    if (property == F2C_OBJECT_DATA) {
        f2c_buffer_printf(&result, "((%s%s *)f2c_descriptor_state_%s(f2c_descriptor_%s, %uU))",
                          reference->readonly_storage ? "const " : "", f2c_symbol_c_type(symbol),
                          reference->readonly_storage ? "readonly_data" : "data",
                          f2c_symbol_c_name(unit, symbol), reference->state_qualifiers);
    } else {
        f2c_buffer_printf(&result, "f2c_descriptor_state_%s(f2c_descriptor_%s, ", field,
                          f2c_symbol_c_name(unit, symbol));
        if (dimension_property(property))
            f2c_buffer_printf(&result, "%zuU, ", dimension);
        f2c_buffer_printf(&result, "%uU)", reference->state_qualifiers);
    }
    return f2c_buffer_take(&result);
}

char *f2c_storage_symbol_property(Unit *unit, const Symbol *symbol, F2cObjectStateProperty property,
                                  size_t dimension) {
    const F2cStorageReference reference = f2c_ir_symbol_storage_reference(symbol);
    return f2c_storage_read_property(unit, &reference, property, dimension);
}

char *f2c_storage_symbol_data(Unit *unit, const Symbol *symbol) {
    return f2c_storage_symbol_property(unit, symbol, F2C_OBJECT_DATA, 0U);
}
