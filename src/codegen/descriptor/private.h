#ifndef F2C_CODEGEN_DESCRIPTOR_PRIVATE_H
#define F2C_CODEGEN_DESCRIPTOR_PRIVATE_H

#include "internal/f2c.h"

typedef struct F2cDescriptorView {
    char *data;
    char *character_length;
    char *record_size;
    char *lower[F2C_MAX_RANK];
    char *extent[F2C_MAX_RANK];
    char *byte_stride[F2C_MAX_RANK];
    size_t rank;
    unsigned int storage_qualifiers;
    int readonly_storage;
} F2cDescriptorView;

/* Logical record width is independent of each dimension's signed byte stride.
 * A substring retains its parent's stride while exposing a shorter record. */
char *f2c_descriptor_symbol_record_size(Unit *unit, const Symbol *symbol);
char *f2c_descriptor_expression_record_size(Unit *unit, const F2cExpr *expression);
char *f2c_descriptor_dense_byte_stride(Unit *unit, const F2cExpr *expression, size_t dimension);

/* C address qualification is independent of Fortran object identity and intent.
 * A materialized value owns ordinary storage; an alias view keeps its source's
 * qualification. No cast that drops a qualifier is needed at transport. */
const char *f2c_descriptor_address_member(unsigned int qualifiers, int readonly_storage);
int f2c_descriptor_readonly_storage(const F2cExpr *expression);

int f2c_descriptor_view(Unit *unit, const F2cExpr *expression, F2cDescriptorView *view);
int f2c_descriptor_project_view(Buffer *prelude, Unit *unit, const F2cExpr *expression, int depth,
                                F2cDescriptorView *view);
char *f2c_descriptor_source_byte_stride(Unit *unit, const Symbol *symbol, size_t dimension);
char *f2c_descriptor_storage_designator(Unit *unit, const F2cExpr *expression);
char *f2c_descriptor_dimension_lower(Unit *unit, const F2cExpr *expression, size_t dimension);
char *f2c_descriptor_dimension_upper(Unit *unit, const F2cExpr *expression, size_t dimension);
char *f2c_descriptor_dimension_extent(Unit *unit, const F2cExpr *expression, size_t dimension);
char *f2c_descriptor_expression_byte_stride(Unit *unit, const F2cExpr *expression,
                                            size_t dimension);
char *f2c_descriptor_element_designator(Unit *unit, const F2cExpr *expression, char **indices,
                                        size_t count);
size_t f2c_descriptor_selector_offset(const F2cExpr *expression);
const F2cExpr *f2c_descriptor_selector(const F2cExpr *expression, size_t dimension);
int f2c_descriptor_association_view(Buffer *prelude, Unit *unit, const F2cExpr *expression,
                                    int depth, F2cDescriptorView *view);
int f2c_descriptor_materialize_view(Buffer *prelude, Buffer *cleanup, Unit *unit,
                                    const F2cExpr *expression, F2cIntent intent,
                                    size_t minimum_count, size_t identifier, int depth,
                                    F2cDescriptorView *view);
void f2c_descriptor_view_free(F2cDescriptorView *view);
char *f2c_descriptor_section_address(Unit *unit, const F2cExpr *expression, const char *element,
                                     const F2cDescriptorView *view);

#endif
