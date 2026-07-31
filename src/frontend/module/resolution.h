#ifndef F2C_FRONTEND_MODULE_RESOLUTION_H
#define F2C_FRONTEND_MODULE_RESOLUTION_H

#include "frontend/token.h"
#include "internal/context.h"

int f2c_supported_intrinsic_module(const F2cToken *name);
int f2c_permitted_external_module(const Context *context, const F2cToken *name);

#endif
