#include "codegen/expression/private.h"

#include "codegen/array/private.h"
#include "codegen/descriptor/private.h"

#include <stdlib.h>
#include <string.h>

static const F2cExpr *argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static const F2cExpr *call_argument(const F2cExpr *call, const char *keyword, size_t position) {
    size_t positional = 0U;
    size_t argument;
    for (argument = 0U; call != NULL && argument < call->child_count; ++argument) {
        const F2cExpr *actual = call->children[argument];
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (actual->text != NULL && strcmp(actual->text, keyword) == 0)
                return argument_value(actual);
        } else if (positional++ == position) {
            return actual;
        }
    }
    return NULL;
}

static int section_has_unit_stride(Unit *unit, const F2cExpr *section) {
    int64_t value = 0;
    return section != NULL && section->child_count == 3U &&
           (section->children[2]->kind == F2C_EXPR_INVALID ||
            f2c_evaluate_integer_constant(unit, section->children[2], &value)) &&
           (section->children[2]->kind == F2C_EXPR_INVALID || value == 1);
}

static int contiguous_section_view(Unit *unit, const F2cExpr *array, char **pointer, char **count,
                                   char **stride, int *supported) {
    const size_t selector_offset = f2c_descriptor_selector_offset(array);
    size_t highest_section = SIZE_MAX;
    size_t source_dimension;
    char *indices[F2C_MAX_RANK] = {0};
    char *reference = NULL;
    Buffer extent_list = {0};
    Buffer count_code = {0};
    Buffer address = {0};
    if (array == NULL || array->symbol == NULL || array->rank == 0U ||
        f2c_symbol_uses_descriptor(array->symbol) ||
        selector_offset + array->symbol->rank != array->child_count)
        return 0;
    for (source_dimension = 0U; source_dimension < array->symbol->rank; ++source_dimension) {
        const F2cExpr *selector = f2c_descriptor_selector(array, source_dimension);
        if (selector == NULL)
            return 0;
        if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
            if (!section_has_unit_stride(unit, selector))
                return 0;
            highest_section = source_dimension;
        } else if (selector->rank != 0U) {
            return 0;
        }
    }
    if (highest_section == SIZE_MAX)
        return 0;
    for (source_dimension = 0U; source_dimension < highest_section; ++source_dimension) {
        const F2cExpr *selector = f2c_descriptor_selector(array, source_dimension);
        if (selector->kind != F2C_EXPR_ARRAY_SECTION ||
            selector->children[0]->kind != F2C_EXPR_INVALID ||
            selector->children[1]->kind != F2C_EXPR_INVALID)
            return 0;
    }
    for (source_dimension = 0U; source_dimension < array->symbol->rank; ++source_dimension) {
        const F2cExpr *selector = f2c_descriptor_selector(array, source_dimension);
        if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
            indices[source_dimension] =
                selector->children[0]->kind == F2C_EXPR_INVALID
                    ? f2c_descriptor_dimension_lower(unit, array, source_dimension)
                    : f2c_expression_emit(unit, selector->children[0], supported);
        } else {
            indices[source_dimension] = f2c_expression_emit(unit, selector, supported);
        }
        if (!*supported || indices[source_dimension] == NULL)
            goto cleanup;
    }
    reference = f2c_descriptor_element_designator(unit, array, indices, array->symbol->rank);
    if (reference == NULL)
        goto cleanup;
    for (source_dimension = 0U; source_dimension < array->rank; ++source_dimension) {
        char *extent = f2c_array_expression_extent(unit, array, source_dimension);
        if (extent == NULL)
            goto cleanup;
        f2c_buffer_printf(&extent_list, "%s(size_t)(%s)", source_dimension == 0U ? "" : ", ",
                          extent);
        free(extent);
    }
    f2c_buffer_printf(&address, "(&%s)", reference);
    f2c_buffer_printf(&count_code, "f2c_inquiry_size(%zuU, (const size_t[]){%s})", array->rank,
                      extent_list.data != NULL ? extent_list.data : "");
    *pointer = f2c_buffer_take(&address);
    *count = f2c_buffer_take(&count_code);
    *stride = f2c_strdup("1");

cleanup:
    for (source_dimension = 0U; source_dimension < array->symbol->rank; ++source_dimension)
        free(indices[source_dimension]);
    free(reference);
    free(extent_list.data);
    free(address.data);
    free(count_code.data);
    return *pointer != NULL && *count != NULL && *stride != NULL;
}

int f2c_expression_array_view(Unit *unit, const F2cExpr *array, char **pointer, char **count,
                              char **stride, int *supported) {
    Buffer extent = {0};
    Buffer storage_stride = {0};
    const F2cExpr *selector;
    char *reference;
    char *lower;
    char *upper;
    char *step;
    size_t dimension;
    if (array == NULL || array->rank == 0U)
        return 0;
    if (array->kind == F2C_EXPR_NAME && array->symbol != NULL) {
        Buffer dynamic_stride = {0};
        *pointer = f2c_strdup(f2c_symbol_c_name(unit, array->symbol));
        *count = f2c_symbol_element_count(unit, array->symbol);
        if (array->symbol->rank == 1U &&
            (array->symbol->pointer ||
             (array->symbol->argument && f2c_symbol_uses_descriptor(array->symbol)))) {
            f2c_buffer_printf(&dynamic_stride, "%s_stride_1",
                              f2c_symbol_c_name(unit, array->symbol));
            *stride = f2c_buffer_take(&dynamic_stride);
        } else {
            *stride = f2c_strdup("1");
        }
        return *pointer != NULL && *count != NULL && *stride != NULL;
    }
    if (array->kind == F2C_EXPR_COMPONENT && array->child_count == 1U && array->symbol != NULL &&
        array->rank == 1U) {
        char *component_extent;
        Buffer count_code = {0};
        *pointer = f2c_descriptor_storage_designator(unit, array);
        component_extent = f2c_descriptor_dimension_extent(unit, array, 0U);
        if (component_extent != NULL)
            f2c_buffer_printf(&count_code, "(size_t)(%s)", component_extent);
        free(component_extent);
        *count = f2c_buffer_take(&count_code);
        *stride = f2c_descriptor_expression_stride(unit, array, 0U);
        return *pointer != NULL && *count != NULL && *stride != NULL;
    }
    if (array->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        char *constructor = f2c_expression_emit(unit, array, supported);
        Buffer grouped = {0};
        if (constructor != NULL)
            f2c_buffer_printf(&grouped, "(%s)", constructor);
        free(constructor);
        *pointer = f2c_buffer_take(&grouped);
        f2c_buffer_printf(&extent, "%zuU", array->child_count);
        *count = f2c_buffer_take(&extent);
        *stride = f2c_strdup("1");
        return *supported && *pointer != NULL && *count != NULL && *stride != NULL;
    }
    if (array->kind == F2C_EXPR_CALL && array->text != NULL &&
        strcmp(array->text, "reshape") == 0) {
        const F2cExpr *source = call_argument(array, "source", 0U);
        const F2cExpr *pad = call_argument(array, "pad", 2U);
        const F2cExpr *order = call_argument(array, "order", 3U);
        char *source_count = NULL;
        Buffer result_count = {0};
        size_t index;
        if (source == NULL || pad != NULL || order != NULL || array->rank == 0U ||
            !f2c_expression_array_view(unit, source, pointer, &source_count, stride, supported))
            return 0;
        for (index = 0U; index < array->rank; ++index) {
            char *dimension_extent = f2c_array_expression_extent(unit, array, index);
            if (dimension_extent == NULL) {
                free(*pointer);
                free(source_count);
                free(*stride);
                *pointer = NULL;
                *stride = NULL;
                return 0;
            }
            f2c_buffer_printf(&result_count, "%s(size_t)(%s)", index == 0U ? "" : " * ",
                              dimension_extent);
            free(dimension_extent);
        }
        f2c_buffer_printf(&extent, "((%s) <= (size_t)(%s) ? (%s) : (abort(), 0U))",
                          result_count.data, source_count, result_count.data);
        free(result_count.data);
        free(source_count);
        *count = f2c_buffer_take(&extent);
        return *pointer != NULL && *count != NULL && *stride != NULL;
    }
    if ((array->kind != F2C_EXPR_ARRAY_REFERENCE && array->kind != F2C_EXPR_COMPONENT) ||
        array->symbol == NULL)
        return 0;
    if (contiguous_section_view(unit, array, pointer, count, stride, supported))
        return 1;
    if (array->rank != 1U)
        return 0;
    selector = NULL;
    dimension = 0U;
    const size_t selector_offset = f2c_descriptor_selector_offset(array);
    for (size_t i = selector_offset; i < array->child_count; ++i) {
        if (array->children[i]->kind == F2C_EXPR_ARRAY_SECTION) {
            if (selector != NULL)
                return 0;
            selector = array->children[i];
            dimension = i - selector_offset;
        } else if (array->children[i]->rank != 0U) {
            return 0;
        }
    }
    if (selector == NULL || selector->child_count != 3U)
        return 0;
    lower = selector->children[0]->kind == F2C_EXPR_INVALID
                ? f2c_descriptor_dimension_lower(unit, array, dimension)
                : f2c_expression_emit(unit, selector->children[0], supported);
    upper = selector->children[1]->kind == F2C_EXPR_INVALID
                ? f2c_descriptor_dimension_upper(unit, array, dimension)
                : f2c_expression_emit(unit, selector->children[1], supported);
    step = selector->children[2]->kind == F2C_EXPR_INVALID
               ? f2c_strdup("1")
               : f2c_expression_emit(unit, selector->children[2], supported);
    {
        char *indices[F2C_MAX_RANK] = {0};
        size_t source_dimension;
        for (source_dimension = 0U; source_dimension < array->symbol->rank; ++source_dimension) {
            const F2cExpr *source_selector = f2c_descriptor_selector(array, source_dimension);
            if (source_selector == selector)
                indices[source_dimension] = f2c_strdup(lower);
            else
                indices[source_dimension] = f2c_expression_emit(unit, source_selector, supported);
        }
        reference = *supported ? f2c_descriptor_element_designator(unit, array, indices,
                                                                   array->symbol->rank)
                               : NULL;
        for (source_dimension = 0U; source_dimension < array->symbol->rank; ++source_dimension)
            free(indices[source_dimension]);
    }
    if (!*supported || lower == NULL || upper == NULL || step == NULL || reference == NULL) {
        free(lower);
        free(upper);
        free(step);
        free(reference);
        return 0;
    }
    {
        Buffer address = {0};
        f2c_buffer_printf(&address, "(&%s)", reference);
        *pointer = f2c_buffer_take(&address);
    }
    f2c_buffer_printf(&extent,
                      "((%s) > 0 ? ((%s) >= (%s) ? (size_t)(((%s) - (%s)) / (%s) + 1) "
                      ": 0U) : ((%s) < 0 ? ((%s) <= (%s) ? "
                      "(size_t)(((%s) - (%s)) / (-(%s)) + 1) : 0U) : (abort(), 0U)))",
                      step, upper, lower, upper, lower, step, step, upper, lower, lower, upper,
                      step);
    *count = f2c_buffer_take(&extent);
    for (size_t i = 0U; i < dimension; ++i) {
        char *dimension_extent = f2c_descriptor_dimension_extent(unit, array, i);
        if (dimension_extent == NULL) {
            free(reference);
            free(lower);
            free(upper);
            free(step);
            return 0;
        }
        f2c_buffer_printf(&storage_stride, "%s(ptrdiff_t)(%s)", i == 0U ? "" : " * ",
                          dimension_extent);
        free(dimension_extent);
    }
    if (dimension != 0U)
        f2c_buffer_append(&storage_stride, " * ");
    f2c_buffer_printf(&storage_stride, "(ptrdiff_t)(%s)", step);
    *stride = f2c_buffer_take(&storage_stride);
    free(reference);
    free(lower);
    free(upper);
    free(step);
    return *pointer != NULL && *count != NULL && *stride != NULL;
}
