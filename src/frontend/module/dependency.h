#ifndef F2C_FRONTEND_MODULE_DEPENDENCY_H
#define F2C_FRONTEND_MODULE_DEPENDENCY_H

#include "internal/context.h"

int f2c_build_module_analysis_order(Context *context, size_t **order);

#endif
