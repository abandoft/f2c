#include "codegen/descriptor/private.h"

#include "codegen/array/private.h"

#include <stdlib.h>
#include <string.h>

void f2c_descriptor_view_free(F2cDescriptorView *view) {
    size_t dimension;
    if (view == NULL)
        return;
    free(view->data);
    free(view->character_length);
    for (dimension = 0U; dimension < F2C_MAX_RANK; ++dimension) {
        free(view->lower[dimension]);
        free(view->extent[dimension]);
        free(view->stride[dimension]);
    }
    memset(view, 0, sizeof(*view));
}

static char *contiguous_stride(Unit *unit, const Symbol *symbol, size_t dimension) {
    char *stride = f2c_strdup("1");
    size_t prior;
    for (prior = 0U; stride != NULL && prior < dimension; ++prior) {
        char *extent = f2c_symbol_dimension_extent(unit, symbol, prior);
        Buffer next = {0};
        if (extent == NULL) {
            free(stride);
            return NULL;
        }
        f2c_buffer_printf(&next, "f2c_descriptor_stride_extent((ptrdiff_t)(%s), (size_t)(%s))",
                          stride, extent);
        free(stride);
        free(extent);
        stride = f2c_buffer_take(&next);
    }
    return stride;
}

char *f2c_descriptor_source_stride(Unit *unit, const Symbol *symbol, size_t dimension) {
    Buffer result = {0};
    if (symbol->pointer || (symbol->argument && f2c_symbol_uses_descriptor(symbol))) {
        f2c_buffer_printf(&result, "%s_stride_%zu", f2c_symbol_c_name(unit, symbol),
                          dimension + 1U);
        return f2c_buffer_take(&result);
    }
    return contiguous_stride(unit, symbol, dimension);
}

static char *section_first(Unit *unit, const F2cExpr *section, const Symbol *symbol,
                           size_t dimension) {
    if (section->child_count != 3U)
        return NULL;
    if (section->children[0]->kind != F2C_EXPR_INVALID)
        return f2c_emit_typed_expression(unit, section->children[0]);
    return f2c_symbol_dimension_lower(unit, symbol, dimension);
}

static char *section_step(Unit *unit, const F2cExpr *section) {
    if (section->child_count != 3U)
        return NULL;
    return section->children[2]->kind == F2C_EXPR_INVALID
               ? f2c_strdup("1")
               : f2c_emit_typed_expression(unit, section->children[2]);
}

static int whole_array_view(Unit *unit, const F2cExpr *expression, F2cDescriptorView *view) {
    size_t dimension;
    const Symbol *symbol = expression->symbol;
    view->data = f2c_strdup(f2c_symbol_c_name(unit, symbol));
    view->rank = symbol->rank;
    for (dimension = 0U; dimension < view->rank; ++dimension) {
        view->lower[dimension] = f2c_symbol_dimension_lower(unit, symbol, dimension);
        view->extent[dimension] = f2c_symbol_dimension_extent(unit, symbol, dimension);
        view->stride[dimension] = f2c_descriptor_source_stride(unit, symbol, dimension);
        if (view->lower[dimension] == NULL || view->extent[dimension] == NULL ||
            view->stride[dimension] == NULL)
            return 0;
    }
    return view->data != NULL;
}

static int section_view(Unit *unit, const F2cExpr *expression, F2cDescriptorView *view) {
    const Symbol *symbol = expression->symbol;
    char *indices[F2C_MAX_RANK] = {0};
    char *reference = NULL;
    size_t source_dimension;
    size_t result_dimension = 0U;
    if (expression->child_count != symbol->rank || expression->rank == 0U)
        return 0;
    for (source_dimension = 0U; source_dimension < symbol->rank; ++source_dimension) {
        const F2cExpr *selector = expression->children[source_dimension];
        if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
            char *step = section_step(unit, selector);
            char *base_stride = f2c_descriptor_source_stride(unit, symbol, source_dimension);
            Buffer stride = {0};
            indices[source_dimension] = section_first(unit, selector, symbol, source_dimension);
            view->lower[result_dimension] = f2c_strdup("1");
            view->extent[result_dimension] =
                f2c_array_expression_extent(unit, expression, result_dimension);
            if (step != NULL && base_stride != NULL)
                f2c_buffer_printf(&stride,
                                  "f2c_descriptor_stride_step((ptrdiff_t)(%s), (int64_t)(%s))",
                                  base_stride, step);
            view->stride[result_dimension] = f2c_buffer_take(&stride);
            free(step);
            free(base_stride);
            if (indices[source_dimension] == NULL || view->lower[result_dimension] == NULL ||
                view->extent[result_dimension] == NULL || view->stride[result_dimension] == NULL)
                goto failed;
            ++result_dimension;
        } else if (selector->rank == 0U) {
            indices[source_dimension] = f2c_emit_typed_expression(unit, selector);
            if (indices[source_dimension] == NULL)
                goto failed;
        } else {
            goto failed;
        }
    }
    if (result_dimension != expression->rank)
        goto failed;
    reference = f2c_emit_array_reference(unit, expression->symbol, indices, symbol->rank);
    if (reference != NULL) {
        Buffer data = {0};
        f2c_buffer_printf(&data, "(&%s)", reference);
        view->data = f2c_buffer_take(&data);
    }
    view->rank = result_dimension;
    for (source_dimension = 0U; source_dimension < symbol->rank; ++source_dimension)
        free(indices[source_dimension]);
    free(reference);
    return view->data != NULL;

failed:
    for (source_dimension = 0U; source_dimension < symbol->rank; ++source_dimension)
        free(indices[source_dimension]);
    free(reference);
    return 0;
}

int f2c_descriptor_view(Unit *unit, const F2cExpr *expression, F2cDescriptorView *view) {
    int result = 0;
    if (unit == NULL || expression == NULL || view == NULL || expression->symbol == NULL)
        return 0;
    memset(view, 0, sizeof(*view));
    if (expression->kind == F2C_EXPR_NAME)
        result = whole_array_view(unit, expression, view);
    else if (expression->kind == F2C_EXPR_ARRAY_REFERENCE)
        result = section_view(unit, expression, view);
    if (!result)
        f2c_descriptor_view_free(view);
    return result;
}
