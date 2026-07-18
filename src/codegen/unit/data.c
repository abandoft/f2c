#include "codegen/unit/private.h"

#include <stdlib.h>

char *f2c_unit_data_array_initializer(Unit *unit, const Symbol *symbol) {
    Buffer initializer = {0};
    int complete = 1;
    size_t element;
    if (unit == NULL || symbol == NULL || symbol->data_element_initializers == NULL ||
        symbol->data_element_initializer_count == 0U)
        return NULL;
    for (element = 0U; element < symbol->data_element_initializer_count; ++element)
        if (symbol->data_element_initializers[element] == NULL) {
            complete = 0;
            break;
        }
    f2c_buffer_append(&initializer, "{");
    for (element = 0U; element < symbol->data_element_initializer_count; ++element) {
        char *value;
        if (symbol->data_element_initializers[element] == NULL)
            continue;
        value = f2c_emit_typed_expression(unit, symbol->data_element_initializers[element]);
        if (value == NULL) {
            free(f2c_buffer_take(&initializer));
            return NULL;
        }
        if (initializer.length > 1U)
            f2c_buffer_append(&initializer, ", ");
        if (!complete)
            f2c_buffer_printf(&initializer, "[%zu] = ", element);
        f2c_buffer_append(&initializer, value);
        free(value);
    }
    f2c_buffer_append(&initializer, "}");
    return f2c_buffer_take(&initializer);
}
