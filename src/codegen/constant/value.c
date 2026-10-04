#include "codegen/constant/private.h"

#include "codegen/literal/integer.h"
#include "codegen/literal/real.h"
#include "codegen/type/initialization.h"
#include "internal/f2c.h"

#include <stdlib.h>

char *f2c_emit_constant_value(Unit *unit, const F2cConstantValue *value) {
    const Type type = value->type.type;
    const int kind = value->type.kind;
    Buffer output = {0};
    if (type == TYPE_INTEGER)
        return f2c_integer_constant_literal(value->payload.integer, kind);
    if (type == TYPE_LOGICAL)
        return f2c_strdup(value->payload.integer != 0 ? "true" : "false");
    if (type == TYPE_DERIVED)
        return f2c_derived_constructor_initializer(unit, value->payload.derived);
    if (type == TYPE_REAL || type == TYPE_DOUBLE || type == TYPE_COMPLEX ||
        type == TYPE_DOUBLE_COMPLEX) {
        char *real = f2c_real_constant_literal(value->payload.number.real, kind);
        if (real == NULL)
            return NULL;
        if (type == TYPE_REAL || type == TYPE_DOUBLE) {
            f2c_buffer_printf(&output, "(%s)(%s)", f2c_c_type_kind(type, kind), real);
        } else {
            char *imaginary = f2c_real_constant_literal(value->payload.number.imaginary, kind);
            if (imaginary == NULL) {
                free(real);
                return NULL;
            }
            f2c_buffer_printf(&output, "%s((%s)(%s), (%s)(%s))",
                              kind == 4 ? "F2C_COMPLEX_FLOAT_INITIALIZER"
                                        : "F2C_COMPLEX_DOUBLE_INITIALIZER",
                              f2c_c_type_kind(TYPE_REAL, kind), real,
                              f2c_c_type_kind(TYPE_REAL, kind), imaginary);
            free(imaginary);
        }
        free(real);
        return f2c_buffer_take(&output);
    }
    return NULL;
}
