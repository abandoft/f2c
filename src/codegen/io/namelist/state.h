#ifndef F2C_CODEGEN_NAMELIST_STATE_H
#define F2C_CODEGEN_NAMELIST_STATE_H

#include "internal/f2c.h"

int f2c_namelist_has_descriptor_state(const Symbol *symbol);
int f2c_namelist_emit_state_snapshot(Context *context, Unit *unit, const Symbol *symbol,
                                     size_t member, int depth);
int f2c_namelist_emit_state_commit(Context *context, Unit *unit, const Symbol *symbol,
                                   size_t member, int depth);

#endif
