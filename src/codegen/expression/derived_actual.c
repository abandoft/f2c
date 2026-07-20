#include "codegen/expression/private.h"

#include <stdint.h>
#include <stdlib.h>

static const F2cExpr *actual_value(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   expression->child_count == 1U
               ? expression->children[0]
               : expression;
}

static int expression_is_owned(const F2cExpr *expression) {
    return expression != NULL &&
           (expression->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR ||
            (expression->kind == F2C_EXPR_CALL && expression->intrinsic != F2C_INTRINSIC_MERGE) ||
            expression->resolved_procedure != NULL);
}

static char *emit_pointer(Unit *unit, const F2cExpr *expression, size_t temporary, int *supported) {
    const char *type_name;
    Buffer result = {0};
    if (expression == NULL || expression->type != TYPE_DERIVED ||
        expression->derived_type == NULL || expression->rank != 0U || temporary == SIZE_MAX) {
        *supported = 0;
        return NULL;
    }
    type_name = expression->derived_type->c_name;
    if (expression->kind == F2C_EXPR_CALL && expression->intrinsic == F2C_INTRINSIC_MERGE) {
        const F2cExpr *true_source =
            f2c_intrinsic_argument(expression->children, expression->child_count, "tsource", 0U);
        const F2cExpr *false_source =
            f2c_intrinsic_argument(expression->children, expression->child_count, "fsource", 1U);
        const F2cExpr *mask =
            f2c_intrinsic_argument(expression->children, expression->child_count, "mask", 2U);
        char *true_pointer;
        char *false_pointer;
        char *mask_code;
        if (true_source == NULL || false_source == NULL || mask == NULL || mask->rank != 0U) {
            *supported = 0;
            return NULL;
        }
        true_pointer = emit_pointer(unit, true_source, temporary, supported);
        false_pointer = *supported ? emit_pointer(unit, false_source, temporary, supported) : NULL;
        mask_code = *supported ? f2c_expression_emit(unit, mask, supported) : NULL;
        if (!*supported || true_pointer == NULL || false_pointer == NULL || mask_code == NULL) {
            free(true_pointer);
            free(false_pointer);
            free(mask_code);
            *supported = 0;
            return NULL;
        }
        f2c_buffer_printf(&result, "((bool)(%s) ? (%s) : (%s))", mask_code, true_pointer,
                          false_pointer);
        free(true_pointer);
        free(false_pointer);
        free(mask_code);
        return f2c_buffer_take(&result);
    }
    {
        char *code = f2c_expression_emit(unit, expression, supported);
        if (!*supported || code == NULL) {
            free(code);
            return NULL;
        }
        if (expression_is_owned(expression))
            f2c_buffer_printf(&result,
                              "f2c_materialize_move_%s(&f2c_derived_actual_%zu, "
                              "&f2c_derived_actual_live_%zu, %s)",
                              type_name, temporary, temporary, code);
        else
            f2c_buffer_printf(&result,
                              "f2c_materialize_copy_%s(&f2c_derived_actual_%zu, "
                              "&f2c_derived_actual_live_%zu, &(%s))",
                              type_name, temporary, temporary, code);
        free(code);
    }
    return f2c_buffer_take(&result);
}

char *f2c_expression_derived_actual_pointer(Unit *unit, const F2cExpr *expression, int *supported) {
    return emit_pointer(unit, expression,
                        expression != NULL ? expression->temporary_index : SIZE_MAX, supported);
}

void f2c_expression_append_derived_actual_releases(Buffer *output, const F2cExpr *expression,
                                                   size_t first) {
    size_t child;
    for (child = first; expression != NULL && child < expression->child_count; ++child) {
        const F2cExpr *actual = actual_value(expression->children[child]);
        if (actual == NULL || actual->type != TYPE_DERIVED || actual->derived_type == NULL ||
            actual->rank != 0U || actual->definable)
            continue;
        f2c_buffer_printf(output,
                          ", f2c_release_%s(&f2c_derived_actual_%zu, "
                          "&f2c_derived_actual_live_%zu)",
                          actual->derived_type->c_name, actual->temporary_index,
                          actual->temporary_index);
    }
}
