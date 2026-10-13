#include "codegen/descriptor/private.h"

#include "codegen/array/private.h"
#include "codegen/expression/private.h"

#include <stdlib.h>
#include <string.h>

static int source_view(Buffer *prelude, Unit *unit, const F2cExpr *expression, int depth,
                       F2cDescriptorView *view) {
    return prelude != NULL ? f2c_descriptor_association_view(prelude, unit, expression, depth, view)
                           : f2c_descriptor_view(unit, expression, view);
}

static int adopt_source(F2cDescriptorView *view, F2cDescriptorView *source) {
    const unsigned int qualifiers = view->storage_qualifiers;
    const int readonly_storage = view->readonly_storage;
    f2c_descriptor_view_free(view);
    *view = *source;
    memset(source, 0, sizeof(*source));
    view->storage_qualifiers |= qualifiers;
    view->readonly_storage |= readonly_storage;
    /* Projected component/substring arrays are not whole-array targets. */
    for (size_t dimension = 0U; dimension < view->rank; ++dimension) {
        free(view->lower[dimension]);
        view->lower[dimension] = f2c_strdup("1");
        if (view->lower[dimension] == NULL)
            return 0;
    }
    return 1;
}

static char *array_count(const F2cDescriptorView *view) {
    Buffer count = {0};
    f2c_buffer_printf(&count, "f2c_inquiry_size(%zuU, (const size_t[]){", view->rank);
    for (size_t dimension = 0U; dimension < view->rank; ++dimension)
        f2c_buffer_printf(&count, "%s(size_t)(%s)", dimension == 0U ? "" : ", ",
                          view->extent[dimension]);
    f2c_buffer_append(&count, "})");
    return f2c_buffer_take(&count);
}

static char *component_address(Unit *unit, const F2cExpr *expression,
                               const F2cDescriptorView *source) {
    const Symbol *component = expression->symbol;
    Buffer base = {0};
    Buffer field = {0};
    Buffer address = {0};
    char *count = array_count(source);
    f2c_buffer_printf(&base, "(*(%s))", source->data);
    f2c_expression_append_component(&field, base.data, expression->children[0]->derived_type,
                                    component);
    free(base.data);
    if (count == NULL || field.data == NULL)
        goto failed;
    f2c_buffer_printf(&address, "(f2c_descriptor_data_absent(%s) || (%s) == 0U ? (%s%s *)(%s) : ",
                      source->data, count, source->readonly_storage ? "const " : "",
                      f2c_expression_c_type(expression), source->data);
    if (component->rank == 0U) {
        f2c_buffer_printf(&address, component->type == TYPE_CHARACTER ? "(%s))" : "(&(%s)))",
                          field.data);
    } else {
        if (expression->child_count != component->rank + 1U)
            goto failed;
        Buffer offset = {0};
        for (size_t dimension = 0U; dimension < component->rank; ++dimension) {
            const F2cExpr *selector = expression->children[dimension + 1U];
            char *index = selector->rank == 0U ? f2c_emit_typed_expression(unit, selector) : NULL;
            char *lower = f2c_symbol_dimension_lower(unit, component, dimension);
            char *extent = f2c_symbol_dimension_extent(unit, component, dimension);
            if (index == NULL || lower == NULL || extent == NULL) {
                free(index);
                free(lower);
                free(extent);
                free(offset.data);
                goto failed;
            }
            f2c_buffer_printf(&offset,
                              "%sf2c_array_offset((int64_t)(%s), (int64_t)(%s), "
                              "(size_t)(%s))",
                              dimension == 0U ? "" : " + ", index, lower, extent);
            for (size_t prior = 0U; prior < dimension; ++prior) {
                char *prior_extent = f2c_symbol_dimension_extent(unit, component, prior);
                if (prior_extent == NULL) {
                    free(index);
                    free(lower);
                    free(extent);
                    free(offset.data);
                    goto failed;
                }
                f2c_buffer_printf(&offset, " * (size_t)(%s)", prior_extent);
                free(prior_extent);
            }
            free(index);
            free(lower);
            free(extent);
        }
        if (component->type == TYPE_CHARACTER) {
            char *length = f2c_symbol_character_length(unit, component);
            if (length == NULL) {
                free(offset.data);
                goto failed;
            }
            f2c_buffer_printf(&address, "(&(%s)[(%s) * (size_t)(%s)]))", field.data, offset.data,
                              length);
            free(length);
        } else {
            f2c_buffer_printf(&address, "(&(%s)[%s]))", field.data, offset.data);
        }
        free(offset.data);
    }
    free(count);
    free(field.data);
    return f2c_buffer_take(&address);
failed:
    free(count);
    free(field.data);
    free(address.data);
    return NULL;
}

static int project_component(Buffer *prelude, Unit *unit, const F2cExpr *expression, int depth,
                             F2cDescriptorView *view) {
    F2cDescriptorView source = {0};
    const F2cExpr *parent = expression->children[0];
    char *data = NULL;
    char *record = NULL;
    if (expression->symbol == NULL || expression->symbol->pointer ||
        expression->symbol->allocatable || parent->rank == 0U || parent->derived_type == NULL ||
        expression->rank != parent->rank || !source_view(prelude, unit, parent, depth, &source))
        goto failed;
    data = component_address(unit, expression, &source);
    record = f2c_descriptor_expression_record_size(unit, expression);
    if (data == NULL || record == NULL)
        goto failed;
    if (!adopt_source(view, &source))
        goto failed;
    free(view->data);
    free(view->record_size);
    free(view->character_length);
    view->data = data;
    view->record_size = record;
    view->character_length = expression->type == TYPE_CHARACTER ? f2c_strdup(record) : NULL;
    return expression->type != TYPE_CHARACTER || view->character_length != NULL;
failed:
    free(data);
    free(record);
    f2c_descriptor_view_free(&source);
    return 0;
}

static int project_substring(Buffer *prelude, Unit *unit, const F2cExpr *expression, int depth,
                             F2cDescriptorView *view) {
    F2cDescriptorView source = {0};
    const F2cExpr *parent = f2c_substring_parent(expression);
    const F2cExpr *first = f2c_substring_lower(expression);
    const F2cExpr *last = f2c_substring_upper(expression);
    Buffer address = {0};
    char *length = NULL;
    char *lower = NULL;
    char *upper = NULL;
    char *result_length = NULL;
    if (parent == NULL || parent->rank == 0U || expression->rank != parent->rank ||
        !source_view(prelude, unit, parent, depth, &source))
        goto failed;
    length = f2c_character_length_expression(unit, parent);
    lower = first != NULL ? f2c_emit_typed_expression(unit, first) : f2c_strdup("1");
    upper = last != NULL ? f2c_emit_typed_expression(unit, last) : f2c_strdup(length);
    result_length = f2c_character_length_expression(unit, expression);
    if (length == NULL || lower == NULL || upper == NULL || result_length == NULL)
        goto failed;
    f2c_buffer_printf(&address,
                      "(f2c_descriptor_data_absent(%s) ? (char *)NULL : (%s) + "
                      "f2c_substring_offset((size_t)(%s), (int64_t)(%s), (int64_t)(%s)))",
                      source.data, source.data, length, lower, upper);
    if (!adopt_source(view, &source))
        goto failed;
    free(view->data);
    free(view->character_length);
    free(view->record_size);
    view->data = f2c_buffer_take(&address);
    view->character_length = result_length;
    result_length = NULL;
    view->record_size = f2c_strdup(view->character_length);
    free(length);
    free(lower);
    free(upper);
    return view->data != NULL && view->record_size != NULL;
failed:
    free(length);
    free(lower);
    free(upper);
    free(result_length);
    free(address.data);
    f2c_descriptor_view_free(&source);
    return 0;
}

int f2c_descriptor_project_view(Buffer *prelude, Unit *unit, const F2cExpr *expression, int depth,
                                F2cDescriptorView *view) {
    size_t temporary = 0U;
    if (prelude != NULL &&
        !f2c_array_hoist_scalar_subexpressions(unit, expression, expression->span.begin.line,
                                               "projection", &temporary, prelude, depth, 1))
        return 0;
    if (expression->kind == F2C_EXPR_SUBSTRING)
        return project_substring(prelude, unit, expression, depth, view);
    if (expression->kind == F2C_EXPR_COMPONENT && expression->child_count != 0U)
        return project_component(prelude, unit, expression, depth, view);
    return 0;
}
