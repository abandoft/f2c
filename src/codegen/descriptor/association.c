#include "codegen/descriptor/private.h"

#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int level;
    for (level = 0; level < depth; ++level)
        f2c_buffer_append(output, "    ");
}

static char *temporary_name(const char *field, size_t dimension) {
    Buffer result = {0};
    f2c_buffer_printf(&result, "f2c_pointer_%s_%zu", field, dimension + 1U);
    return f2c_buffer_take(&result);
}

static char *emit_bound(Unit *unit, const F2cExpr *expression) {
    return expression != NULL && expression->kind != F2C_EXPR_INVALID
               ? f2c_emit_typed_expression(unit, expression)
               : NULL;
}

static int emit_section(Buffer *prelude, Unit *unit, const Symbol *symbol, const F2cExpr *section,
                        size_t source_dimension, size_t result_dimension, int depth, char **index,
                        F2cDescriptorView *view) {
    char *declared_lower = NULL;
    char *declared_upper = NULL;
    char *explicit_first = NULL;
    char *explicit_last = NULL;
    char *step = NULL;
    char *base_stride = NULL;
    char *first_name = NULL;
    char *last_name = NULL;
    char *step_name = NULL;
    char *extent_name = NULL;
    char *stride_name = NULL;
    int success = 0;
    if (section == NULL || section->child_count != 3U)
        return 0;
    declared_lower = f2c_symbol_dimension_lower(unit, symbol, source_dimension);
    declared_upper = f2c_symbol_dimension_upper(unit, symbol, source_dimension);
    explicit_first = emit_bound(unit, section->children[0]);
    explicit_last = emit_bound(unit, section->children[1]);
    step = emit_bound(unit, section->children[2]);
    if (step == NULL)
        step = f2c_strdup("1");
    base_stride = f2c_descriptor_source_stride(unit, symbol, source_dimension);
    first_name = temporary_name("first", source_dimension);
    last_name = temporary_name("last", source_dimension);
    step_name = temporary_name("step", source_dimension);
    extent_name = temporary_name("extent", result_dimension);
    stride_name = temporary_name("stride", result_dimension);
    if (declared_lower == NULL || declared_upper == NULL || step == NULL || base_stride == NULL ||
        first_name == NULL || last_name == NULL || step_name == NULL || extent_name == NULL ||
        stride_name == NULL)
        goto cleanup;

    indent(prelude, depth);
    f2c_buffer_printf(prelude, "const int64_t %s = (int64_t)(%s);\n", step_name, step);
    if (explicit_first != NULL) {
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "const int64_t %s = (int64_t)(%s);\n", first_name,
                          explicit_first);
    } else {
        free(first_name);
        first_name = temporary_name("first_default", source_dimension);
        if (first_name == NULL)
            goto cleanup;
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "const int64_t %s = (int64_t)(%s);\n", first_name,
                          declared_lower);
    }
    if (explicit_last != NULL) {
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "const int64_t %s = (int64_t)(%s);\n", last_name, explicit_last);
    } else {
        free(last_name);
        last_name = temporary_name("last_default", source_dimension);
        if (last_name == NULL)
            goto cleanup;
        indent(prelude, depth);
        f2c_buffer_printf(prelude, "const int64_t %s = (int64_t)(%s);\n", last_name,
                          declared_upper);
    }
    indent(prelude, depth);
    f2c_buffer_printf(prelude, "const size_t %s = f2c_section_extent(%s, %s, %s);\n", extent_name,
                      first_name, last_name, step_name);
    indent(prelude, depth);
    f2c_buffer_printf(prelude,
                      "const ptrdiff_t %s = f2c_descriptor_stride_step((ptrdiff_t)(%s), %s);\n",
                      stride_name, base_stride, step_name);

    *index = f2c_strdup(first_name);
    view->lower[result_dimension] = f2c_strdup("1");
    view->extent[result_dimension] = f2c_strdup(extent_name);
    view->stride[result_dimension] = f2c_strdup(stride_name);
    success = *index != NULL && view->lower[result_dimension] != NULL &&
              view->extent[result_dimension] != NULL && view->stride[result_dimension] != NULL;

cleanup:
    free(declared_lower);
    free(declared_upper);
    free(explicit_first);
    free(explicit_last);
    free(step);
    free(base_stride);
    free(first_name);
    free(last_name);
    free(step_name);
    free(extent_name);
    free(stride_name);
    return success;
}

int f2c_descriptor_association_view(Buffer *prelude, Unit *unit, const F2cExpr *expression,
                                    int depth, F2cDescriptorView *view) {
    const Symbol *symbol;
    char *indices[F2C_MAX_RANK] = {0};
    char *reference = NULL;
    size_t source_dimension;
    size_t result_dimension = 0U;
    int success = 0;
    if (prelude == NULL || unit == NULL || expression == NULL || view == NULL ||
        expression->symbol == NULL || expression->rank == 0U)
        return 0;
    if (expression->kind == F2C_EXPR_NAME)
        return f2c_descriptor_view(unit, expression, view);
    symbol = expression->symbol;
    memset(view, 0, sizeof(*view));
    if (expression->kind != F2C_EXPR_ARRAY_REFERENCE || expression->child_count != symbol->rank)
        return 0;

    for (source_dimension = 0U; source_dimension < symbol->rank; ++source_dimension) {
        const F2cExpr *selector = expression->children[source_dimension];
        if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
            if (!emit_section(prelude, unit, symbol, selector, source_dimension, result_dimension,
                              depth, &indices[source_dimension], view))
                goto cleanup;
            ++result_dimension;
        } else if (selector->rank == 0U) {
            char *value = f2c_emit_typed_expression(unit, selector);
            char *name = temporary_name("subscript", source_dimension);
            if (value == NULL || name == NULL) {
                free(value);
                free(name);
                goto cleanup;
            }
            indent(prelude, depth);
            f2c_buffer_printf(prelude, "const int64_t %s = (int64_t)(%s);\n", name, value);
            indices[source_dimension] = name;
            free(value);
        } else {
            goto cleanup;
        }
    }
    if (result_dimension != expression->rank)
        goto cleanup;
    reference = f2c_emit_array_reference(unit, expression->symbol, indices, symbol->rank);
    if (reference != NULL) {
        Buffer data = {0};
        f2c_buffer_printf(&data, "(&%s)", reference);
        view->data = f2c_buffer_take(&data);
    }
    view->rank = result_dimension;
    success = view->data != NULL;

cleanup:
    for (source_dimension = 0U; source_dimension < symbol->rank; ++source_dimension)
        free(indices[source_dimension]);
    free(reference);
    if (!success)
        f2c_descriptor_view_free(view);
    return success;
}
