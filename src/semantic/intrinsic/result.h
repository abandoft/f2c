#ifndef F2C_SEMANTIC_INTRINSIC_RESULT_H
#define F2C_SEMANTIC_INTRINSIC_RESULT_H

#include "semantic/intrinsic.h"

typedef struct F2cIntrinsicResult {
    F2cScalarType scalar;
    F2cDerivedType *derived_type;
    const F2cExpr *type_source;
    size_t rank;
} F2cIntrinsicResult;

/* Bind by the specification's formal slot, never the written argument order. */
const F2cExpr *f2c_intrinsic_argument_at(const F2cIntrinsicSignature *signature,
                                         F2cExpr *const *arguments, size_t count, size_t slot);
size_t f2c_intrinsic_result_rank(const F2cIntrinsicSignature *signature, F2cExpr *const *arguments,
                                 size_t count);
/* Only for synthesized C/libm calls; registered Fortran calls use the model. */
Type f2c_generated_intrinsic_type(const char *name, Type first);
/* Result inference is shared by AST construction and postorder semantic refresh.
 * A zero kind on an intrinsic scalar records an unresolved/invalid selector;
 * dedicated argument validation retains responsibility for its diagnostic. */
F2cIntrinsicResult f2c_intrinsic_result(Unit *unit, const F2cIntrinsicSignature *signature,
                                        F2cExpr *const *arguments, size_t count);
void f2c_intrinsic_refresh_result(Unit *unit, F2cExpr *expression);
void f2c_intrinsic_refresh_shape(Unit *unit, F2cExpr *expression,
                                 const F2cIntrinsicSignature *signature);

#endif
