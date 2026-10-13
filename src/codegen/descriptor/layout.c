#include "codegen/descriptor/private.h"

#include "codegen/array/private.h"

#include <stdlib.h>

char *f2c_descriptor_symbol_record_size(Unit *unit, const Symbol *symbol) {
    Buffer result = {0};
    if (symbol == NULL)
        return NULL;
    if (symbol->type == TYPE_CHARACTER)
        return f2c_symbol_character_length(unit, symbol);
    f2c_buffer_printf(&result, "sizeof(%s)", f2c_symbol_c_type(symbol));
    return f2c_buffer_take(&result);
}

char *f2c_descriptor_expression_record_size(Unit *unit, const F2cExpr *expression) {
    Buffer result = {0};
    if (expression == NULL)
        return NULL;
    if (expression->type == TYPE_CHARACTER)
        return f2c_character_length_expression(unit, expression);
    f2c_buffer_printf(&result, "sizeof(%s)", f2c_expression_c_type(expression));
    return f2c_buffer_take(&result);
}

char *f2c_descriptor_dense_byte_stride(Unit *unit, const F2cExpr *expression, size_t dimension) {
    char *stride = f2c_descriptor_expression_record_size(unit, expression);
    if (stride != NULL) {
        Buffer checked = {0};
        f2c_buffer_printf(&checked, "f2c_descriptor_record_stride((size_t)(%s))", stride);
        free(stride);
        stride = f2c_buffer_take(&checked);
    }
    for (size_t prior = 0U; stride != NULL && prior < dimension; ++prior) {
        Buffer next = {0};
        char *extent = f2c_array_expression_extent(unit, expression, prior);
        if (extent == NULL) {
            free(stride);
            return NULL;
        }
        f2c_buffer_printf(&next, "f2c_descriptor_stride_extent((ptrdiff_t)(%s), (size_t)(%s))",
                          stride, extent);
        free(stride);
        free(extent);
        stride = f2c_buffer_take(&next);
    }
    return stride;
}
