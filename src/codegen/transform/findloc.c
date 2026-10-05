#include "codegen/transform/findloc/private.h"

#include <stdlib.h>

int f2c_transform_emit_findloc(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                               size_t line, int depth) {
    if (target->rank != call->rank)
        return 0;
    F2cFindloc lowering = {0};
    lowering.context = context;
    lowering.unit = unit;
    lowering.target = target;
    lowering.call = call;
    lowering.result_kind = call->type_kind != 0 ? call->type_kind : 4;
    lowering.result_rank = call->rank;
    lowering.has_dimension = f2c_transform_argument(call, "dim", 2U) != NULL;
    const size_t output_start = context->output.length;
    int success = f2c_type_is_numeric(target->type) &&
                  (lowering.result_kind == 1 || lowering.result_kind == 2 ||
                   lowering.result_kind == 4 || lowering.result_kind == 8) &&
                  f2c_findloc_prepare(&lowering, depth) &&
                  f2c_findloc_result_prepare(&lowering, depth + 1);
    if (success) {
        f2c_findloc_scan(&lowering, depth + 1);
        f2c_transform_emit_array_cleanup(context, &lowering.source, depth + 1);
        f2c_transform_emit_array_cleanup(context, &lowering.mask, depth + 1);
        success = f2c_findloc_result_commit(&lowering, depth + 1);
    }
    if (success) {
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
    } else {
        context->output.length = output_start;
        if (context->output.data != NULL)
            context->output.data[output_start] = '\0';
        f2c_diagnostic(context, line, 1,
                       "FINDLOC requires a supported intrinsic comparison, conformable MASK, "
                       "and a result shape selected by DIM");
    }
    f2c_transform_free_array(&lowering.source);
    f2c_transform_free_array(&lowering.mask);
    free(lowering.prefix);
    free(lowering.condition);
    free(lowering.comparison);
    return 1;
}
