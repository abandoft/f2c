#ifndef F2C_CODEGEN_ARRAY_VIEW_H
#define F2C_CODEGEN_ARRAY_VIEW_H

#include "internal/f2c.h"

/* Describes the storage actually read by a kernel. A lowered value temporary is
 * unqualified even when its source expression read a qualified object. */
typedef struct F2cArrayView {
    char *pointer;
    char *count;
    char *stride;
    unsigned int storage_qualifiers;
} F2cArrayView;

int f2c_array_view(Unit *unit, const F2cExpr *array, F2cArrayView *view, int *supported);
void f2c_array_view_discard(F2cArrayView *view);

#endif
