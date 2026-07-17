#ifndef F2C_FRONTEND_MODULE_CONSTANTS_H
#define F2C_FRONTEND_MODULE_CONSTANTS_H

#include "internal/context.h"

typedef struct F2cModuleConstant {
    const char *name;
    Type type;
    const char *initializer;
} F2cModuleConstant;

const F2cModuleConstant *f2c_la_constants(size_t *count);
int f2c_has_la_constants_module(const Context *context);

#endif
