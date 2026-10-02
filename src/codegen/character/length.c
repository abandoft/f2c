#include "codegen/codegen.h"

#include "semantic/semantic.h"

#include <stdlib.h>

char *f2c_character_parameter_length(Unit *unit, const F2cExpr *expression) {
    Buffer result = {0};
    int64_t constant;
    char *value;
    if (expression == NULL || expression->type != TYPE_INTEGER || expression->rank != 0U)
        return NULL;
    if (f2c_evaluate_integer_constant(unit, expression, &constant))
        return constant <= 0 ? f2c_strdup("0U") : f2c_emit_typed_expression(unit, expression);
    value = f2c_emit_typed_expression(unit, expression);
    if (value == NULL)
        return NULL;
    /* The function argument is evaluated once, unlike a MAX macro expansion. */
    f2c_buffer_printf(&result, "f2c_character_parameter_length((int64_t)(%s))", value);
    free(value);
    return f2c_buffer_take(&result);
}

void f2c_emit_character_length_support(Buffer *output) {
    f2c_buffer_append(
        output,
        "static inline F2C_UNUSED bool f2c_character_parameter_size(int64_t value, size_t "
        "*length) { if (value <= 0) { *length = 0U; return true; }\n"
        "#if SIZE_MAX < UINT64_MAX\n"
        "if ((uint64_t)value > (uint64_t)SIZE_MAX) return false;\n"
        "#endif\n"
        "*length = (size_t)value; return true; }\n"
        "static inline F2C_UNUSED size_t f2c_character_parameter_length(int64_t value) { "
        "size_t length = 0U; if (!f2c_character_parameter_size(value, &length)) abort(); "
        "return length; }\n"
        "static inline F2C_UNUSED size_t f2c_character_copy_length(size_t destination, "
        "size_t source) { return destination < source ? destination : source; }\n"
        "static inline F2C_UNUSED bool f2c_character_target_lengths(size_t pointer_length, "
        "size_t target_length) { return target_length != 0U && pointer_length == "
        "target_length; }\n"
        "static inline F2C_UNUSED bool f2c_character_associated_target(const char *pointer, "
        "size_t pointer_length, const char *target, size_t target_length) { return "
        "pointer != NULL && f2c_character_target_lengths(pointer_length, target_length) "
        "&& pointer == target; }\n");
}
