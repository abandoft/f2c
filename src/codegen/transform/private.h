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
    int temporary;
} TransformArray;

void f2c_transform_indent(Buffer *output, int depth);
const F2cExpr *f2c_transform_argument_value(const F2cExpr *argument);
const F2cExpr *f2c_transform_argument(const F2cExpr *call, const char *keyword, size_t position);
char *f2c_transform_emit_expression(Unit *unit, const F2cExpr *expression);
void f2c_transform_free_array(TransformArray *array);
int f2c_transform_array_view(Unit *unit, const F2cExpr *expression, TransformArray *array);
int f2c_transform_materialize_array(Context *context, Unit *unit, TransformArray *array,
                                    const char *role, int depth);
void f2c_transform_emit_array_cleanup(Context *context, const TransformArray *array, int depth);
void f2c_transform_append_array_store(Buffer *output, const Symbol *target, const char *destination,
                                      const TransformArray *source, const char *source_index);
void f2c_transform_emit_result_count(Context *context, size_t rank, int depth);
void f2c_transform_emit_result_allocation(Context *context, Unit *unit, const Symbol *target,
                                          const F2cExpr *element_source, int depth);
void f2c_transform_emit_result_commit(Context *context, Unit *unit, Symbol *target, size_t rank,
                                      int depth);
void f2c_transform_emit_source_extents(Context *context, const TransformArray *source, int depth);
char *f2c_transform_mask_test(Unit *unit, const F2cExpr *mask, const char *index);
int f2c_transform_supported_element_type(const Symbol *target);
int f2c_transform_compatible_array(const Symbol *target, const TransformArray *array);
char *f2c_transform_inquiry_element(Unit *unit, const F2cExpr *value, size_t index);
int f2c_transform_emit_inquiry(Context *context, Unit *unit, const F2cExpr *left,
                               const F2cExpr *call, size_t line, int depth);
int f2c_transform_emit_findloc(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                               size_t line, int depth);
int f2c_transform_emit_matrix(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                              size_t line, int depth);

#endif
