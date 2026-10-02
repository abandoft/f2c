#include "codegen/expression/private.h"

#include <stdint.h>
#include <stdlib.h>

char *f2c_expression_parenthesized(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *value;
    char *code;
    Buffer result = {0};
    if (expression == NULL || expression->child_count != 1U ||
        (value = expression->children[0]) == NULL) {
        *supported = 0;
        return NULL;
    }
    code = f2c_expression_emit(unit, value, supported);
    if (!*supported || code == NULL) {
        free(code);
        *supported = 0;
        return NULL;
    }
    if (expression->type == TYPE_CHARACTER && expression->rank == 0U) {
        char *pointer = f2c_character_source_pointer(unit, value, code);
        char *length = f2c_character_length_expression(unit, value);
        if (expression->temporary_index == SIZE_MAX || pointer == NULL || length == NULL) {
            free(code);
            free(pointer);
            free(length);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result,
                          "f2c_character_snapshot%s(&f2c_character_result_%zu, %s, "
                          "(size_t)(%s))",
                          (value->storage_qualifiers & F2C_STORAGE_VOLATILE) != 0U ? "_volatile"
                                                                                   : "",
                          expression->temporary_index, pointer, length);
        free(pointer);
        free(length);
    } else {
        f2c_buffer_printf(&result, "(%s)", code);
    }
    free(code);
    return f2c_buffer_take(&result);
}
