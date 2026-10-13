#include "codegen/storage/private.h"

#include "codegen/descriptor/private.h"

#include <stdlib.h>

char *f2c_storage_linear_byte_offset(Unit *unit, const F2cStorageReference *reference,
                                     const char *index) {
    const Symbol *symbol = reference != NULL ? reference->symbol : NULL;
    Buffer offset = {0};
    if (symbol == NULL || index == NULL || symbol->rank == 0U)
        return NULL;
    if (!symbol->pointer && !(symbol->argument && f2c_symbol_uses_descriptor(symbol))) {
        char *size = reference->expression != NULL
                         ? f2c_descriptor_expression_record_size(unit, reference->expression)
                         : f2c_descriptor_symbol_record_size(unit, symbol);
        if (size == NULL)
            return NULL;
        f2c_buffer_printf(
            &offset,
            "f2c_descriptor_stride_extent(f2c_descriptor_record_stride((size_t)(%s)), "
            "(size_t)(%s))",
            size, index);
        free(size);
        return f2c_buffer_take(&offset);
    }
    f2c_buffer_printf(&offset,
                      "f2c_descriptor_linear_byte_offset(&(f2c_descriptor){.rank = %zuU, "
                      ".extent = {",
                      symbol->rank);
    for (size_t dimension = 0U; dimension < symbol->rank; ++dimension) {
        char *extent = f2c_storage_read_property(unit, reference, F2C_OBJECT_EXTENT, dimension);
        if (extent == NULL)
            goto failed;
        f2c_buffer_printf(&offset, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ", extent);
        free(extent);
    }
    f2c_buffer_append(&offset, "}, .byte_stride = {");
    for (size_t dimension = 0U; dimension < symbol->rank; ++dimension) {
        char *stride =
            f2c_storage_read_property(unit, reference, F2C_OBJECT_BYTE_STRIDE, dimension);
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
    char *data = f2c_storage_read_property(unit, reference, F2C_OBJECT_DATA, 0U);
    const Symbol *symbol = reference != NULL ? reference->symbol : NULL;
    if (data != NULL && index != NULL && symbol != NULL && symbol->type != TYPE_CHARACTER &&
        !symbol->pointer && !(symbol->argument && f2c_symbol_uses_descriptor(symbol))) {
        Buffer element = {0};
        /* Dense C arrays retain native typed indexing. Byte offsets belong to
         * descriptor-backed views, not to the ordinary dense storage path. */
        f2c_buffer_printf(&element, "(%s)[(size_t)(%s)]", data, index);
        free(data);
        return f2c_buffer_take(&element);
    }
    char *offset = f2c_storage_linear_byte_offset(unit, reference, index);
    if (data == NULL || offset == NULL) {
        free(data);
        free(offset);
        return NULL;
    }
    char *result = f2c_storage_byte_element(reference, data, offset);
    free(data);
    free(offset);
    return result;
}

char *f2c_storage_byte_element(const F2cStorageReference *reference, const char *data,
                               const char *byte_offset) {
    Buffer result = {0};
    if (reference == NULL || reference->symbol == NULL || data == NULL || byte_offset == NULL)
        return NULL;
    const char *constant = reference->readonly_storage ? "const " : "";
    const char *qualified =
        (reference->object_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "volatile " : "";
    f2c_buffer_printf(&result, "(*(%s%s%s *)((%s%sunsigned char *)(%s) + (%s)))", constant,
                      qualified, f2c_symbol_c_type(reference->symbol), constant, qualified, data,
                      byte_offset);
    return f2c_buffer_take(&result);
}
