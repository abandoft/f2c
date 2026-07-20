#include "codegen/unit/private.h"

#include <inttypes.h>
#include <stdint.h>
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

static char *static_numeric_initializer(Unit *unit, Type target_type, const F2cExpr *expression) {
    Buffer initializer = {0};
    int64_t integer_value;
    double real_value;
    double imaginary_value = 0.0;
    if (expression == NULL)
        return NULL;
    if (target_type == TYPE_INTEGER || target_type == TYPE_LOGICAL) {
        if (!f2c_evaluate_integer_constant(unit, expression, &integer_value))
            return NULL;
        if (target_type == TYPE_LOGICAL)
            return f2c_strdup(integer_value != 0 ? "true" : "false");
        if (integer_value == INT64_MIN)
            return f2c_strdup("INT64_MIN");
        if (integer_value < 0)
            f2c_buffer_printf(&initializer, "-INT64_C(%" PRId64 ")", -integer_value);
        else
            f2c_buffer_printf(&initializer, "INT64_C(%" PRId64 ")", integer_value);
        return f2c_buffer_take(&initializer);
    }
    if (target_type == TYPE_REAL || target_type == TYPE_DOUBLE) {
        if (!f2c_evaluate_real_constant(unit, expression, &real_value))
            return NULL;
        f2c_buffer_printf(&initializer, "(%s)(%a)", f2c_c_type(target_type), real_value);
        return f2c_buffer_take(&initializer);
    }
    if (target_type != TYPE_COMPLEX && target_type != TYPE_DOUBLE_COMPLEX)
        return NULL;
    if (expression->kind == F2C_EXPR_COMPLEX_LITERAL && expression->child_count == 2U) {
        if (!f2c_evaluate_real_constant(unit, expression->children[0], &real_value) ||
            !f2c_evaluate_real_constant(unit, expression->children[1], &imaginary_value))
            return NULL;
    } else if (!f2c_evaluate_real_constant(unit, expression, &real_value)) {
        return NULL;
    }
    f2c_buffer_printf(&initializer, "%s((%s)(%a), (%s)(%a))",
                      target_type == TYPE_DOUBLE_COMPLEX ? "F2C_COMPLEX_DOUBLE_INITIALIZER"
                                                         : "F2C_COMPLEX_FLOAT_INITIALIZER",
                      target_type == TYPE_DOUBLE_COMPLEX ? "double" : "float", real_value,
                      target_type == TYPE_DOUBLE_COMPLEX ? "double" : "float", imaginary_value);
    return f2c_buffer_take(&initializer);
}

static void append_character_constant(Buffer *output, unsigned char value) {
    if (value == '\'' || value == '\\')
        f2c_buffer_printf(output, "'\\%c'", value);
    else if (value >= 32U && value <= 126U)
        f2c_buffer_printf(output, "'%c'", value);
    else
        f2c_buffer_printf(output, "0x%02X", (unsigned int)value);
}

static char *character_data_array_initializer(Unit *unit, const Symbol *symbol) {
    Buffer initializer = {0};
    int64_t declared_length;
    size_t element;
    int emitted = 0;
    if (symbol->character_length_expression == NULL ||
        !f2c_evaluate_integer_constant(unit, symbol->character_length_expression,
                                       &declared_length) ||
        declared_length < 0 || (uint64_t)declared_length > SIZE_MAX)
        return NULL;
    f2c_buffer_append(&initializer, "{");
    for (element = 0U; element < symbol->data_element_initializer_count; ++element) {
        const F2cExpr *expression = symbol->data_element_initializers[element];
        char *value = NULL;
        size_t value_length = 0U;
        size_t offset;
        if (expression == NULL)
            continue;
        if (!f2c_evaluate_character_constant(unit, expression, &value, &value_length)) {
            free(f2c_buffer_take(&initializer));
            return NULL;
        }
        for (offset = 0U; offset < (size_t)declared_length; ++offset) {
            if (emitted)
                f2c_buffer_append(&initializer, ", ");
            f2c_buffer_printf(&initializer, "[%zu] = ", element * (size_t)declared_length + offset);
            append_character_constant(&initializer, offset < value_length
                                                        ? (unsigned char)value[offset]
                                                        : (unsigned char)' ');
            emitted = 1;
        }
        free(value);
    }
    if (!emitted)
        f2c_buffer_append(&initializer, "0");
    f2c_buffer_append(&initializer, "}");
    return f2c_buffer_take(&initializer);
}

static char *numeric_data_array_initializer(Unit *unit, const Symbol *symbol) {
    Buffer initializer = {0};
    size_t element;
    int complete = 1;
    if (symbol->data_element_initializers == NULL || symbol->data_element_initializer_count == 0U)
        return NULL;
    for (element = 0U; element < symbol->data_element_initializer_count; ++element)
        if (symbol->data_element_initializers[element] == NULL)
            complete = 0;
    f2c_buffer_append(&initializer, "{");
    for (element = 0U; element < symbol->data_element_initializer_count; ++element) {
        char *value;
        if (symbol->data_element_initializers[element] == NULL)
            continue;
        value = static_numeric_initializer(unit, symbol->type,
                                           symbol->data_element_initializers[element]);
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

char *f2c_unit_common_initializer(Unit *unit, const Symbol *symbol) {
    if (unit == NULL || symbol == NULL)
        return NULL;
    if (symbol->type == TYPE_CHARACTER) {
        if (symbol->rank != 0U && symbol->data_element_initializers != NULL)
            return character_data_array_initializer(unit, symbol);
        if (symbol->initializer_expression != NULL) {
            int supported = 0;
            char *initializer = f2c_character_declaration_initializer(unit, symbol, &supported);
            return supported ? initializer : NULL;
        }
        return NULL;
    }
    if (symbol->rank != 0U && symbol->data_element_initializers != NULL)
        return numeric_data_array_initializer(unit, symbol);
    if (symbol->initializer_expression != NULL)
        return static_numeric_initializer(unit, symbol->type, symbol->initializer_expression);
    return NULL;
}
