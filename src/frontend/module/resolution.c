#include "frontend/module/resolution.h"

int f2c_supported_intrinsic_module(const F2cToken *name) {
    return f2c_token_equals(name, "iso_fortran_env");
}

int f2c_permitted_external_module(const Context *context, const F2cToken *name) {
    size_t index;
    if (context == NULL || name == NULL)
        return 0;
    for (index = 0U; index < context->external_module_count; ++index)
        if (f2c_token_equals(name, context->external_module_names[index]))
            return 1;
    return 0;
}
