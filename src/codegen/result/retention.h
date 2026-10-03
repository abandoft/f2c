#ifndef F2C_CODEGEN_RESULT_RETENTION_H
#define F2C_CODEGEN_RESULT_RETENTION_H

#include "codegen/array/private.h"

/* Retain every runtime instance, not just the last evaluation of an IR node.
 * The generated records are local C storage; there is no support-library ABI. */
struct F2cResultRetentionScope {
    Unit *unit;
    char *name;
    size_t *temporaries;
    size_t count;
    size_t capacity;
    int active;
};

int f2c_result_retention_begin(F2cResultRetentionScope *scope, Unit *unit,
                               const F2cExpr *expression, const char *name, Buffer *output,
                               int depth);
/* Unlike constructor retention, a statement owns the root values as well. */
int f2c_result_retention_begin_values(F2cResultRetentionScope *scope, Unit *unit,
                                      F2cExpr *const *expressions, size_t count, const char *name,
                                      Buffer *output, int depth);
int f2c_result_retention_capture(F2cResultRetentionScope *scope, const F2cArrayCleanupList *cleanup,
                                 Buffer *output, int depth);
int f2c_result_retention_release(const F2cResultRetentionScope *scope, Buffer *output, int depth);
void f2c_result_retention_clear(F2cResultRetentionScope *scope);

#endif
