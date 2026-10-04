#ifndef F2C_CODEGEN_LOOP_CONTROL_H
#define F2C_CODEGEN_LOOP_CONTROL_H

#include "internal/f2c.h"

const char *f2c_integer_loop_suffix(int kind);
char *f2c_loop_local_prefix(Unit *unit, const char *preferred, size_t identifier);
void f2c_loop_emit_parameter(Buffer *output, const char *prefix, size_t identifier,
                             const char *role, int kind, const F2cExpr *source, const char *code,
                             int depth);
void f2c_loop_emit_state(Buffer *output, const char *prefix, size_t identifier, const char *status,
                         int formatted_status, int depth);
void f2c_loop_emit_real_state(Buffer *output, const F2cExpr *variable, const char *prefix,
                              size_t identifier, int depth);
char *f2c_loop_store_expression(Unit *unit, const F2cExpr *variable, const char *value);
char *f2c_loop_advance_expression(Unit *unit, const F2cExpr *variable, const char *prefix,
                                  size_t identifier);
void f2c_loop_emit_header(Buffer *output, const char *prefix, size_t identifier,
                          const char *advance, const char *status_condition, int depth);

#endif
