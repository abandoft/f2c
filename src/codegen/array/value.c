#include "codegen/array/value.h"

#include "codegen/transform/private.h"

int f2c_array_value_view(Unit *unit, const F2cExpr *expression, F2cArrayValue *value) {
    return f2c_transform_array_view(unit, expression, value);
}

int f2c_array_value_materialize(Context *context, Unit *unit, F2cArrayValue *value,
                                const char *role, int depth) {
    return f2c_transform_materialize_array(context, unit, value, role, depth);
}

void f2c_array_value_emit_cleanup(Context *context, const F2cArrayValue *value, int depth) {
    f2c_transform_emit_array_cleanup(context, value, depth);
}

void f2c_array_value_clear(F2cArrayValue *value) { f2c_transform_free_array(value); }
