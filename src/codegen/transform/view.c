#include "codegen/transform/private.h"

#include "codegen/array/private.h"

#include <stdio.h>
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
        if (!expression->symbol->argument || !f2c_symbol_uses_descriptor(expression->symbol))
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
    } else if (expression->type != TYPE_DERIVED || expression->kind == F2C_EXPR_ARRAY_REFERENCE ||
               (expression->kind == F2C_EXPR_CALL && expression->resolved_procedure != NULL &&
                expression->resolved_procedure->elemental)) {
        const char *ordinals[F2C_MAX_RANK] = {0};
        F2cExpr *element;
        char *code;
        Buffer count = {0};
        for (dimension = 0U; dimension < array->rank; ++dimension) {
            ordinals[dimension] = "0U";
            array->extents[dimension] = f2c_array_expression_extent(unit, expression, dimension);
        }
        element = f2c_array_element_expression(unit, expression, array->rank, ordinals);
        code = element != NULL ? f2c_transform_emit_expression(unit, element) : NULL;
        if (element == NULL || code == NULL) {
            f2c_expr_free(element);
            free(code);
            f2c_transform_free_array(array);
            return 0;
        }
        f2c_expr_free(element);
        free(code);
        f2c_buffer_printf(&count, "f2c_inquiry_size(%zuU, (const size_t[]){", array->rank);
        for (dimension = 0U; dimension < array->rank; ++dimension)
            f2c_buffer_printf(&count, "%s(size_t)(%s)", dimension == 0U ? "" : ", ",
                              array->extents[dimension] != NULL ? array->extents[dimension] : "0U");
        f2c_buffer_append(&count, "})");
        array->count = f2c_buffer_take(&count);
    }
    if (array->count == NULL) {
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

int f2c_transform_materialize_array(Context *context, Unit *unit, TransformArray *array,
                                    const char *role, int depth) {
    const char *ordinals[F2C_MAX_RANK] = {0};
    char ordinal_names[F2C_MAX_RANK][96];
    char *element_code;
    F2cExpr *element;
    Buffer pointer_name = {0};
    Buffer count_name = {0};
    size_t dimension;
    int element_definable;
    F2cExprKind element_kind;
    if (context == NULL || unit == NULL || array == NULL || role == NULL)
        return 0;
    if (array->pointer != NULL)
        return 1;
    if (array->expression == NULL || array->rank == 0U ||
        (array->type == TYPE_DERIVED &&
         (array->derived_type == NULL || array->derived_type->c_name == NULL)))
        return 0;
    for (dimension = 0U; dimension < array->rank; ++dimension) {
        char *extent_name;
        Buffer extent = {0};
        f2c_buffer_printf(&extent, "f2c_transform_%s_extent_%zu", role, dimension + 1U);
        extent_name = f2c_buffer_take(&extent);
        if (extent_name == NULL)
            return 0;
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const size_t %s = (size_t)(%s);\n", extent_name,
                          array->extents[dimension]);
        free(array->extents[dimension]);
        array->extents[dimension] = extent_name;
        (void)snprintf(ordinal_names[dimension], sizeof(ordinal_names[dimension]),
                       "f2c_transform_%s_ordinal_%zu", role, dimension + 1U);
        ordinals[dimension] = ordinal_names[dimension];
    }
    element = f2c_array_element_expression(unit, array->expression, array->rank, ordinals);
    element_code = element != NULL ? f2c_transform_emit_expression(unit, element) : NULL;
    element_definable = element != NULL && element->definable;
    element_kind = element != NULL ? element->kind : F2C_EXPR_INVALID;
    f2c_expr_free(element);
    if (element_code == NULL ||
        (array->type == TYPE_DERIVED && !element_definable && element_kind != F2C_EXPR_CALL &&
         element_kind != F2C_EXPR_STRUCTURE_CONSTRUCTOR)) {
        free(element_code);
        return 0;
    }
    f2c_buffer_printf(&pointer_name, "f2c_transform_%s_values", role);
    f2c_buffer_printf(&count_name, "f2c_transform_%s_count", role);
    if (pointer_name.data == NULL || count_name.data == NULL) {
        free(element_code);
        free(pointer_name.data);
        free(count_name.data);
        return 0;
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "const size_t %s = f2c_inquiry_size(%zuU, "
                      "(const size_t[]){",
                      count_name.data, array->rank);
    for (dimension = 0U; dimension < array->rank; ++dimension)
        f2c_buffer_printf(&context->output, "%s%s", dimension == 0U ? "" : ", ",
                          array->extents[dimension]);
    f2c_buffer_append(&context->output, "});\n");
    f2c_transform_indent(&context->output, depth);
    if (array->type == TYPE_CHARACTER) {
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transform_%s_element_length = (size_t)(%s);\n", role,
                          array->element_length != NULL ? array->element_length : "0U");
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_transform_%s_element_length != 0U && %s > SIZE_MAX / "
                          "f2c_transform_%s_element_length) abort();\n",
                          role, count_name.data, role);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "char *%s = (char *)malloc(%s == 0U || "
                          "f2c_transform_%s_element_length == 0U ? 1U : %s * "
                          "f2c_transform_%s_element_length);\n",
                          pointer_name.data, count_name.data, role, count_name.data, role);
    } else if (array->type == TYPE_DERIVED) {
        const char *element_type = array->derived_type->c_name;
        f2c_buffer_printf(&context->output, "if (%s > SIZE_MAX / sizeof(%s)) abort();\n",
                          count_name.data, element_type);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "%s *%s = (%s *)calloc(%s == 0U ? 1U : %s, sizeof(%s));\n", element_type,
                          pointer_name.data, element_type, count_name.data, count_name.data,
                          element_type);
    } else {
        const char *element_type = f2c_c_type_kind(array->type, array->expression->type_kind);
        f2c_buffer_printf(&context->output, "if (%s > SIZE_MAX / sizeof(%s)) abort();\n",
                          count_name.data, element_type);
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "%s *%s = (%s *)malloc(%s == 0U ? sizeof(%s) : %s * sizeof(%s));\n",
                          element_type, pointer_name.data, element_type, count_name.data,
                          element_type, count_name.data, element_type);
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "if (%s == NULL) abort();\n", pointer_name.data);
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "for (size_t f2c_transform_%s_index = 0U; f2c_transform_%s_index < %s; "
                      "++f2c_transform_%s_index) { size_t f2c_transform_%s_ordinal = "
                      "f2c_transform_%s_index; ",
                      role, role, count_name.data, role, role, role);
    for (dimension = 0U; dimension < array->rank; ++dimension)
        f2c_buffer_printf(&context->output,
                          "size_t %s = f2c_transform_%s_ordinal %% %s; "
                          "f2c_transform_%s_ordinal /= %s; ",
                          ordinal_names[dimension], role, array->extents[dimension], role,
                          array->extents[dimension]);
    if (array->type == TYPE_CHARACTER) {
        f2c_buffer_printf(&context->output,
                          "char *f2c_destination = %s + f2c_transform_%s_index * "
                          "f2c_transform_%s_element_length; const char *f2c_source = %s(%s); "
                          "if (f2c_transform_%s_element_length != 0U) memmove(f2c_destination, "
                          "f2c_source, f2c_transform_%s_element_length); }\n",
                          pointer_name.data, role, role, element_definable ? "&" : "", element_code,
                          role, role);
        free(array->element_length);
        {
            Buffer length = {0};
            f2c_buffer_printf(&length, "f2c_transform_%s_element_length", role);
            array->element_length = f2c_buffer_take(&length);
        }
    } else if (array->type == TYPE_DERIVED) {
        if (element_definable) {
            f2c_buffer_printf(&context->output,
                              "f2c_clone_%s(&%s[f2c_transform_%s_index], &(%s)); }\n",
                              array->derived_type->c_name, pointer_name.data, role, element_code);
        } else {
            f2c_buffer_printf(&context->output, "%s f2c_source = (%s); ",
                              array->derived_type->c_name, element_code);
            if (element_kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR)
                f2c_buffer_printf(&context->output, "f2c_initialize_%s(&f2c_source); ",
                                  array->derived_type->c_name);
            f2c_buffer_printf(&context->output,
                              "f2c_clone_%s(&%s[f2c_transform_%s_index], &f2c_source); "
                              "f2c_destroy_%s(&f2c_source); }\n",
                              array->derived_type->c_name, pointer_name.data, role,
                              array->derived_type->c_name);
        }
    } else {
        f2c_buffer_printf(&context->output, "%s[f2c_transform_%s_index] = (%s); }\n",
                          pointer_name.data, role, element_code);
    }
    free(element_code);
    free(array->pointer);
    free(array->count);
    array->pointer = f2c_buffer_take(&pointer_name);
    array->count = f2c_buffer_take(&count_name);
    array->temporary = 1;
    return array->pointer != NULL && array->count != NULL;
}

void f2c_transform_emit_array_cleanup(Context *context, const TransformArray *array, int depth) {
    if (context == NULL || array == NULL || !array->temporary || array->pointer == NULL)
        return;
    if (array->type == TYPE_DERIVED && array->derived_type != NULL && array->count != NULL) {
        f2c_transform_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "f2c_destroy_array_%s(%s, %s, %zuU);\n",
                          array->derived_type->c_name, array->pointer, array->count, array->rank);
    }
    f2c_transform_indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "free(%s);\n", array->pointer);
}

int f2c_transform_supported_element_type(const Symbol *target) {
    return target != NULL && target->type != TYPE_UNKNOWN &&
           (target->type != TYPE_DERIVED || target->derived_type != NULL);
}

int f2c_transform_compatible_array(const Symbol *target, const TransformArray *array) {
    return target != NULL && array != NULL && target->type == array->type &&
           (target->type != TYPE_DERIVED || target->derived_type == array->derived_type);
}
