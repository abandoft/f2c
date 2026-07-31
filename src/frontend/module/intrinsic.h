#ifndef F2C_FRONTEND_MODULE_INTRINSIC_H
#define F2C_FRONTEND_MODULE_INTRINSIC_H

#include "ast/declaration/use.h"
#include "internal/context.h"

int f2c_supported_intrinsic_module(const F2cToken *name);
void f2c_import_intrinsic_module(Context *context, Unit *unit, const F2cUseStatementSyntax *syntax);

#endif
