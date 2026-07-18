#include "codegen/unit/private.h"

#include <stdlib.h>

char *f2c_unit_data_array_initializer(Unit *unit, const Symbol *symbol) {
    Buffer initializer = {0};
    size_t element;
    if (unit == NULL || symbol == NULL || symbol->data_element_initializers == NULL ||
        symbol->data_element_initializer_count == 0U)
        return NULL;
    f2c_buffer_append(&initializer, "{");
    for (element = 0U; element < symbol->data_element_initializer_count; ++element) {
        char *value = f2c_emit_typed_expression(unit, symbol->data_element_initializers[element]);
        if (value == NULL) {
            free(f2c_buffer_take(&initializer));
            return NULL;
        }
        f2c_buffer_printf(&initializer, "%s%s", element == 0U ? "" : ", ", value);
        free(value);
    }
    f2c_buffer_append(&initializer, "}");
    return f2c_buffer_take(&initializer);
}
