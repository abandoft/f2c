#include "codegen/value/private.h"

#include <stdlib.h>

static void indent(Buffer *output, int depth) {
    int index;
    for (index = 0; index < depth; ++index)
        f2c_buffer_append(output, "    ");
}

static int expression_is_owned(const F2cExpr *expression) {
    return expression != NULL &&
           (expression->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR ||
            (expression->kind == F2C_EXPR_CALL && expression->intrinsic != F2C_INTRINSIC_MERGE) ||
            expression->resolved_procedure != NULL);
}

static int emit_clone(Buffer *output, Unit *unit, const F2cExpr *source, const char *destination,
                      const char *scope, size_t identifier, int depth, size_t merge_depth) {
    const size_t output_start = output != NULL ? output->length : 0U;
    const char *type_name;
    if (output == NULL || unit == NULL || source == NULL || destination == NULL || scope == NULL ||
        source->type != TYPE_DERIVED || source->derived_type == NULL || source->rank != 0U)
        return 0;
    type_name = source->derived_type->c_name;
    if (source->kind == F2C_EXPR_CALL && source->intrinsic == F2C_INTRINSIC_MERGE) {
        const F2cExpr *true_source =
            f2c_intrinsic_argument(source->children, source->child_count, "tsource", 0U);
        const F2cExpr *false_source =
            f2c_intrinsic_argument(source->children, source->child_count, "fsource", 1U);
        const F2cExpr *mask =
            f2c_intrinsic_argument(source->children, source->child_count, "mask", 2U);
        char *mask_code = NULL;
        int supported = 0;
        if (true_source != NULL && false_source != NULL && mask != NULL && mask->rank == 0U)
            mask_code = f2c_emit_expression_ast(unit, mask, &supported);
        if (!supported || mask_code == NULL)
            goto failed;
        indent(output, depth);
        f2c_buffer_append(output, "{\n");
        indent(output, depth + 1);
        f2c_buffer_printf(output, "const bool f2c_%s_merge_mask_%zu_%zu = (bool)(%s);\n", scope,
                          identifier, merge_depth, mask_code);
        indent(output, depth + 1);
        f2c_buffer_printf(output, "if (f2c_%s_merge_mask_%zu_%zu) {\n", scope, identifier,
                          merge_depth);
        if (!emit_clone(output, unit, true_source, destination, scope, identifier, depth + 2,
                        merge_depth + 1U)) {
            free(mask_code);
            goto failed;
        }
        indent(output, depth + 1);
        f2c_buffer_append(output, "} else {\n");
        if (!emit_clone(output, unit, false_source, destination, scope, identifier, depth + 2,
                        merge_depth + 1U)) {
            free(mask_code);
            goto failed;
        }
        indent(output, depth + 1);
        f2c_buffer_append(output, "}\n");
        indent(output, depth);
        f2c_buffer_append(output, "}\n");
        free(mask_code);
        return 1;
    }
    {
        char *source_code;
        int supported = 0;
        source_code = f2c_emit_expression_ast(unit, source, &supported);
        if (!supported || source_code == NULL) {
            free(source_code);
            goto failed;
        }
        if (expression_is_owned(source)) {
            indent(output, depth);
            f2c_buffer_append(output, "{\n");
            indent(output, depth + 1);
            f2c_buffer_printf(output, "%s f2c_%s_source_%zu_%zu = %s;\n", type_name, scope,
                              identifier, merge_depth, source_code);
            if (source->kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR) {
                indent(output, depth + 1);
                f2c_buffer_printf(output, "f2c_initialize_%s(&f2c_%s_source_%zu_%zu);\n", type_name,
                                  scope, identifier, merge_depth);
            }
            indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_clone_%s(&(%s), &f2c_%s_source_%zu_%zu);\n", type_name,
                              destination, scope, identifier, merge_depth);
            indent(output, depth + 1);
            f2c_buffer_printf(output, "f2c_destroy_%s(&f2c_%s_source_%zu_%zu);\n", type_name, scope,
                              identifier, merge_depth);
            indent(output, depth);
            f2c_buffer_append(output, "}\n");
        } else {
            indent(output, depth);
            f2c_buffer_printf(output, "f2c_clone_%s(&(%s), &(%s));\n", type_name, destination,
                              source_code);
        }
        free(source_code);
    }
    return 1;

failed:
    if (output != NULL && output->data != NULL && output_start <= output->length) {
        output->length = output_start;
        output->data[output_start] = '\0';
    }
    return 0;
}

int f2c_emit_derived_clone_expression(Buffer *output, Unit *unit, const F2cExpr *source,
                                      const char *destination, const char *scope, size_t identifier,
                                      int depth) {
    return emit_clone(output, unit, source, destination, scope, identifier, depth, 0U);
}
