#ifndef F2C_CODEGEN_CALL_PRIVATE_H
#define F2C_CODEGEN_CALL_PRIVATE_H

#include "codegen/descriptor/private.h"

int f2c_call_actual_guaranteed_contiguous(const F2cExpr *actual);
int f2c_call_actual_requires_materialization(Unit *unit, const Symbol *callee,
                                             const F2cExpr *actual, size_t parameter);
int f2c_call_actual_permits_copy(Unit *unit, const Symbol *callee, const F2cExpr *actual,
                                 size_t parameter);
int f2c_call_cache_actual_view(Buffer *setup, Unit *unit, const F2cExpr *actual,
                               const F2cDescriptorView *view, int depth);
int f2c_call_expression_requires_materialization(Unit *unit, const F2cExpr *expression);
int f2c_call_materialize_expression(Unit *unit, F2cExpr *expression, size_t identifier,
                                    const char *role, size_t *temporary, Buffer *prelude,
                                    int depth);
char *f2c_call_result_character_length(Unit *unit, const F2cExpr *expression);

#endif
