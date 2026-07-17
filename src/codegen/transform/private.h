#ifndef F2C_CODEGEN_TRANSFORM_PRIVATE_H
#define F2C_CODEGEN_TRANSFORM_PRIVATE_H

#include "internal/f2c.h"

typedef struct TransformArray {
    const F2cExpr *expression;
    Symbol *symbol;
    Type type;
    F2cDerivedType *derived_type;
    char *pointer;
    char *count;
    char *element_length;
    char *extents[F2C_MAX_RANK];
    size_t rank;
} TransformArray;

void f2c_transform_indent(Buffer *output, int depth);
const F2cExpr *f2c_transform_argument_value(const F2cExpr *argument);
const F2cExpr *f2c_transform_argument(const F2cExpr *call, const char *keyword, size_t position);
char *f2c_transform_emit_expression(Unit *unit, const F2cExpr *expression);
void f2c_transform_free_array(TransformArray *array);
int f2c_transform_array_view(Unit *unit, const F2cExpr *expression, TransformArray *array);
int f2c_transform_supported_element_type(const Symbol *target);
int f2c_transform_compatible_array(const Symbol *target, const TransformArray *array);

#endif
