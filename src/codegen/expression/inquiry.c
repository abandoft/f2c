#include "codegen/expression/private.h"

#include "codegen/array/private.h"
#include "codegen/descriptor/private.h"

#include <stdlib.h>
#include <string.h>

static const F2cExpr *inquiry_argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static const F2cExpr *inquiry_argument(const F2cExpr *call, const char *keyword, size_t position) {
    size_t positional = 0U;
    size_t argument;
    if (call == NULL)
        return NULL;
    for (argument = 0U; argument < call->child_count; ++argument) {
        const F2cExpr *actual = call->children[argument];
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (actual->text != NULL && strcmp(actual->text, keyword) == 0)
                return inquiry_argument_value(actual);
        } else if (positional++ == position) {
            return actual;
        }
    }
    return NULL;
}

static void free_inquiry_dimensions(char **extents, char **lowers, size_t rank) {
    size_t dimension;
    for (dimension = 0U; dimension < rank; ++dimension) {
        free(extents[dimension]);
        free(lowers[dimension]);
    }
}

char *f2c_expression_array_inquiry(Unit *unit, const F2cExpr *expression, int *supported) {
    const char *name = expression != NULL ? expression->text : NULL;
    const F2cExpr *array;
    const F2cExpr *dimension;
    char *extents[F2C_MAX_RANK] = {0};
    char *lowers[F2C_MAX_RANK] = {0};
    char *dimension_code = NULL;
    Buffer extent_list = {0};
    Buffer lower_list = {0};
    Buffer result = {0};
    size_t metadata_rank;
    size_t index;
    int assumed_size;
    if (name == NULL ||
        (strcmp(name, "size") != 0 && strcmp(name, "lbound") != 0 && strcmp(name, "ubound") != 0))
        return NULL;
    array = inquiry_argument(expression, "array", 0U);
    dimension = inquiry_argument(expression, "dim", 1U);
    if (array == NULL || array->rank == 0U || array->rank > F2C_MAX_RANK ||
        ((strcmp(name, "lbound") == 0 || strcmp(name, "ubound") == 0) && dimension == NULL)) {
        *supported = 0;
        return NULL;
    }
    assumed_size = f2c_expression_is_whole_assumed_size(array);
    metadata_rank = array->rank;
    if (assumed_size && strcmp(name, "lbound") != 0)
        --metadata_rank;
    if (assumed_size && strcmp(name, "size") == 0 && dimension == NULL)
        goto unsupported;
    for (index = 0U; index < metadata_rank; ++index) {
        if (assumed_size && strcmp(name, "lbound") == 0 && index + 1U == array->rank)
            extents[index] = f2c_strdup("1U");
        else
            extents[index] = f2c_array_expression_extent(unit, array, index);
        if ((array->kind == F2C_EXPR_NAME ||
             (array->kind == F2C_EXPR_COMPONENT && array->child_count == 1U)) &&
            array->symbol != NULL)
            lowers[index] = f2c_descriptor_dimension_lower(unit, array, index);
        else
            lowers[index] = f2c_strdup("1");
        if (extents[index] == NULL || lowers[index] == NULL)
            goto unsupported;
        f2c_buffer_printf(&extent_list, "%s(size_t)(%s)", index == 0U ? "" : ", ", extents[index]);
        f2c_buffer_printf(&lower_list, "%s(int64_t)(%s)", index == 0U ? "" : ", ", lowers[index]);
    }
    if (dimension != NULL)
        dimension_code = f2c_expression_emit(unit, dimension, supported);
    if (!*supported || (dimension != NULL && dimension_code == NULL))
        goto unsupported;
    f2c_buffer_printf(&result, "((%s)", f2c_expression_c_type(expression));
    if (strcmp(name, "size") == 0) {
        f2c_buffer_append(&result, "f2c_inquiry_size_integer(");
        if (dimension_code != NULL)
            f2c_buffer_printf(&result,
                              "f2c_inquiry_extent((int64_t)(%s), %zuU, "
                              "(const size_t[]){%s})",
                              dimension_code, metadata_rank,
                              extent_list.data != NULL ? extent_list.data : "");
        else
            f2c_buffer_printf(&result, "f2c_inquiry_size(%zuU, (const size_t[]){%s})",
                              metadata_rank, extent_list.data != NULL ? extent_list.data : "");
        f2c_buffer_printf(&result, ", %d))", expression->type_kind);
    } else if (strcmp(name, "lbound") == 0) {
        f2c_buffer_printf(&result,
                          "f2c_inquiry_bound_integer(f2c_inquiry_lower((int64_t)(%s), %zuU, "
                          "(const int64_t[]){%s}, (const size_t[]){%s}), %d))",
                          dimension_code, metadata_rank,
                          lower_list.data != NULL ? lower_list.data : "",
                          extent_list.data != NULL ? extent_list.data : "", expression->type_kind);
    } else {
        f2c_buffer_printf(&result,
                          "f2c_inquiry_bound_integer(f2c_inquiry_upper_dimension((int64_t)(%s), "
                          "%zuU, (const int64_t[]){%s}, (const size_t[]){%s}), %d))",
                          dimension_code, metadata_rank,
                          lower_list.data != NULL ? lower_list.data : "",
                          extent_list.data != NULL ? extent_list.data : "", expression->type_kind);
    }
    free_inquiry_dimensions(extents, lowers, array->rank);
    free(dimension_code);
    free(extent_list.data);
    free(lower_list.data);
    return f2c_buffer_take(&result);

unsupported:
    free_inquiry_dimensions(extents, lowers, array->rank);
    free(dimension_code);
    free(extent_list.data);
    free(lower_list.data);
    free(result.data);
    *supported = 0;
    return NULL;
}
