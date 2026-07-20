#ifndef F2C_CODEGEN_STATEMENT_PRIVATE_H
#define F2C_CODEGEN_STATEMENT_PRIVATE_H

#include "internal/f2c.h"

int f2c_statement_unit_has_label_target(const Unit *unit, const char *label);
int f2c_statement_unit_targets_construct(const Unit *unit, const F2cStatement *target,
                                         F2cStatementKind transfer_kind);
size_t f2c_statement_unit_index(const Unit *unit, const F2cStatement *statement);

char *f2c_emit_statement_expression(Context *context, Unit *unit, const F2cExpr *expression,
                                    size_t line);
int f2c_emit_do_begin(Context *context, Unit *unit, const F2cStatement *statement,
                      size_t source_line, int *depth);
int f2c_emit_do_end(Context *context, Unit *unit, const F2cStatement *opener, size_t source_line,
                    int *depth);
int f2c_emit_nullify_statement(Context *context, Unit *unit, const F2cStatement *statement,
                               size_t line, int depth);
int f2c_emit_pointer_assignment_statement(Context *context, Unit *unit,
                                          const F2cStatement *statement, size_t line, int depth);
int f2c_emit_assignment_statement(Context *context, Unit *unit, const F2cStatement *statement,
                                  size_t line, int depth);
int f2c_emit_mvbits_statement(Context *context, Unit *unit, const F2cStatement *statement,
                              int depth);
int f2c_emit_random_statement(Context *context, Unit *unit, const F2cStatement *statement,
                              int depth);
int f2c_emit_time_statement(Context *context, Unit *unit, const F2cStatement *statement, int depth);

#endif
