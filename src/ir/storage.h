#ifndef F2C_IR_STORAGE_H
#define F2C_IR_STORAGE_H

#include "ir/type.h"

typedef enum F2cObjectStateSource {
    F2C_OBJECT_STATE_NONE,
    F2C_OBJECT_STATE_LOCAL,
    F2C_OBJECT_STATE_DESCRIPTOR,
    F2C_OBJECT_STATE_COMPONENT
} F2cObjectStateSource;

/* State qualification concerns the object's association/allocation metadata;
 * object qualification also concerns accesses to its designated data. Neither
 * belongs to a freshly computed value. This reference is derived from typed IR
 * rather than reconstructed from the generated C spelling. */
typedef struct F2cStorageReference {
    const Symbol *symbol;
    const F2cExpr *expression;
    const F2cExpr *owner;
    F2cObjectStateSource state_source;
    unsigned int object_qualifiers;
    unsigned int state_qualifiers;
    int readonly_storage;
    int readonly_state;
} F2cStorageReference;

F2cStorageReference f2c_ir_symbol_storage_reference(const Symbol *symbol);
F2cStorageReference f2c_ir_storage_reference(const F2cExpr *expression);
int f2c_ir_storage_state_definable(const F2cExpr *expression);

#endif
