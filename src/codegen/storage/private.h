#ifndef F2C_CODEGEN_STORAGE_PRIVATE_H
#define F2C_CODEGEN_STORAGE_PRIVATE_H

#include "internal/f2c.h"
#include "ir/storage.h"

typedef enum F2cObjectStateProperty {
    F2C_OBJECT_DATA,
    F2C_OBJECT_LOWER,
    F2C_OBJECT_EXTENT,
    F2C_OBJECT_STRIDE,
    F2C_OBJECT_CHARACTER_LENGTH,
    F2C_OBJECT_DEALLOCATABLE
} F2cObjectStateProperty;

typedef enum F2cStorageEmissionStyle {
    F2C_STORAGE_STATEMENT,
    F2C_STORAGE_COMMA_EXPRESSION
} F2cStorageEmissionStyle;

/* An optional physical binding names a previously evaluated component owner.
 * It never changes the typed reference's access or ownership contract. */
char *f2c_storage_bound_property(Unit *unit, const F2cStorageReference *reference,
                                 F2cObjectStateProperty property, size_t dimension,
                                 const char *physical_binding, int writable);
char *f2c_storage_write_property(Unit *unit, const F2cStorageReference *reference,
                                 F2cObjectStateProperty property, size_t dimension);
int f2c_storage_emit_store(Buffer *output, Unit *unit, const F2cStorageReference *reference,
                           F2cObjectStateProperty property, size_t dimension,
                           const char *physical_binding, const char *value, int depth);
int f2c_storage_emit_contiguous_dimension(Buffer *output, Unit *unit,
                                          const F2cStorageReference *reference, size_t dimension,
                                          const char *physical_binding, const char *lower,
                                          const char *extent, int depth);
int f2c_storage_emit_dummy_entry(Buffer *output, Unit *unit, Symbol *symbol, int depth);

/* Commit an owned call bridge to its original object. descriptor_pointer and
 * physical_binding must name already evaluated storage, not effectful code.
 * Incoming dynamic dummies forward their descriptor identity instead. */
int f2c_storage_emit_descriptor_commit(Buffer *output, Unit *unit,
                                       const F2cStorageReference *reference,
                                       const char *physical_binding, const char *descriptor_pointer,
                                       F2cStorageEmissionStyle style, int depth);

char *f2c_storage_read_property(Unit *unit, const F2cStorageReference *reference,
                                F2cObjectStateProperty property, size_t dimension);
char *f2c_storage_symbol_data(Unit *unit, const Symbol *symbol);
char *f2c_storage_symbol_property(Unit *unit, const Symbol *symbol, F2cObjectStateProperty property,
                                  size_t dimension);
char *f2c_storage_linear_offset(Unit *unit, const F2cStorageReference *reference,
                                const char *index);
char *f2c_storage_linear_element(Unit *unit, const F2cStorageReference *reference,
                                 const char *index);

#endif
