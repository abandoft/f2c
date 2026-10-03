#include "codegen/storage/private.h"

#include <stdlib.h>

char *f2c_storage_linear_offset(Unit *unit, const F2cStorageReference *reference,
                                const char *index) {
    const Symbol *symbol = reference != NULL ? reference->symbol : NULL;
    Buffer offset = {0};
    if (symbol == NULL || index == NULL || symbol->rank == 0U)
        return NULL;
    if (!symbol->pointer && !(symbol->argument && f2c_symbol_uses_descriptor(symbol)))
        return f2c_strdup(index);
    f2c_buffer_printf(&offset,
                      "f2c_descriptor_linear_offset(&(f2c_descriptor){.rank = %zuU, "
                      ".extent = {",
                      symbol->rank);
    for (size_t dimension = 0U; dimension < symbol->rank; ++dimension) {
        char *extent = f2c_storage_read_property(unit, reference, F2C_OBJECT_EXTENT, dimension);
        if (extent == NULL)
            goto failed;
        f2c_buffer_printf(&offset, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ", extent);
        free(extent);
    }
    f2c_buffer_append(&offset, "}, .stride = {");
    for (size_t dimension = 0U; dimension < symbol->rank; ++dimension) {
        char *stride = f2c_storage_read_property(unit, reference, F2C_OBJECT_STRIDE, dimension);
        if (stride == NULL)
            goto failed;
        f2c_buffer_printf(&offset, "%s(ptrdiff_t)(%s)", dimension == 0U ? "" : ", ", stride);
        free(stride);
    }
    f2c_buffer_printf(&offset, "}}, (size_t)(%s))", index);
    return f2c_buffer_take(&offset);
failed:
    free(f2c_buffer_take(&offset));
    return NULL;
}

char *f2c_storage_linear_element(Unit *unit, const F2cStorageReference *reference,
                                 const char *index) {
    Buffer result = {0};
    char *data = f2c_storage_read_property(unit, reference, F2C_OBJECT_DATA, 0U);
    char *offset = f2c_storage_linear_offset(unit, reference, index);
    if (data == NULL || offset == NULL) {
        free(data);
        free(offset);
        return NULL;
    }
    if ((reference->object_qualifiers & F2C_STORAGE_VOLATILE) != 0U)
        f2c_buffer_printf(&result, "((%svolatile %s *)(%s))[%s]",
                          reference->readonly_storage ? "const " : "",
                          f2c_symbol_c_type(reference->symbol), data, offset);
    else
        f2c_buffer_printf(&result, "(%s)[%s]", data, offset);
    free(data);
    free(offset);
    return f2c_buffer_take(&result);
}
