#include "frontend/pipeline.h"

#include "frontend/frontend.h"
#include "frontend/module/dependency.h"
#include "semantic/semantic.h"

#include <stdint.h>
#include <stdlib.h>

static int apply_module_analysis_order(Context *context, const size_t *order) {
    Unit *ordered;
    size_t index;
    if (context->modules.count == 0U)
        return 1;
    if (context->modules.count > SIZE_MAX / sizeof(*ordered))
        return 0;
    ordered = (Unit *)malloc(context->modules.count * sizeof(*ordered));
    if (ordered == NULL)
        return 0;
    for (index = 0U; index < context->modules.count; ++index)
        ordered[index] = context->modules.items[order[index]];
    free(context->modules.items);
    context->modules.items = ordered;
    context->modules.capacity = context->modules.count;
    return 1;
}

int f2c_build_syntax_program(Context *context) {
    if (context == NULL || context->phase != F2C_COMPILATION_SOURCE)
        return 0;
    if (!f2c_tokenize_lines(context) || !f2c_discover_modules(context) ||
        !f2c_discover_units(context)) {
        if (context->result.error_count == 0U)
            f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1,
                                "out of memory while building syntax program");
        return 0;
    }
    context->phase = F2C_COMPILATION_SYNTAX;
    return context->result.error_count == 0U;
}

int f2c_build_typed_program(Context *context) {
    size_t index;
    size_t *module_order = NULL;
    if (context == NULL || context->phase != F2C_COMPILATION_SYNTAX)
        return 0;
    if (!f2c_build_module_analysis_order(context, &module_order))
        return 0;
    if (!apply_module_analysis_order(context, module_order)) {
        free(module_order);
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1,
                            "out of memory applying project module order");
        return 0;
    }
    free(module_order);
    for (index = 0U; index < context->modules.count; ++index) {
        context->options = &context->modules.items[index].options;
        f2c_analyze_module(context, &context->modules.items[index]);
        if (context->result.error_count == 0U)
            f2c_build_statement_ir(context, &context->modules.items[index]);
    }
    for (index = 0U; index < context->units.count; ++index) {
        context->options = &context->units.items[index].options;
        f2c_analyze_unit(context, &context->units.items[index]);
    }
    if (context->result.error_count == 0U && !f2c_build_procedure_registry(context))
        f2c_diagnostic_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, 1U, 1,
                            "out of memory while building procedure registry");
    if (context->result.error_count == 0U)
        f2c_resolve_derived_semantics(context);
    if (context->result.error_count == 0U) {
        for (index = 0U; index < context->units.count; ++index) {
            context->options = &context->units.items[index].options;
            f2c_validate_implicit_external(context, &context->units.items[index]);
        }
    }
    if (context->result.error_count == 0U) {
        for (index = 0U; index < context->units.count; ++index) {
            context->options = &context->units.items[index].options;
            f2c_build_statement_ir(context, &context->units.items[index]);
        }
    }
    if (context->result.error_count == 0U)
        f2c_validate_project_storage(context);
    if (context->result.error_count != 0U)
        return 0;
    context->phase = F2C_COMPILATION_TYPED_IR;
    return 1;
}
