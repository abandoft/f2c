#ifndef F2C_CODEGEN_HOST_PRIVATE_H
#define F2C_CODEGEN_HOST_PRIVATE_H

#include "internal/f2c.h"

int f2c_host_function_result_symbol(const Unit *unit, const Symbol *symbol);
const Symbol *f2c_host_capture_actual(Unit *caller, const Unit *procedure, size_t capture,
                                      const Symbol **formal);
int f2c_host_capture_is_local_descriptor(const Unit *caller, const Symbol *actual);
int f2c_host_capture_needs_descriptor_lifecycle(const Symbol *actual);

#endif
