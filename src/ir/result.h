#ifndef F2C_IR_RESULT_H
#define F2C_IR_RESULT_H

#include "ir/type.h"

/* Result ownership is a procedure characteristic, not the allocation
 * provenance of a pointer target. Only nonpointer results transfer storage. */
typedef enum F2cFunctionResultKind {
    F2C_FUNCTION_RESULT_NONE,
    F2C_FUNCTION_RESULT_SCALAR_VALUE,
    F2C_FUNCTION_RESULT_ARRAY_VALUE,
    F2C_FUNCTION_RESULT_ALLOCATABLE,
    F2C_FUNCTION_RESULT_POINTER
} F2cFunctionResultKind;

/* A pointer result may supply either a value snapshot or its target identity. */
typedef enum F2cFunctionResultUse {
    F2C_FUNCTION_RESULT_VALUE,
    F2C_FUNCTION_RESULT_REFERENCE
} F2cFunctionResultUse;

F2cFunctionResultKind f2c_unit_result_kind(const Unit *unit);
F2cFunctionResultKind f2c_procedure_result_kind(const Symbol *procedure);
void f2c_bind_expression_result(F2cExpr *expression);
int f2c_result_kind_uses_descriptor(F2cFunctionResultKind kind);
int f2c_result_kind_owns_storage(F2cFunctionResultKind kind);

#endif
