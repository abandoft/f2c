#ifndef F2C_CLI_INCLUDE_H
#define F2C_CLI_INCLUDE_H

#include "f2c/f2c.h"

typedef struct F2cCliIncludeContext {
    const char **paths;
    size_t count;
} F2cCliIncludeContext;

F2cIncludeStatus f2c_cli_resolve_include(const F2cIncludeRequest *request, F2cIncludeSource *result,
                                         void *user_data);
void f2c_cli_release_include(F2cIncludeSource *source, void *user_data);

#endif
