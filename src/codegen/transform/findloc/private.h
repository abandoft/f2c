#ifndef F2C_CODEGEN_TRANSFORM_FINDLOC_PRIVATE_H
#define F2C_CODEGEN_TRANSFORM_FINDLOC_PRIVATE_H

#include "codegen/transform/private.h"

typedef struct F2cFindloc {
    Context *context;
    Unit *unit;
    Symbol *target;
    const F2cExpr *call;
    TransformArray source, mask;
    char *prefix, *condition, *comparison;
    int result_kind;
    size_t result_rank;
    int has_dimension;
} F2cFindloc;

int f2c_findloc_prepare(F2cFindloc *lowering, int depth);
void f2c_findloc_scan(F2cFindloc *lowering, int depth);
int f2c_findloc_result_prepare(F2cFindloc *lowering, int depth);
int f2c_findloc_result_commit(F2cFindloc *lowering, int depth);
void f2c_findloc_store_position(F2cFindloc *lowering, const char *output_index, int depth);

#endif
