#ifndef F2C_CODEGEN_STATEMENT_PRIVATE_H
#define F2C_CODEGEN_STATEMENT_PRIVATE_H

#include "internal/f2c.h"

int f2c_statement_unit_has_label_target(const Unit *unit, const char *label);
int f2c_statement_unit_targets_construct(const Unit *unit, const F2cStatement *target,
                                         F2cStatementKind transfer_kind);
size_t f2c_statement_label_line(const Unit *unit, const char *label);

#endif
