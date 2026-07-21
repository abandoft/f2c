#include "codegen/expression/private.h"

#include "codegen/descriptor/private.h"

#include <stdlib.h>

char *f2c_expression_emit(Unit *unit, const F2cExpr *expression, int *supported);

char *f2c_expression_associated_scalar_target(Unit *unit, const F2cExpr *target, int *supported) {
    const Symbol *symbol = target != NULL ? target->symbol : NULL;
    Buffer result = {0};
    char *code;
    if (symbol == NULL)
        return NULL;
    if (target->kind == F2C_EXPR_ARRAY_REFERENCE && target->rank == 0U) {
        code = f2c_expression_emit(unit, target, supported);
        if (!*supported || code == NULL) {
            free(code);
            return NULL;
        }
        f2c_buffer_printf(&result, "&(%s)", code);
        free(code);
        return f2c_buffer_take(&result);
    }
    if (target->kind == F2C_EXPR_SUBSTRING) {
        code = f2c_expression_emit(unit, target, supported);
        if (!*supported || code == NULL) {
            free(code);
            return NULL;
        }
        if (target->child_count == 1U && target->children[0] != NULL &&
            target->children[0]->kind == F2C_EXPR_ARRAY_SECTION)
            f2c_buffer_append(&result, code);
        else
            f2c_buffer_printf(&result, "&(%s)", code);
        free(code);
        return f2c_buffer_take(&result);
    }
    if (target->kind == F2C_EXPR_COMPONENT) {
        if (target->rank == 0U && target->symbol->rank != 0U && target->child_count > 1U) {
            code = f2c_expression_emit(unit, target, supported);
            if (!*supported || code == NULL) {
                free(code);
                return NULL;
            }
            f2c_buffer_printf(&result, "&(%s)", code);
            free(code);
            return f2c_buffer_take(&result);
        }
        code = f2c_descriptor_storage_designator(unit, target);
        if (code == NULL)
            return NULL;
        f2c_buffer_printf(
            &result, "%s%s",
            symbol->pointer || symbol->allocatable || symbol->type == TYPE_CHARACTER ? "" : "&",
            code);
        free(code);
        return f2c_buffer_take(&result);
    }
    if (target->kind != F2C_EXPR_NAME)
        return NULL;
    f2c_buffer_printf(&result, "%s%s",
                      symbol->pointer || symbol->allocatable || symbol->argument ||
                              symbol->type == TYPE_CHARACTER
                          ? ""
                          : "&",
                      f2c_symbol_c_name(unit, symbol));
    return f2c_buffer_take(&result);
}

static void append_size_array(Buffer *output, char *const *values, size_t count) {
    size_t dimension;
    f2c_buffer_append(output, "(const size_t[]){");
    for (dimension = 0U; dimension < count; ++dimension)
        f2c_buffer_printf(output, "%s(size_t)(%s)", dimension == 0U ? "" : ", ", values[dimension]);
    f2c_buffer_append(output, "}");
}

static void append_int64_array(Buffer *output, char *const *values, size_t count) {
    size_t dimension;
    f2c_buffer_append(output, "(const int64_t[]){");
    for (dimension = 0U; dimension < count; ++dimension)
        f2c_buffer_printf(output, "%s(int64_t)(%s)", dimension == 0U ? "" : ", ",
                          values[dimension]);
    f2c_buffer_append(output, "}");
}

static void append_stride_array(Buffer *output, char *const *values, size_t count) {
    size_t dimension;
    f2c_buffer_append(output, "(const ptrdiff_t[]){");
    for (dimension = 0U; dimension < count; ++dimension)
        f2c_buffer_printf(output, "%s(ptrdiff_t)(%s)", dimension == 0U ? "" : ", ",
                          values[dimension]);
    f2c_buffer_append(output, "}");
}

static void free_target_arrays(char **lower, char **extent, char **stride, char **first,
                               char **last, char **step, size_t count) {
    size_t dimension;
    for (dimension = 0U; dimension < count; ++dimension) {
        free(lower[dimension]);
        free(extent[dimension]);
        free(stride[dimension]);
        free(first[dimension]);
        free(last[dimension]);
        free(step[dimension]);
    }
}

char *f2c_expression_associated_array_target(Unit *unit, const F2cExpr *pointer,
                                             const F2cExpr *target, const char *pointer_storage,
                                             int *supported) {
    const Symbol *pointer_symbol = pointer != NULL ? pointer->symbol : NULL;
    const Symbol *target_symbol = target != NULL ? target->symbol : NULL;
    char *lower[F2C_MAX_RANK] = {0};
    char *extent[F2C_MAX_RANK] = {0};
    char *stride[F2C_MAX_RANK] = {0};
    char *first[F2C_MAX_RANK] = {0};
    char *last[F2C_MAX_RANK] = {0};
    char *step[F2C_MAX_RANK] = {0};
    int section[F2C_MAX_RANK] = {0};
    char *pointer_extent[F2C_MAX_RANK] = {0};
    char *pointer_stride[F2C_MAX_RANK] = {0};
    char *target_storage = NULL;
    char *element_size = NULL;
    Buffer result = {0};
    size_t dimension;
    int success = 0;
    if (pointer_symbol == NULL || target_symbol == NULL || pointer_symbol->rank == 0U ||
        target_symbol->rank == 0U || target_symbol->rank > F2C_MAX_RANK ||
        (target->kind != F2C_EXPR_NAME && target->kind != F2C_EXPR_ARRAY_REFERENCE &&
         target->kind != F2C_EXPR_COMPONENT))
        return NULL;
    if ((target->kind == F2C_EXPR_ARRAY_REFERENCE ||
         (target->kind == F2C_EXPR_COMPONENT && target->child_count > 1U)) &&
        target->child_count != target_symbol->rank + f2c_descriptor_selector_offset(target))
        return NULL;
    target_storage = f2c_descriptor_storage_designator(unit, target);
    if (target_storage == NULL)
        return NULL;
    for (dimension = 0U; dimension < target_symbol->rank; ++dimension) {
        const F2cExpr *selector =
            target->kind == F2C_EXPR_NAME ? NULL : f2c_descriptor_selector(target, dimension);
        lower[dimension] = f2c_descriptor_dimension_lower(unit, target, dimension);
        extent[dimension] = f2c_descriptor_dimension_extent(unit, target, dimension);
        stride[dimension] = f2c_descriptor_expression_stride(unit, target, dimension);
        if (selector == NULL || selector->kind == F2C_EXPR_ARRAY_SECTION) {
            const F2cExpr *lower_bound =
                selector != NULL && selector->child_count == 3U ? selector->children[0] : NULL;
            const F2cExpr *upper_bound =
                selector != NULL && selector->child_count == 3U ? selector->children[1] : NULL;
            const F2cExpr *stride_value =
                selector != NULL && selector->child_count == 3U ? selector->children[2] : NULL;
            section[dimension] = 1;
            first[dimension] = lower_bound != NULL && lower_bound->kind != F2C_EXPR_INVALID
                                   ? f2c_expression_emit(unit, lower_bound, supported)
                                   : f2c_descriptor_dimension_lower(unit, target, dimension);
            last[dimension] = upper_bound != NULL && upper_bound->kind != F2C_EXPR_INVALID
                                  ? f2c_expression_emit(unit, upper_bound, supported)
                                  : f2c_descriptor_dimension_upper(unit, target, dimension);
            step[dimension] = stride_value != NULL && stride_value->kind != F2C_EXPR_INVALID
                                  ? f2c_expression_emit(unit, stride_value, supported)
                                  : f2c_strdup("1");
        } else {
            first[dimension] = f2c_expression_emit(unit, selector, supported);
            last[dimension] = f2c_strdup("0");
            step[dimension] = f2c_strdup("1");
        }
        if (!*supported || lower[dimension] == NULL || extent[dimension] == NULL ||
            stride[dimension] == NULL || first[dimension] == NULL || last[dimension] == NULL ||
            step[dimension] == NULL)
            goto cleanup;
    }
    for (dimension = 0U; dimension < pointer_symbol->rank; ++dimension) {
        Buffer pointer_extent_name = {0};
        Buffer pointer_stride_name = {0};
        f2c_buffer_printf(&pointer_extent_name, "%s_extent_%zu", pointer_storage, dimension + 1U);
        f2c_buffer_printf(&pointer_stride_name, "%s_stride_%zu", pointer_storage, dimension + 1U);
        pointer_extent[dimension] = f2c_buffer_take(&pointer_extent_name);
        pointer_stride[dimension] = f2c_buffer_take(&pointer_stride_name);
        if (pointer_extent[dimension] == NULL || pointer_stride[dimension] == NULL)
            goto cleanup;
    }
    if (target_symbol->type == TYPE_CHARACTER)
        element_size = f2c_character_length_expression(unit, target);
    else {
        Buffer size = {0};
        f2c_buffer_printf(&size, "sizeof(%s)", f2c_symbol_c_type(target_symbol));
        element_size = f2c_buffer_take(&size);
    }
    if (element_size == NULL)
        goto cleanup;
    f2c_buffer_printf(&result, "f2c_associated_array_target((const void *)(%s), %zuU, ",
                      pointer_storage, pointer_symbol->rank);
    append_size_array(&result, pointer_extent, pointer_symbol->rank);
    f2c_buffer_append(&result, ", ");
    append_stride_array(&result, pointer_stride, pointer_symbol->rank);
    f2c_buffer_printf(&result, ", (const void *)(%s), (size_t)(%s), %zuU, ", target_storage,
                      element_size, target_symbol->rank);
    append_int64_array(&result, lower, target_symbol->rank);
    f2c_buffer_append(&result, ", ");
    append_size_array(&result, extent, target_symbol->rank);
    f2c_buffer_append(&result, ", ");
    append_stride_array(&result, stride, target_symbol->rank);
    f2c_buffer_append(&result, ", (const bool[]){");
    for (dimension = 0U; dimension < target_symbol->rank; ++dimension)
        f2c_buffer_printf(&result, "%s%s", dimension == 0U ? "" : ", ",
                          section[dimension] ? "true" : "false");
    f2c_buffer_append(&result, "}, ");
    append_int64_array(&result, first, target_symbol->rank);
    f2c_buffer_append(&result, ", ");
    append_int64_array(&result, last, target_symbol->rank);
    f2c_buffer_append(&result, ", ");
    append_int64_array(&result, step, target_symbol->rank);
    f2c_buffer_append(&result, ")");
    success = 1;

cleanup:
    free_target_arrays(lower, extent, stride, first, last, step, target_symbol->rank);
    for (dimension = 0U; dimension < pointer_symbol->rank; ++dimension) {
        free(pointer_extent[dimension]);
        free(pointer_stride[dimension]);
    }
    free(element_size);
    free(target_storage);
    if (!success) {
        free(result.data);
        return NULL;
    }
    return f2c_buffer_take(&result);
}
