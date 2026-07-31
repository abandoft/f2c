#include "frontend/module/intrinsic.h"

#include "frontend/module/constant.h"

typedef struct F2cIntrinsicModuleSpecification {
    const char *name;
    const char *display_name;
    const char *c_name_prefix;
    const F2cModuleConstant *constants;
    size_t constant_count;
} F2cIntrinsicModuleSpecification;

static const F2cModuleConstant iso_fortran_env_constants[] = {
    {"character_storage_size", TYPE_INTEGER, "8"},
    {"error_unit", TYPE_INTEGER, "0"},
    {"file_storage_size", TYPE_INTEGER, "8"},
    {"input_unit", TYPE_INTEGER, "5"},
    {"int8", TYPE_INTEGER, "1"},
    {"int16", TYPE_INTEGER, "2"},
    {"int32", TYPE_INTEGER, "4"},
    {"int64", TYPE_INTEGER, "8"},
    {"iostat_end", TYPE_INTEGER, "-1"},
    {"iostat_eor", TYPE_INTEGER, "-2"},
    {"numeric_storage_size", TYPE_INTEGER, "32"},
    {"output_unit", TYPE_INTEGER, "6"},
    {"real32", TYPE_INTEGER, "4"},
    {"real64", TYPE_INTEGER, "8"},
    {"real128", TYPE_INTEGER, "-1"},
};

static const F2cIntrinsicModuleSpecification intrinsic_modules[] = {
    {"iso_fortran_env", "ISO_FORTRAN_ENV", "f2c_iso_fortran_env", iso_fortran_env_constants,
     sizeof(iso_fortran_env_constants) / sizeof(iso_fortran_env_constants[0])},
};

static const F2cIntrinsicModuleSpecification *find_intrinsic_module(const F2cToken *name) {
    size_t index;
    if (name == NULL)
        return NULL;
    for (index = 0U; index < sizeof(intrinsic_modules) / sizeof(intrinsic_modules[0]); ++index)
        if (f2c_token_equals(name, intrinsic_modules[index].name))
            return &intrinsic_modules[index];
    return NULL;
}

int f2c_supported_intrinsic_module(const F2cToken *name) {
    return find_intrinsic_module(name) != NULL;
}

void f2c_import_intrinsic_module(Context *context, Unit *unit,
                                 const F2cUseStatementSyntax *syntax) {
    const F2cIntrinsicModuleSpecification *module =
        syntax != NULL ? find_intrinsic_module(syntax->module_name) : NULL;
    if (module == NULL)
        return;
    f2c_import_constant_module(context, unit, syntax, module->display_name, module->c_name_prefix,
                               module->constants, module->constant_count);
}
