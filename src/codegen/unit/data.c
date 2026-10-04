#include "codegen/unit/private.h"

#include "codegen/array/static_shape.h"
#include "codegen/constant/private.h"
#include "codegen/literal/real.h"
#include "codegen/type/initialization.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static char *static_numeric_initializer(Unit *unit, const Symbol *symbol,
                                        const F2cExpr *expression) {
    const Type target_type = symbol->type;
    Buffer initializer = {0};
    int64_t integer_value;
    double real_value;
    double imaginary_value = 0.0;
    char *real_literal;
    char *imaginary_literal;
    const int real_kind = symbol->kind != 0 ? symbol->kind : f2c_default_kind(target_type);
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
        real_literal = f2c_real_constant_literal(real_value, real_kind);
        if (real_literal == NULL)
            return NULL;
        f2c_buffer_printf(&initializer, "(%s)(%s)", f2c_symbol_c_type(symbol), real_literal);
        free(real_literal);
        return f2c_buffer_take(&initializer);
    }
    if (target_type != TYPE_COMPLEX && target_type != TYPE_DOUBLE_COMPLEX)
        return NULL;
    if (!f2c_evaluate_complex_constant(unit, expression, &real_value, &imaginary_value))
        return NULL;
    real_literal = f2c_real_constant_literal(real_value, real_kind);
    imaginary_literal = f2c_real_constant_literal(imaginary_value, real_kind);
    if (real_literal == NULL || imaginary_literal == NULL) {
        free(real_literal);
        free(imaginary_literal);
        return NULL;
    }
    f2c_buffer_printf(&initializer, "%s((%s)(%s), (%s)(%s))",
                      real_kind == 8 ? "F2C_COMPLEX_DOUBLE_INITIALIZER"
                                     : "F2C_COMPLEX_FLOAT_INITIALIZER",
                      f2c_c_type_kind(TYPE_REAL, real_kind), real_literal,
                      f2c_c_type_kind(TYPE_REAL, real_kind), imaginary_literal);
    free(real_literal);
    free(imaginary_literal);
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

static char *take_bounded_initializer(Unit *unit, Buffer *output) {
    if (output->limit_exceeded && unit->context != NULL)
        unit->context->output.limit_exceeded = 1;
    return f2c_buffer_take(output);
}

static char *fixed_character_initializer(Unit *unit, const Symbol *symbol,
                                         const F2cExpr *expression) {
    Buffer initializer = {.limit = unit->context != NULL ? unit->context->output.limit
                                                         : F2C_DEFAULT_MAX_OUTPUT_BYTES};
    int64_t declared_length;
    char *value = NULL;
    size_t value_length = 0U;
    size_t offset;
    if (!f2c_character_declaration_length(unit, symbol, &declared_length) ||
        (uint64_t)declared_length > SIZE_MAX)
        return NULL;
    if (!f2c_evaluate_character_constant(unit, expression, &value, &value_length)) {
        free(value);
        return NULL;
    }
    if (!f2c_reserve_constant_steps(unit, (size_t)declared_length)) {
        free(value);
        return NULL;
    }
    f2c_buffer_append(&initializer, "{");
    for (offset = 0U; offset < (size_t)declared_length && !initializer.failed; ++offset) {
        if (offset != 0U)
            f2c_buffer_append(&initializer, ", ");
        append_character_constant(&initializer, offset < value_length ? (unsigned char)value[offset]
                                                                      : (unsigned char)' ');
    }
    if (declared_length == 0)
        f2c_buffer_append(&initializer, "0");
    f2c_buffer_append(&initializer, "}");
    free(value);
    return take_bounded_initializer(unit, &initializer);
}

static char *character_data_array_initializer(Unit *unit, const Symbol *symbol) {
    Buffer initializer = {.limit = unit->context != NULL ? unit->context->output.limit
                                                         : F2C_DEFAULT_MAX_OUTPUT_BYTES};
    int64_t declared_length;
    size_t element;
    int emitted = 0;
    if (!f2c_character_declaration_length(unit, symbol, &declared_length) ||
        (uint64_t)declared_length > SIZE_MAX)
        return NULL;
    if (!f2c_reserve_constant_steps(unit, symbol->data_element_initializer_count))
        return NULL;
    f2c_buffer_append(&initializer, "{");
    for (element = 0U; element < symbol->data_element_initializer_count && !initializer.failed;
         ++element) {
        const F2cExpr *expression = symbol->data_element_initializers[element];
        char *value = NULL;
        size_t value_length = 0U;
        size_t offset;
        if (expression == NULL)
            continue;
        if (!f2c_reserve_constant_steps(unit, (size_t)declared_length)) {
            free(f2c_buffer_take(&initializer));
            return NULL;
        }
        if (!f2c_evaluate_character_constant(unit, expression, &value, &value_length)) {
            free(f2c_buffer_take(&initializer));
            return NULL;
        }
        for (offset = 0U; offset < (size_t)declared_length && !initializer.failed; ++offset) {
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
    return take_bounded_initializer(unit, &initializer);
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
        value =
            static_numeric_initializer(unit, symbol, symbol->data_element_initializers[element]);
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

char *f2c_unit_static_storage_initializer(Unit *unit, const Symbol *symbol) {
    if (unit == NULL || symbol == NULL)
        return NULL;
    if (symbol->rank != 0U && symbol->initializer_expression != NULL &&
        symbol->data_element_initializers == NULL)
        return f2c_constant_storage_initializer(unit, symbol);
    if (symbol->type == TYPE_DERIVED && symbol->derived_type != NULL)
        return f2c_derived_entity_initializer(unit, symbol);
    if (symbol->type == TYPE_CHARACTER) {
        if (symbol->rank != 0U && symbol->data_element_initializers != NULL)
            return character_data_array_initializer(unit, symbol);
        if (symbol->initializer_expression != NULL) {
            if (symbol->common_block != NULL || symbol->equivalence_associated)
                return fixed_character_initializer(unit, symbol, symbol->initializer_expression);
            int supported = 0;
            char *initializer = f2c_character_declaration_initializer(unit, symbol, &supported);
            return supported ? initializer : NULL;
        }
        return NULL;
    }
    if (symbol->rank != 0U && symbol->data_element_initializers != NULL)
        return numeric_data_array_initializer(unit, symbol);
    if (symbol->initializer_expression != NULL)
        return static_numeric_initializer(unit, symbol, symbol->initializer_expression);
    return NULL;
}
