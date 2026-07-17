#ifndef F2C_CODEGEN_DESCRIPTOR_PRIVATE_H
#define F2C_CODEGEN_DESCRIPTOR_PRIVATE_H

#include "internal/f2c.h"

typedef struct F2cDescriptorView {
    char *data;
    char *character_length;
    char *lower[F2C_MAX_RANK];
    char *extent[F2C_MAX_RANK];
    char *stride[F2C_MAX_RANK];
    size_t rank;
} F2cDescriptorView;

int f2c_descriptor_view(Unit *unit, const F2cExpr *expression, F2cDescriptorView *view);
int f2c_descriptor_materialize_view(Buffer *prelude, Buffer *cleanup, Unit *unit,
                                    const F2cExpr *expression, F2cIntent intent, size_t identifier,
                                    int depth, F2cDescriptorView *view);
void f2c_descriptor_view_free(F2cDescriptorView *view);

#endif
