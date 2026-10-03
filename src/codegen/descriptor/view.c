#include "codegen/descriptor/private.h"

#include "codegen/array/private.h"
#include "codegen/lowering/private.h"
#include "codegen/storage/private.h"

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
    if (symbol->pointer || (symbol->argument && f2c_symbol_uses_descriptor(symbol))) {
        return f2c_storage_symbol_property(unit, symbol, F2C_OBJECT_STRIDE, dimension);
    }
    return contiguous_stride(unit, symbol, dimension);
}

static char *section_first(Unit *unit, const F2cExpr *expression, const F2cExpr *section,
                           size_t dimension) {
    if (section->child_count != 3U)
        return NULL;
    if (section->children[0]->kind != F2C_EXPR_INVALID)
        return f2c_emit_typed_expression(unit, section->children[0]);
    return f2c_descriptor_dimension_lower(unit, expression, dimension);
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
    const F2cStorageReference reference = f2c_ir_storage_reference(expression);
    view->data = f2c_storage_read_property(unit, &reference, F2C_OBJECT_DATA, 0U);
    view->rank = symbol->rank;
    for (dimension = 0U; dimension < view->rank; ++dimension) {
        view->lower[dimension] = f2c_descriptor_dimension_lower(unit, expression, dimension);
        view->extent[dimension] = f2c_descriptor_dimension_extent(unit, expression, dimension);
        view->stride[dimension] = f2c_descriptor_expression_stride(unit, expression, dimension);
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
    const size_t offset = f2c_descriptor_selector_offset(expression);
    if (expression->child_count != symbol->rank + offset || expression->rank == 0U)
        return 0;
    for (source_dimension = 0U; source_dimension < symbol->rank; ++source_dimension) {
        const F2cExpr *selector = f2c_descriptor_selector(expression, source_dimension);
        if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
            char *step = section_step(unit, selector);
            char *base_stride =
                f2c_descriptor_expression_stride(unit, expression, source_dimension);
            char *last = selector->children[1]->kind != F2C_EXPR_INVALID
                             ? f2c_emit_typed_expression(unit, selector->children[1])
                             : f2c_descriptor_dimension_upper(unit, expression, source_dimension);
            Buffer stride = {0};
            Buffer extent = {0};
            indices[source_dimension] = section_first(unit, expression, selector, source_dimension);
            view->lower[result_dimension] = f2c_strdup("1");
            if (indices[source_dimension] != NULL && last != NULL && step != NULL)
                f2c_buffer_printf(&extent,
                                  "f2c_section_extent((int64_t)(%s), (int64_t)(%s), "
                                  "(int64_t)(%s))",
                                  indices[source_dimension], last, step);
            view->extent[result_dimension] = f2c_buffer_take(&extent);
            if (step != NULL && base_stride != NULL)
                f2c_buffer_printf(&stride,
                                  "f2c_descriptor_stride_step((ptrdiff_t)(%s), (int64_t)(%s))",
                                  base_stride, step);
            view->stride[result_dimension] = f2c_buffer_take(&stride);
            free(step);
            free(base_stride);
            free(last);
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
    reference = f2c_descriptor_element_designator(unit, expression, indices, symbol->rank);
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

static int lowered_array_view(Unit *unit, const F2cExpr *expression, F2cDescriptorView *view) {
    const char *data = f2c_lowering_code(unit, expression);
    size_t dimension;
    if (data == NULL)
        return 0;
    view->data = f2c_strdup(data);
    view->rank = expression->rank;
    if (expression->type == TYPE_CHARACTER)
        view->character_length = f2c_character_length_expression(unit, expression);
    for (dimension = 0U; dimension < view->rank; ++dimension) {
        Buffer stride = {0};
        size_t prior;
        view->lower[dimension] = f2c_strdup("1");
        view->extent[dimension] = f2c_array_expression_extent(unit, expression, dimension);
        f2c_buffer_append(&stride, "((ptrdiff_t)1");
        for (prior = 0U; prior < dimension; ++prior)
            f2c_buffer_printf(&stride, " * (ptrdiff_t)(%s)", view->extent[prior]);
        f2c_buffer_append(&stride, ")");
        view->stride[dimension] = f2c_buffer_take(&stride);
        if (view->lower[dimension] == NULL || view->extent[dimension] == NULL ||
            view->stride[dimension] == NULL)
            return 0;
    }
    return view->data != NULL &&
           (expression->type != TYPE_CHARACTER || view->character_length != NULL);
}

static int pointer_result_view(const F2cExpr *expression, const char *descriptor,
                               F2cDescriptorView *view) {
    Buffer data = {0};
    f2c_buffer_printf(&data, "(%s *)%s.data", f2c_expression_c_type(expression), descriptor);
    view->data = f2c_buffer_take(&data);
    view->rank = expression->rank;
    if (expression->type == TYPE_CHARACTER) {
        Buffer length = {0};
        f2c_buffer_printf(&length, "%s.character_length", descriptor);
        view->character_length = f2c_buffer_take(&length);
    }
    for (size_t dimension = 0U; dimension < view->rank; ++dimension) {
        Buffer lower = {0};
        Buffer extent = {0};
        Buffer stride = {0};
        f2c_buffer_printf(&lower, "%s.lower[%zu]", descriptor, dimension);
        f2c_buffer_printf(&extent, "%s.extent[%zu]", descriptor, dimension);
        f2c_buffer_printf(&stride, "%s.stride[%zu]", descriptor, dimension);
        view->lower[dimension] = f2c_buffer_take(&lower);
        view->extent[dimension] = f2c_buffer_take(&extent);
        view->stride[dimension] = f2c_buffer_take(&stride);
        if (view->lower[dimension] == NULL || view->extent[dimension] == NULL ||
            view->stride[dimension] == NULL)
            return 0;
    }
    return view->data != NULL &&
           (expression->type != TYPE_CHARACTER || view->character_length != NULL);
}

int f2c_descriptor_view(Unit *unit, const F2cExpr *expression, F2cDescriptorView *view) {
    int result = 0;
    if (unit == NULL || expression == NULL || view == NULL)
        return 0;
    memset(view, 0, sizeof(*view));
    view->storage_qualifiers = f2c_lowering_storage_qualifiers(unit, expression);
    view->readonly_storage = f2c_lowering_is_array_temporary(unit, expression)
                                 ? f2c_lowering_readonly_storage(unit, expression)
                                 : f2c_descriptor_readonly_storage(expression);
    const char *result_descriptor = f2c_lowering_result_descriptor(unit, expression);
    if (f2c_expression_has_pointer_result(expression) &&
        expression->result_use == F2C_FUNCTION_RESULT_REFERENCE && result_descriptor != NULL)
        result = pointer_result_view(expression, result_descriptor, view);
    else if (f2c_lowering_is_array_temporary(unit, expression))
        result = lowered_array_view(unit, expression, view);
    else if (expression->symbol == NULL)
        return 0;
    else if (expression->kind == F2C_EXPR_NAME ||
             (expression->kind == F2C_EXPR_COMPONENT && expression->child_count == 1U))
        result = whole_array_view(unit, expression, view);
    else if (expression->kind == F2C_EXPR_ARRAY_REFERENCE || expression->kind == F2C_EXPR_COMPONENT)
        result = section_view(unit, expression, view);
    if (!result)
        f2c_descriptor_view_free(view);
    return result;
}
