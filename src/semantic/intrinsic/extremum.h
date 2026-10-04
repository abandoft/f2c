#ifndef F2C_SEMANTIC_INTRINSIC_EXTREMUM_H
#define F2C_SEMANTIC_INTRINSIC_EXTREMUM_H

#include "ir/expression.h"

typedef enum F2cExtremumBindingError {
    F2C_EXTREMUM_BINDING_VALID,
    F2C_EXTREMUM_BINDING_UNKNOWN_NAME,
    F2C_EXTREMUM_BINDING_DUPLICATE,
    F2C_EXTREMUM_BINDING_POSITIONAL_AFTER_KEYWORD,
    F2C_EXTREMUM_BINDING_MISSING,
    F2C_EXTREMUM_BINDING_INVALID_VALUE,
    F2C_EXTREMUM_BINDING_ALLOCATION
} F2cExtremumBindingError;

typedef struct F2cExtremumActual {
    const F2cExpr *value;
    const F2cExpr *actual;
    size_t index;
    size_t source_order;
} F2cExtremumActual;

#define F2C_EXTREMUM_INLINE_ARGUMENTS 8U

/* Compact, non-owning view ordered by dummy number. Optional A3... may be
 * omitted, so allocation is proportional to actual count, never to an A<n>
 * keyword. Initialize in place: the inline storage is not transferable.
 */
typedef struct F2cExtremumBinding {
    F2cExtremumActual inline_values[F2C_EXTREMUM_INLINE_ARGUMENTS];
    F2cExtremumActual *values;
    size_t count;
    F2cExtremumBindingError error;
    const F2cExpr *offending;
    size_t error_index;
} F2cExtremumBinding;

int f2c_extremum_bind(const F2cExpr *expression, F2cExtremumBinding *binding);
void f2c_extremum_binding_clear(F2cExtremumBinding *binding);

#endif
