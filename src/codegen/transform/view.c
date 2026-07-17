#include "codegen/transform/private.h"

#include <stdlib.h>
#include <string.h>

void f2c_transform_indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

const F2cExpr *f2c_transform_argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

const F2cExpr *f2c_transform_argument(const F2cExpr *call, const char *keyword, size_t position) {
    size_t positional = 0U;
    size_t i;
    if (call == NULL)
        return NULL;
    for (i = 0U; i < call->child_count; ++i) {
        const F2cExpr *argument = call->children[i];
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (argument->text != NULL && strcmp(argument->text, keyword) == 0)
                return f2c_transform_argument_value(argument);
        } else if (positional++ == position) {
            return argument;
        }
    }
    return NULL;
}

char *f2c_transform_emit_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result =
        expression != NULL ? f2c_emit_expression_ast(unit, expression, &supported) : NULL;
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

void f2c_transform_free_array(TransformArray *array) {
    size_t dimension;
    free(array->pointer);
    free(array->count);
    free(array->element_length);
    for (dimension = 0U; dimension < array->rank; ++dimension)
        free(array->extents[dimension]);
    memset(array, 0, sizeof(*array));
}

int f2c_transform_array_view(Unit *unit, const F2cExpr *expression, TransformArray *array) {
    size_t dimension;
    memset(array, 0, sizeof(*array));
    expression = f2c_transform_argument_value(expression);
    if (expression == NULL || expression->rank == 0U)
        return 0;
    array->expression = expression;
    array->rank = expression->rank;
    array->type = expression->type;
    array->derived_type = expression->derived_type;
    if (expression->type == TYPE_CHARACTER)
        array->element_length = f2c_character_length_expression(unit, expression);
    if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL) {
        array->symbol = expression->symbol;
        array->pointer = f2c_strdup(f2c_symbol_c_name(unit, expression->symbol));
        array->count = f2c_symbol_element_count(unit, expression->symbol);
        for (dimension = 0U; dimension < array->rank; ++dimension)
            array->extents[dimension] =
                f2c_symbol_dimension_extent(unit, expression->symbol, dimension);
    } else if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR && expression->rank == 1U) {
        array->pointer = f2c_transform_emit_expression(unit, expression);
        {
            Buffer count = {0};
            f2c_buffer_printf(&count, "%zuU", expression->child_count);
            array->count = f2c_buffer_take(&count);
            array->extents[0] = f2c_strdup(array->count != NULL ? array->count : "0U");
        }
    }
    if (array->pointer == NULL || array->count == NULL) {
        f2c_transform_free_array(array);
        return 0;
    }
    for (dimension = 0U; dimension < array->rank; ++dimension) {
        if (array->extents[dimension] == NULL) {
            f2c_transform_free_array(array);
            return 0;
        }
    }
    return 1;
}

int f2c_transform_supported_element_type(const Symbol *target) {
    return target != NULL && target->type != TYPE_UNKNOWN &&
           (target->type != TYPE_DERIVED || target->derived_type != NULL);
}

int f2c_transform_compatible_array(const Symbol *target, const TransformArray *array) {
    return target != NULL && array != NULL && target->type == array->type &&
           (target->type != TYPE_DERIVED || target->derived_type == array->derived_type);
}
