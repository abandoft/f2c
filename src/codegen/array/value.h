#ifndef F2C_CODEGEN_ARRAY_VALUE_H
#define F2C_CODEGEN_ARRAY_VALUE_H

#include "internal/f2c.h"

typedef struct F2cArrayValue {
    const F2cExpr *expression;
    Symbol *symbol;
    Type type;
    F2cDerivedType *derived_type;
    char *pointer;
    char *count;
    char *element_length;
    char *extents[F2C_MAX_RANK];
    size_t rank;
    int temporary;
} F2cArrayValue;

int f2c_array_value_view(Unit *unit, const F2cExpr *expression, F2cArrayValue *value);
int f2c_array_value_materialize(Context *context, Unit *unit, F2cArrayValue *value,
                                const char *role, int depth);
void f2c_array_value_emit_cleanup(Context *context, const F2cArrayValue *value, int depth);
void f2c_array_value_clear(F2cArrayValue *value);

#endif
