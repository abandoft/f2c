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
char *f2c_descriptor_source_stride(Unit *unit, const Symbol *symbol, size_t dimension);
char *f2c_descriptor_storage_designator(Unit *unit, const F2cExpr *expression);
char *f2c_descriptor_dimension_lower(Unit *unit, const F2cExpr *expression, size_t dimension);
char *f2c_descriptor_dimension_upper(Unit *unit, const F2cExpr *expression, size_t dimension);
char *f2c_descriptor_dimension_extent(Unit *unit, const F2cExpr *expression, size_t dimension);
char *f2c_descriptor_expression_stride(Unit *unit, const F2cExpr *expression, size_t dimension);
char *f2c_descriptor_element_designator(Unit *unit, const F2cExpr *expression, char **indices,
                                        size_t count);
size_t f2c_descriptor_selector_offset(const F2cExpr *expression);
const F2cExpr *f2c_descriptor_selector(const F2cExpr *expression, size_t dimension);
int f2c_descriptor_association_view(Buffer *prelude, Unit *unit, const F2cExpr *expression,
                                    int depth, F2cDescriptorView *view);
int f2c_descriptor_materialize_view(Buffer *prelude, Buffer *cleanup, Unit *unit,
                                    const F2cExpr *expression, F2cIntent intent, size_t identifier,
                                    int depth, F2cDescriptorView *view);
void f2c_descriptor_view_free(F2cDescriptorView *view);

#endif
