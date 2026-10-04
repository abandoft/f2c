#include "codegen/constant/private.h"

#include "internal/f2c.h"
#include "semantic/constant/array.h"

#include <stdlib.h>

static void append_character(Buffer *output, unsigned char value) {
    if (value == '\'' || value == '\\')
        f2c_buffer_printf(output, "'\\%c'", value);
    else if (value >= 32U && value <= 126U)
        f2c_buffer_printf(output, "'%c'", value);
    else
        f2c_buffer_printf(output, "0x%02X", (unsigned int)value);
}

char *f2c_constant_storage_initializer(Unit *unit, const Symbol *symbol) {
    F2cConstantArray array = {0};
    Buffer output = {.limit = unit->context != NULL ? unit->context->output.limit
                                                    : F2C_DEFAULT_MAX_OUTPUT_BYTES};
    F2cShape shape;
    size_t count, character_length;
    if (!f2c_evaluate_constant_storage_layout(unit, symbol, &shape, &character_length) ||
        !f2c_constant_shape_count(&shape, &count))
        return NULL;
    if (symbol->type == TYPE_CHARACTER) {
        if (character_length != 0U && count > SIZE_MAX / character_length) {
            if (unit->context != NULL)
                unit->context->output.limit_exceeded = 1;
            return NULL;
        }
        count *= character_length;
    }
    /* Even a single-character scalar followed by a separator occupies three
     * bytes. Check this lower bound before allocating or expanding dense IR;
     * the output buffer still enforces the exact literal size afterwards. */
    if (output.limit != 0U && count > output.limit / 3U) {
        if (unit->context != NULL)
            unit->context->output.limit_exceeded = 1;
        return NULL;
    }
    if (!f2c_evaluate_constant_storage(unit, symbol, &array))
        return NULL;
    size_t emitted = 0U;
    const int aggregate = symbol->rank != 0U || symbol->type == TYPE_CHARACTER;
    if (aggregate)
        f2c_buffer_append(&output, "{");
    for (size_t index = 0U; index < array.count && !output.failed && !output.limit_exceeded;
         ++index) {
        const F2cConstantValue *value = &array.values[index];
        if (value->type.type == TYPE_CHARACTER) {
            for (size_t byte = 0U; byte < value->payload.character.length && !output.limit_exceeded;
                 ++byte) {
                if (emitted++ != 0U)
                    f2c_buffer_append(&output, ", ");
                append_character(&output, (unsigned char)value->payload.character.bytes[byte]);
            }
        } else {
            char *item = f2c_emit_constant_value(unit, value);
            if (item == NULL) {
                f2c_constant_array_free(&array);
                free(output.data);
                return NULL;
            }
            if (emitted++ != 0U)
                f2c_buffer_append(&output, ", ");
            f2c_buffer_append(&output, item);
            free(item);
        }
    }
    if (aggregate) {
        if (emitted == 0U)
            f2c_buffer_append(&output, "0");
        f2c_buffer_append(&output, "}");
    }
    f2c_constant_array_free(&array);
    if (output.limit_exceeded && unit->context != NULL)
        unit->context->output.limit_exceeded = 1;
    return f2c_buffer_take(&output);
}
