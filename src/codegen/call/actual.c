#include "codegen/call/private.h"

#include "codegen/array/private.h"
#include "codegen/lowering/private.h"

#include <stdlib.h>

static const F2cExpr *actual_value(const F2cExpr *actual) {
    return actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT && actual->child_count == 1U
               ? actual->children[0]
               : actual;
}

int f2c_call_actual_guaranteed_contiguous(const F2cExpr *actual) {
    return f2c_expression_is_simply_contiguous(actual_value(actual));
}

int f2c_call_actual_requires_materialization(Unit *unit, const Symbol *callee,
                                             const F2cExpr *actual, size_t parameter) {
    F2cDescriptorView view = {0};
    int direct;
    actual = actual_value(actual);
    if (actual == NULL || actual->rank == 0U || actual->kind == F2C_EXPR_ABSENT_ARGUMENT ||
        (f2c_lowering_is_array_temporary(unit, actual) &&
         actual->result_use != F2C_FUNCTION_RESULT_REFERENCE))
        return 0;
    if (callee == NULL || parameter >= callee->external_parameter_count ||
        !callee->external_parameter_descriptor[parameter])
        return !f2c_call_actual_guaranteed_contiguous(actual);
    if (callee->external_parameter_contiguous[parameter] &&
        !callee->external_parameter_pointer[parameter] &&
        !f2c_call_actual_guaranteed_contiguous(actual))
        return 1;
    direct = actual->symbol != NULL && !actual->symbol->equivalence_unaligned &&
             f2c_descriptor_view(unit, actual, &view) && view.rank == actual->rank;
    f2c_descriptor_view_free(&view);
    return !direct;
}

int f2c_call_cache_actual_view(Buffer *setup, Unit *unit, const F2cExpr *actual,
                               const F2cDescriptorView *view, int depth) {
    size_t dimension;
    if (setup == NULL || actual == NULL || view == NULL || view->rank != actual->rank ||
        view->data == NULL || !f2c_lowering_copy_code(unit, actual, view->data) ||
        !f2c_lowering_set_array_temporary(unit, actual, 1) ||
        !f2c_lowering_set_storage_access(unit, actual, view->storage_qualifiers,
                                         view->readonly_storage) ||
        (actual->type == TYPE_CHARACTER &&
         (view->character_length == NULL ||
          !f2c_lowering_copy_character_length(unit, actual, view->character_length))))
        return 0;
    for (dimension = 0U; dimension < view->rank; ++dimension) {
        f2c_array_indent(setup, depth);
        f2c_buffer_printf(setup, "const size_t %s_extent_%zu = (size_t)(%s);\n", view->data,
                          dimension + 1U, view->extent[dimension]);
        f2c_array_indent(setup, depth);
        f2c_buffer_printf(setup, "(void)%s_extent_%zu;\n", view->data, dimension + 1U);
    }
    return 1;
}

int f2c_call_actual_permits_copy(Unit *unit, const Symbol *callee, const F2cExpr *actual,
                                 size_t parameter) {
    actual = actual_value(actual);
    if (callee == NULL || parameter >= callee->external_parameter_count || actual == NULL ||
        !callee->external_parameter_descriptor[parameter] ||
        !callee->external_parameter_target[parameter] ||
        callee->external_parameter_contiguous[parameter] ||
        !f2c_expression_has_target_attribute(actual))
        return 1;
    f2c_diagnostic_span_code(unit->context, F2C_DIAGNOSTIC_UNSUPPORTED, &actual->span, 1,
                             "TARGET actual requires a byte-strided descriptor view; copying "
                             "would invalidate persistent pointer associations");
    return 0;
}
