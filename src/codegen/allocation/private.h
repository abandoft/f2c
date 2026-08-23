#ifndef F2C_CODEGEN_ALLOCATION_PRIVATE_H
#define F2C_CODEGEN_ALLOCATION_PRIVATE_H

#include "codegen/array/private.h"
#include "codegen/array/value.h"

typedef struct F2cAllocationModel {
    F2cExpr *owned_expression;
    const F2cExpr *expression;
    F2cArrayValue array;
    F2cArrayCleanupList cleanup;
    char *scalar_name;
    char *character_length;
    char *availability;
    size_t identifier;
    int source;
    int prepared;
    int guarded;
} F2cAllocationModel;

int f2c_allocation_model_prepare(Context *context, Unit *unit, const F2cStatement *statement,
                                 const F2cExpr *expression, int source, int depth,
                                 F2cAllocationModel *model);
char *f2c_allocation_model_lower(Unit *unit, const F2cAllocationModel *model, size_t dimension);
char *f2c_allocation_model_upper(Unit *unit, const F2cAllocationModel *model, size_t dimension);
char *f2c_allocation_model_extent(const F2cAllocationModel *model, size_t dimension);
char *f2c_allocation_model_character_length(Unit *unit, const F2cAllocationModel *model);
int f2c_allocation_model_emit_source(Context *context, const Symbol *target,
                                     const F2cAllocationModel *model, int depth);
void f2c_allocation_model_emit_cleanup(Context *context, Unit *unit,
                                       const F2cAllocationModel *model, int depth);
void f2c_allocation_model_clear(Unit *unit, F2cAllocationModel *model);

#endif
