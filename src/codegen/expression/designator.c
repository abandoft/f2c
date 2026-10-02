#include "codegen/expression/private.h"

#include <stdlib.h>

char *f2c_expression_emit_substring(Unit *unit, const F2cExpr *expression, int *supported) {
    const F2cExpr *parent = f2c_substring_parent(expression);
    const F2cExpr *range = f2c_substring_range(expression);
    const F2cExpr *lower_expression = f2c_substring_lower(expression);
    const F2cExpr *upper_expression = f2c_substring_upper(expression);
    char *parent_code = NULL;
    char *pointer = NULL;
    char *length = NULL;
    char *lower = NULL;
    char *upper = NULL;
    Buffer result = {0};
    if (parent == NULL || range == NULL || range->kind != F2C_EXPR_ARRAY_SECTION ||
        range->child_count != 3U || parent->rank != 0U) {
        *supported = 0;
        return NULL;
    }
    parent_code = f2c_expression_emit(unit, parent, supported);
    if (*supported && parent_code != NULL)
        pointer = f2c_character_source_pointer(unit, parent, parent_code);
    length = f2c_character_length_expression(unit, parent);
    lower = lower_expression != NULL ? f2c_expression_emit(unit, lower_expression, supported)
                                     : f2c_strdup("1");
    upper = upper_expression != NULL ? f2c_expression_emit(unit, upper_expression, supported)
                                     : (length != NULL ? f2c_strdup(length) : NULL);
    if (*supported && pointer != NULL && length != NULL && lower != NULL && upper != NULL) {
        f2c_buffer_printf(&result,
                          "(&(%s)[f2c_substring_offset((size_t)(%s), (int64_t)(%s), "
                          "(int64_t)(%s))])",
                          pointer, length, lower, upper);
    } else {
        *supported = 0;
    }
    free(parent_code);
    free(pointer);
    free(length);
    free(lower);
    free(upper);
    return *supported ? f2c_buffer_take(&result) : NULL;
}
