#ifndef F2C_FRONTEND_MODULE_CONSTANT_H
#define F2C_FRONTEND_MODULE_CONSTANT_H

#include "ast/declaration/use.h"
#include "frontend/module_constants.h"

int f2c_use_name_is_renamed(const F2cUseStatementSyntax *syntax, const char *name);
void f2c_import_constant_module(Context *context, Unit *unit, const F2cUseStatementSyntax *syntax,
                                const char *module_name, const char *c_name_prefix,
                                const F2cModuleConstant *constants, size_t constant_count);

#endif
