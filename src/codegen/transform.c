#include "internal/f2c.h"

#include <stdlib.h>
#include <string.h>

typedef struct TransformArray {
    const F2cExpr *expression;
    Symbol *symbol;
    Type type;
    F2cDerivedType *derived_type;
    char *pointer;
    char *count;
    char *element_length;
    char *extents[F2C_MAX_RANK];
    size_t rank;
} TransformArray;

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static const F2cExpr *argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static const F2cExpr *transform_argument(const F2cExpr *call, const char *keyword,
                                         size_t position) {
    size_t positional = 0U;
    size_t i;
    if (call == NULL)
        return NULL;
    for (i = 0U; i < call->child_count; ++i) {
        const F2cExpr *argument = call->children[i];
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (argument->text != NULL && strcmp(argument->text, keyword) == 0)
                return argument_value(argument);
        } else if (positional++ == position) {
            return argument;
        }
    }
    return NULL;
}

static char *emit_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result =
        expression != NULL ? f2c_emit_expression_ast(unit, expression, &supported) : NULL;
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

static void free_array(TransformArray *array) {
    size_t dimension;
    free(array->pointer);
    free(array->count);
    free(array->element_length);
    for (dimension = 0U; dimension < array->rank; ++dimension)
        free(array->extents[dimension]);
    memset(array, 0, sizeof(*array));
}

static int array_view(Unit *unit, const F2cExpr *expression, TransformArray *array) {
    size_t dimension;
    memset(array, 0, sizeof(*array));
    expression = argument_value(expression);
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
        array->pointer = emit_expression(unit, expression);
        {
            Buffer count = {0};
            f2c_buffer_printf(&count, "%zuU", expression->child_count);
            array->count = f2c_buffer_take(&count);
            array->extents[0] = f2c_strdup(array->count != NULL ? array->count : "0U");
        }
    }
    if (array->pointer == NULL || array->count == NULL) {
        free_array(array);
        return 0;
    }
    for (dimension = 0U; dimension < array->rank; ++dimension) {
        if (array->extents[dimension] == NULL) {
            free_array(array);
            return 0;
        }
    }
    return 1;
}

static int supported_element_type(const Symbol *target) {
    return target != NULL && target->type != TYPE_UNKNOWN &&
           (target->type != TYPE_DERIVED || target->derived_type != NULL);
}

static int compatible_array(const Symbol *target, const TransformArray *array) {
    return target != NULL && array != NULL && target->type == array->type &&
           (target->type != TYPE_DERIVED || target->derived_type == array->derived_type);
}

static void emit_result_count(Context *context, size_t rank, int depth) {
    size_t dimension;
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "size_t f2c_transform_result_count = 1U;\n");
    for (dimension = 0U; dimension < rank; ++dimension) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_transform_result_extent_%zu != 0U && "
                          "f2c_transform_result_count > SIZE_MAX / "
                          "f2c_transform_result_extent_%zu) abort();\n",
                          dimension + 1U, dimension + 1U);
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "f2c_transform_result_count *= f2c_transform_result_extent_%zu;\n",
                          dimension + 1U);
    }
}

static void emit_result_allocation(Context *context, Unit *unit, const Symbol *target,
                                   const F2cExpr *element_source, int depth) {
    if (target->type == TYPE_CHARACTER) {
        char *length = target->deferred_character
                           ? f2c_character_length_expression(unit, element_source)
                           : f2c_symbol_character_length(unit, target);
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transform_result_element_length = (size_t)(%s);\n",
                          length != NULL ? length : "0U");
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "if (f2c_transform_result_element_length != 0U && "
                                            "f2c_transform_result_count > SIZE_MAX / "
                                            "f2c_transform_result_element_length) abort();\n");
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "char *f2c_transform_result = (char *)malloc("
                                            "f2c_transform_result_count == 0U || "
                                            "f2c_transform_result_element_length == 0U ? 1U : "
                                            "f2c_transform_result_count * "
                                            "f2c_transform_result_element_length);\n");
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "if (f2c_transform_result == NULL) abort();\n");
        free(length);
        return;
    }
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output,
                      "if (f2c_transform_result_count > SIZE_MAX / sizeof(%s)) abort();\n",
                      f2c_symbol_c_type(target));
    indent(&context->output, depth);
    if (target->type == TYPE_DERIVED)
        f2c_buffer_printf(&context->output,
                          "%s *f2c_transform_result = (%s *)calloc("
                          "f2c_transform_result_count == 0U ? 1U : "
                          "f2c_transform_result_count, sizeof(%s));\n",
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target),
                          f2c_symbol_c_type(target));
    else
        f2c_buffer_printf(&context->output,
                          "%s *f2c_transform_result = (%s *)malloc("
                          "f2c_transform_result_count == 0U ? sizeof(%s) : "
                          "f2c_transform_result_count * sizeof(%s));\n",
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target),
                          f2c_symbol_c_type(target), f2c_symbol_c_type(target));
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "if (f2c_transform_result == NULL) abort();\n");
}

static void emit_result_commit(Context *context, Unit *unit, Symbol *target, size_t rank,
                               int depth) {
    const char *name = f2c_symbol_c_name(unit, target);
    size_t dimension;
    if (target->allocatable) {
        indent(&context->output, depth);
        if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
            char *old_count = f2c_symbol_element_count(unit, target);
            f2c_buffer_printf(&context->output, "if (%s != NULL) {\n", name);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output, "f2c_destroy_array_%s(%s, (size_t)(%s), %zuU);\n",
                              target->derived_type->c_name, name,
                              old_count != NULL ? old_count : "0U", rank);
            indent(&context->output, depth);
            f2c_buffer_append(&context->output, "}\n");
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "free(%s);\n", name);
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = f2c_transform_result;\n", name);
            free(old_count);
        } else {
            f2c_buffer_printf(&context->output, "free(%s);\n", name);
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output, "%s = f2c_transform_result;\n", name);
        }
        if (target->type == TYPE_CHARACTER && target->deferred_character) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "f2c_char_len_%s = f2c_transform_result_element_length;\n", name);
        }
        for (dimension = 0U; dimension < rank; ++dimension) {
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "%s_lower_%zu = 1; %s_extent_%zu = "
                              "(int32_t)f2c_transform_result_extent_%zu;\n",
                              name, dimension + 1U, name, dimension + 1U, dimension + 1U);
        }
    } else {
        for (dimension = 0U; dimension < rank; ++dimension) {
            char *extent = f2c_symbol_dimension_extent(unit, target, dimension);
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "if ((size_t)(%s) != f2c_transform_result_extent_%zu) abort();\n",
                              extent != NULL ? extent : "0U", dimension + 1U);
            free(extent);
        }
        indent(&context->output, depth);
        if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
            f2c_buffer_printf(&context->output,
                              "for (size_t i = 0U; i < f2c_transform_result_count; ++i) "
                              "f2c_copy_%s(&%s[i], &f2c_transform_result[i]);\n",
                              target->derived_type->c_name, name);
            indent(&context->output, depth);
            f2c_buffer_printf(&context->output,
                              "f2c_destroy_array_%s(f2c_transform_result, "
                              "f2c_transform_result_count, %zuU);\n",
                              target->derived_type->c_name, rank);
        } else if (target->type == TYPE_CHARACTER) {
            f2c_buffer_printf(&context->output,
                              "if (f2c_transform_result_count != 0U && "
                              "f2c_transform_result_element_length != 0U) memmove(%s, "
                              "f2c_transform_result, f2c_transform_result_count * "
                              "f2c_transform_result_element_length);\n",
                              name);
        } else {
            f2c_buffer_printf(&context->output,
                              "if (f2c_transform_result_count != 0U) memmove(%s, "
                              "f2c_transform_result, f2c_transform_result_count * sizeof(*%s));\n",
                              name, name);
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "free(f2c_transform_result);\n");
    }
    indent(&context->output, depth - 1);
    f2c_buffer_append(&context->output, "}\n");
}

static void append_array_store(Buffer *output, const Symbol *target, const char *destination,
                               const TransformArray *source, const char *source_index) {
    if (target->type == TYPE_CHARACTER) {
        f2c_buffer_printf(output,
                          "{ char *f2c_dst = f2c_transform_result + (%s) * "
                          "f2c_transform_result_element_length; const char *f2c_src = %s + "
                          "(%s) * (size_t)(%s); size_t f2c_copy = "
                          "F2C_MIN(f2c_transform_result_element_length, (size_t)(%s)); "
                          "if (f2c_copy != 0U) memmove(f2c_dst, f2c_src, f2c_copy); "
                          "if (f2c_transform_result_element_length > f2c_copy) "
                          "memset(f2c_dst + f2c_copy, ' ', "
                          "f2c_transform_result_element_length - f2c_copy); } ",
                          destination, source->pointer, source_index,
                          source->element_length != NULL ? source->element_length : "0U",
                          source->element_length != NULL ? source->element_length : "0U");
    } else if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
        f2c_buffer_printf(output, "f2c_clone_%s(&f2c_transform_result[%s], &%s[%s]); ",
                          target->derived_type->c_name, destination, source->pointer, source_index);
    } else {
        f2c_buffer_printf(output, "f2c_transform_result[%s] = %s[%s]; ", destination,
                          source->pointer, source_index);
    }
}

static void append_scalar_store(Buffer *output, const Symbol *target, const char *destination,
                                const char *source, const char *source_length) {
    if (target->type == TYPE_CHARACTER) {
        f2c_buffer_printf(output,
                          "{ char *f2c_dst = f2c_transform_result + (%s) * "
                          "f2c_transform_result_element_length; const char *f2c_src = (%s); "
                          "size_t f2c_src_len = (size_t)(%s); size_t f2c_copy = "
                          "F2C_MIN(f2c_transform_result_element_length, f2c_src_len); "
                          "if (f2c_copy != 0U) memmove(f2c_dst, f2c_src, f2c_copy); "
                          "if (f2c_transform_result_element_length > f2c_copy) "
                          "memset(f2c_dst + f2c_copy, ' ', "
                          "f2c_transform_result_element_length - f2c_copy); } ",
                          destination, source, source_length != NULL ? source_length : "0U");
    } else if (target->type == TYPE_DERIVED && target->derived_type != NULL) {
        f2c_buffer_printf(output, "f2c_clone_%s(&f2c_transform_result[%s], &(%s)); ",
                          target->derived_type->c_name, destination, source);
    } else {
        f2c_buffer_printf(output, "f2c_transform_result[%s] = (%s); ", destination, source);
    }
}

static void emit_source_extents(Context *context, const TransformArray *source, int depth) {
    size_t dimension;
    for (dimension = 0U; dimension < source->rank; ++dimension) {
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_source_extent_%zu = %s;\n",
                          dimension + 1U, source->extents[dimension]);
        indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "(void)f2c_transform_source_extent_%zu;\n",
                          dimension + 1U);
    }
    indent(&context->output, depth);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_source_count = %s;\n",
                      source->count);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "(void)f2c_transform_source_count;\n");
}

static char *vector_element(Unit *unit, const F2cExpr *vector, size_t index) {
    const F2cExpr *value = argument_value(vector);
    if (value == NULL)
        return NULL;
    if (value->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        if (index >= value->child_count)
            return NULL;
        return emit_expression(unit, value->children[index]);
    }
    if (value->kind == F2C_EXPR_NAME && value->symbol != NULL && value->rank == 1U) {
        Buffer result = {0};
        f2c_buffer_printf(&result, "%s[%zuU]", f2c_symbol_c_name(unit, value->symbol), index);
        return f2c_buffer_take(&result);
    }
    return NULL;
}

static int emit_reshape(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                        size_t line, int depth) {
    const F2cExpr *source_expression = transform_argument(call, "source", 0U);
    const F2cExpr *shape = transform_argument(call, "shape", 1U);
    const F2cExpr *pad_expression = transform_argument(call, "pad", 2U);
    const F2cExpr *order = transform_argument(call, "order", 3U);
    TransformArray source = {0};
    TransformArray pad = {0};
    size_t rank = call->rank;
    size_t dimension;
    if (!array_view(unit, source_expression, &source) || !compatible_array(target, &source) ||
        rank == 0U || rank != target->rank ||
        (pad_expression != NULL &&
         (!array_view(unit, pad_expression, &pad) || !compatible_array(target, &pad)))) {
        f2c_diagnostic(context, line, 1,
                       "RESHAPE requires array SOURCE/PAD, a known SHAPE rank, and a conforming "
                       "target");
        free_array(&source);
        free_array(&pad);
        return 1;
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    for (dimension = 0U; dimension < rank; ++dimension) {
        char *extent = vector_element(unit, shape, dimension);
        if (extent == NULL) {
            f2c_diagnostic(context, line, 1, "RESHAPE SHAPE must provide every result extent");
            free_array(&source);
            free_array(&pad);
            return 1;
        }
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const int64_t f2c_transform_shape_%zu = (int64_t)(%s);\n",
                          dimension + 1U, extent);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_transform_shape_%zu < 0) abort();\n"
                          "%*sconst size_t f2c_transform_result_extent_%zu = "
                          "(size_t)f2c_transform_shape_%zu;\n",
                          dimension + 1U, (depth + 1) * 4, "", dimension + 1U, dimension + 1U);
        free(extent);
    }
    emit_source_extents(context, &source, depth + 1);
    if (pad_expression != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_pad_count = %s;\n",
                          pad.count);
    }
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_order[%zu] = {", rank);
    for (dimension = 0U; dimension < rank; ++dimension) {
        char *item = order != NULL ? vector_element(unit, order, dimension) : NULL;
        if (item != NULL)
            f2c_buffer_printf(&context->output, "%s(size_t)(%s)", dimension == 0U ? "" : ", ",
                              item);
        else
            f2c_buffer_printf(&context->output, "%s%zuU", dimension == 0U ? "" : ", ",
                              dimension + 1U);
        free(item);
    }
    f2c_buffer_append(&context->output, "};\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t i = 0U; i < %zuU; ++i) { if (f2c_transform_order[i] < 1U || "
                      "f2c_transform_order[i] > %zuU) abort(); for (size_t j = 0U; j < i; ++j) "
                      "if (f2c_transform_order[i] == f2c_transform_order[j]) abort(); }\n",
                      rank, rank);
    emit_result_count(context, rank, depth + 1);
    emit_result_allocation(context, unit, target, source_expression, depth + 1);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_transform_source_count < f2c_transform_result_count%s) abort();\n",
                      pad_expression != NULL ? " && f2c_transform_pad_count == 0U" : "");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t output = 0U; output < f2c_transform_result_count; ++output) { "
                      "size_t coordinates[%zu] = {0}; size_t stride = 1U; size_t sequence = 0U; "
                      "size_t multiplier = 1U; ",
                      rank);
    for (dimension = 0U; dimension < rank; ++dimension)
        f2c_buffer_printf(&context->output,
                          "coordinates[%zu] = (output / stride) %% "
                          "f2c_transform_result_extent_%zu; stride *= "
                          "f2c_transform_result_extent_%zu; ",
                          dimension, dimension + 1U, dimension + 1U);
    f2c_buffer_printf(&context->output,
                      "for (size_t k = 0U; k < %zuU; ++k) { size_t d = "
                      "f2c_transform_order[k] - 1U; sequence += coordinates[d] * multiplier; "
                      "multiplier *= ((const size_t[%zu]){",
                      rank, rank);
    for (dimension = 0U; dimension < rank; ++dimension)
        f2c_buffer_printf(&context->output, "%sf2c_transform_result_extent_%zu",
                          dimension == 0U ? "" : ", ", dimension + 1U);
    f2c_buffer_append(&context->output, "})[d]; } ");
    f2c_buffer_append(&context->output, "if (sequence < f2c_transform_source_count) { ");
    append_array_store(&context->output, target, "output", &source, "sequence");
    f2c_buffer_append(&context->output, "} ");
    if (pad_expression != NULL) {
        f2c_buffer_append(&context->output, "else { ");
        append_array_store(&context->output, target, "output", &pad,
                           "(sequence - f2c_transform_source_count) % "
                           "f2c_transform_pad_count");
        f2c_buffer_append(&context->output, "} ");
    }
    f2c_buffer_append(&context->output, "}\n");
    emit_result_commit(context, unit, target, rank, depth + 1);
    free_array(&source);
    free_array(&pad);
    return 1;
}

static char *mask_test(Unit *unit, const F2cExpr *mask, const char *index) {
    Buffer result = {0};
    const F2cExpr *value = argument_value(mask);
    char *scalar;
    if (value == NULL || value->type != TYPE_LOGICAL)
        return NULL;
    if (value->rank == 0U) {
        scalar = emit_expression(unit, value);
        if (scalar != NULL) {
            f2c_buffer_printf(&result, "(%s)", scalar);
            free(scalar);
        }
    } else if (value->kind == F2C_EXPR_NAME && value->symbol != NULL) {
        f2c_buffer_printf(&result, "%s[%s]", f2c_symbol_c_name(unit, value->symbol), index);
    }
    return f2c_buffer_take(&result);
}

static int emit_pack(Context *context, Unit *unit, Symbol *target, const F2cExpr *call, size_t line,
                     int depth) {
    TransformArray source = {0};
    TransformArray vector = {0};
    const F2cExpr *mask = transform_argument(call, "mask", 1U);
    const F2cExpr *vector_expression = transform_argument(call, "vector", 2U);
    char *condition;
    if (target->rank != 1U || !array_view(unit, transform_argument(call, "array", 0U), &source) ||
        !compatible_array(target, &source) ||
        (vector_expression != NULL &&
         (!array_view(unit, vector_expression, &vector) || !compatible_array(target, &vector)))) {
        f2c_diagnostic(context, line, 1, "PACK requires ARRAY/MASK and a rank-one result/VECTOR");
        free_array(&source);
        free_array(&vector);
        return 1;
    }
    condition = mask_test(unit, mask, "i");
    if (condition == NULL) {
        f2c_diagnostic(context, line, 1, "PACK MASK must be a scalar or stored LOGICAL array");
        free_array(&source);
        free_array(&vector);
        return 1;
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    emit_source_extents(context, &source, depth + 1);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_transform_selected = 0U;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t i = 0U; i < f2c_transform_source_count; ++i) if (%s) "
                      "++f2c_transform_selected;\n",
                      condition);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_result_extent_1 = %s;\n",
                      vector_expression != NULL ? vector.count : "f2c_transform_selected");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_transform_selected > f2c_transform_result_extent_1) abort();\n");
    emit_result_count(context, 1U, depth + 1);
    emit_result_allocation(context, unit, target, transform_argument(call, "array", 0U), depth + 1);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_transform_output = 0U;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t i = 0U; i < f2c_transform_source_count; ++i) if (%s) { ",
                      condition);
    append_array_store(&context->output, target, "f2c_transform_output", &source, "i");
    f2c_buffer_append(&context->output, "++f2c_transform_output; }\n");
    if (vector_expression != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "while (f2c_transform_output < f2c_transform_result_count) { ");
        append_array_store(&context->output, target, "f2c_transform_output", &vector,
                           "f2c_transform_output");
        f2c_buffer_append(&context->output, "++f2c_transform_output; }\n");
    }
    emit_result_commit(context, unit, target, 1U, depth + 1);
    free(condition);
    free_array(&source);
    free_array(&vector);
    return 1;
}

static int emit_unpack(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                       size_t line, int depth) {
    TransformArray vector = {0};
    TransformArray mask = {0};
    TransformArray field_array = {0};
    const F2cExpr *field = transform_argument(call, "field", 2U);
    char *field_scalar = NULL;
    char *field_length = NULL;
    size_t dimension;
    if (!array_view(unit, transform_argument(call, "vector", 0U), &vector) ||
        !compatible_array(target, &vector) ||
        !array_view(unit, transform_argument(call, "mask", 1U), &mask) ||
        mask.rank != target->rank) {
        f2c_diagnostic(context, line, 1, "UNPACK requires rank-one VECTOR and conforming MASK");
        free_array(&vector);
        free_array(&mask);
        return 1;
    }
    if (field != NULL && field->rank == 0U && field->type == target->type &&
        (target->type != TYPE_DERIVED || field->derived_type == target->derived_type))
        field_scalar = emit_expression(unit, field);
    else if (!array_view(unit, field, &field_array) || !compatible_array(target, &field_array) ||
             field_array.rank != mask.rank) {
        f2c_diagnostic(context, line, 1, "UNPACK FIELD must be scalar or conform with MASK");
        free_array(&vector);
        free_array(&mask);
        free_array(&field_array);
        return 1;
    }
    if (field_scalar != NULL && target->type == TYPE_CHARACTER)
        field_length = f2c_character_length_expression(unit, field);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    for (dimension = 0U; dimension < mask.rank; ++dimension) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_result_extent_%zu = %s;\n",
                          dimension + 1U, mask.extents[dimension]);
    }
    emit_result_count(context, mask.rank, depth + 1);
    emit_result_allocation(context, unit, target, transform_argument(call, "vector", 0U),
                           depth + 1);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_transform_vector_index = 0U;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t i = 0U; i < f2c_transform_result_count; ++i) { if (%s[i]) { "
                      "if (f2c_transform_vector_index >= (size_t)(%s)) abort(); ",
                      mask.pointer, vector.count);
    append_array_store(&context->output, target, "i", &vector, "f2c_transform_vector_index");
    f2c_buffer_append(&context->output, "++f2c_transform_vector_index; } else { ");
    if (field_scalar != NULL)
        append_scalar_store(&context->output, target, "i", field_scalar, field_length);
    else
        append_array_store(&context->output, target, "i", &field_array, "i");
    f2c_buffer_append(&context->output, "} }\n");
    emit_result_commit(context, unit, target, mask.rank, depth + 1);
    free(field_scalar);
    free(field_length);
    free_array(&vector);
    free_array(&mask);
    free_array(&field_array);
    return 1;
}

static int emit_spread(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                       size_t line, int depth) {
    const F2cExpr *source_expression = transform_argument(call, "source", 0U);
    char *source_scalar = NULL;
    TransformArray source = {0};
    char *dimension_code = emit_expression(unit, transform_argument(call, "dim", 1U));
    char *copies_code = emit_expression(unit, transform_argument(call, "ncopies", 2U));
    size_t source_rank = source_expression != NULL ? source_expression->rank : 0U;
    size_t result_rank = source_rank + 1U;
    size_t dimension;
    char *source_length = NULL;
    if (source_rank == 0U)
        source_scalar = emit_expression(unit, source_expression);
    else
        (void)array_view(unit, source_expression, &source);
    if (result_rank != target->rank || source_expression == NULL ||
        source_expression->type != target->type ||
        (target->type == TYPE_DERIVED && source_expression->derived_type != target->derived_type) ||
        dimension_code == NULL || copies_code == NULL ||
        (source_rank == 0U ? source_scalar == NULL : source.pointer == NULL)) {
        f2c_diagnostic(context, line, 1,
                       "SPREAD requires SOURCE, scalar DIM/NCOPIES, and rank+1 target");
        free(source_scalar);
        free(dimension_code);
        free(copies_code);
        free_array(&source);
        return 1;
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const int32_t f2c_transform_dimension = (int32_t)(%s); "
                      "const int64_t f2c_transform_copies_value = (int64_t)(%s); "
                      "if (f2c_transform_dimension < 1 || f2c_transform_dimension > %zu || "
                      "f2c_transform_copies_value < 0) abort();\n",
                      dimension_code, copies_code, result_rank);
    if (source_rank != 0U)
        emit_source_extents(context, &source, depth + 1);
    for (dimension = 0U; dimension < result_rank; ++dimension) {
        indent(&context->output, depth + 1);
        if (source_rank == 0U) {
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transform_result_extent_%zu = "
                              "(size_t)f2c_transform_copies_value;\n",
                              dimension + 1U);
        } else if (dimension == 0U) {
            f2c_buffer_append(&context->output, "const size_t f2c_transform_result_extent_1 = "
                                                "f2c_transform_dimension == 1 ? "
                                                "(size_t)f2c_transform_copies_value : "
                                                "f2c_transform_source_extent_1;\n");
        } else if (dimension == source_rank) {
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transform_result_extent_%zu = "
                              "f2c_transform_dimension == %zu ? "
                              "(size_t)f2c_transform_copies_value : "
                              "f2c_transform_source_extent_%zu;\n",
                              dimension + 1U, dimension + 1U, dimension);
        } else {
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transform_result_extent_%zu = "
                              "f2c_transform_dimension == %zu ? "
                              "(size_t)f2c_transform_copies_value : "
                              "(f2c_transform_dimension > %zu ? "
                              "f2c_transform_source_extent_%zu : "
                              "f2c_transform_source_extent_%zu);\n",
                              dimension + 1U, dimension + 1U, dimension + 1U, dimension,
                              dimension + 1U);
        }
    }
    emit_result_count(context, result_rank, depth + 1);
    if (source_rank == 0U && target->type == TYPE_CHARACTER)
        source_length = f2c_character_length_expression(unit, source_expression);
    emit_result_allocation(context, unit, target, source_expression, depth + 1);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t output = 0U; output < f2c_transform_result_count; ++output) { "
                      "size_t source_index = 0U, source_stride = 1U, result_stride = 1U; ");
    for (dimension = 0U; dimension < result_rank; ++dimension) {
        f2c_buffer_printf(&context->output,
                          "{ size_t coordinate = (output / result_stride) %% "
                          "f2c_transform_result_extent_%zu; if (f2c_transform_dimension != %zu) "
                          "{ source_index += coordinate * source_stride; source_stride *= "
                          "f2c_transform_result_extent_%zu; } result_stride *= "
                          "f2c_transform_result_extent_%zu; } ",
                          dimension + 1U, dimension + 1U, dimension + 1U, dimension + 1U);
    }
    if (source_rank == 0U)
        append_scalar_store(&context->output, target, "output", source_scalar, source_length);
    else
        append_array_store(&context->output, target, "output", &source, "source_index");
    f2c_buffer_append(&context->output, "}\n");
    emit_result_commit(context, unit, target, result_rank, depth + 1);
    free(source_scalar);
    free(dimension_code);
    free(copies_code);
    free(source_length);
    free_array(&source);
    return 1;
}

static char *slice_value(Unit *unit, const F2cExpr *expression, const char *slice,
                         const char *fallback) {
    const F2cExpr *value = argument_value(expression);
    Buffer result = {0};
    char *scalar;
    if (value == NULL)
        return f2c_strdup(fallback);
    if (value->rank == 0U) {
        scalar = emit_expression(unit, value);
        return scalar;
    }
    if (value->kind == F2C_EXPR_NAME && value->symbol != NULL) {
        if (value->type == TYPE_CHARACTER) {
            char *length = f2c_symbol_character_length(unit, value->symbol);
            f2c_buffer_printf(&result, "%s + (%s) * (size_t)(%s)",
                              f2c_symbol_c_name(unit, value->symbol), slice,
                              length != NULL ? length : "0U");
            free(length);
        } else {
            f2c_buffer_printf(&result, "%s[%s]", f2c_symbol_c_name(unit, value->symbol), slice);
        }
        return f2c_buffer_take(&result);
    }
    return NULL;
}

static int emit_shift(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                      size_t line, int depth, int end_off) {
    TransformArray source = {0};
    const F2cExpr *shift = transform_argument(call, "shift", 1U);
    const F2cExpr *boundary = end_off ? transform_argument(call, "boundary", 2U) : NULL;
    const F2cExpr *dim_argument = transform_argument(call, "dim", end_off ? 3U : 2U);
    char *dimension_code =
        dim_argument != NULL ? emit_expression(unit, dim_argument) : f2c_strdup("1");
    const int need_slice =
        (shift != NULL && argument_value(shift) != NULL && argument_value(shift)->rank != 0U) ||
        (boundary != NULL && argument_value(boundary) != NULL &&
         argument_value(boundary)->rank != 0U);
    size_t dimension;
    if (!array_view(unit, transform_argument(call, "array", 0U), &source) ||
        !compatible_array(target, &source) || source.rank != target->rank ||
        dimension_code == NULL || (end_off && target->type == TYPE_DERIVED && boundary == NULL)) {
        f2c_diagnostic(context, line, 1, "%s requires conforming ARRAY/result and scalar DIM",
                       end_off ? "EOSHIFT" : "CSHIFT");
        free_array(&source);
        free(dimension_code);
        return 1;
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(
        &context->output,
        "const int32_t f2c_transform_dimension = (int32_t)(%s); "
        "if (f2c_transform_dimension < 1 || f2c_transform_dimension > %zu) abort();\n",
        dimension_code, source.rank);
    emit_source_extents(context, &source, depth + 1);
    for (dimension = 0U; dimension < source.rank; ++dimension) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_transform_result_extent_%zu = "
                          "f2c_transform_source_extent_%zu;\n",
                          dimension + 1U, dimension + 1U);
    }
    emit_result_count(context, source.rank, depth + 1);
    emit_result_allocation(context, unit, target, transform_argument(call, "array", 0U), depth + 1);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t output = 0U; output < f2c_transform_result_count; ++output) { "
                      "size_t coordinates[%zu] = {0}; size_t stride = 1U; ",
                      source.rank);
    if (need_slice)
        f2c_buffer_append(&context->output, "size_t slice = 0U; size_t slice_stride = 1U; ");
    for (dimension = 0U; dimension < source.rank; ++dimension) {
        f2c_buffer_printf(&context->output,
                          "coordinates[%zu] = (output / stride) %% "
                          "f2c_transform_source_extent_%zu; stride *= "
                          "f2c_transform_source_extent_%zu; ",
                          dimension, dimension + 1U, dimension + 1U);
        if (need_slice)
            f2c_buffer_printf(&context->output,
                              "if (f2c_transform_dimension != %zu) "
                              "{ slice += coordinates[%zu] * slice_stride; slice_stride *= "
                              "f2c_transform_source_extent_%zu; } ",
                              dimension + 1U, dimension, dimension + 1U);
    }
    {
        char *shift_value = slice_value(unit, shift, "slice", "0");
        const char *boundary_fallback = target->type == TYPE_CHARACTER ? "\" \"" : "0";
        char *boundary_value = slice_value(unit, boundary, "slice", boundary_fallback);
        char *boundary_length =
            target->type == TYPE_CHARACTER
                ? (boundary != NULL
                       ? f2c_character_length_expression(unit, argument_value(boundary))
                       : f2c_strdup("1U"))
                : NULL;
        f2c_buffer_printf(&context->output,
                          "int64_t amount = (int64_t)(%s); size_t d = "
                          "(size_t)(f2c_transform_dimension - 1); int64_t extent = "
                          "(int64_t)((const size_t[%zu]){",
                          shift_value != NULL ? shift_value : "0", source.rank);
        for (dimension = 0U; dimension < source.rank; ++dimension)
            f2c_buffer_printf(&context->output, "%sf2c_transform_source_extent_%zu",
                              dimension == 0U ? "" : ", ", dimension + 1U);
        f2c_buffer_append(&context->output, "})[d]; int64_t f2c_shifted_index = "
                                            "(int64_t)coordinates[d] + amount; ");
        if (!end_off)
            f2c_buffer_append(&context->output,
                              "if (extent != 0) f2c_shifted_index = "
                              "((f2c_shifted_index % extent) + extent) % extent; ");
        f2c_buffer_append(&context->output,
                          "size_t source_index = 0U; size_t source_stride = 1U; ");
        for (dimension = 0U; dimension < source.rank; ++dimension)
            f2c_buffer_printf(&context->output,
                              "source_index += (d == %zuU ? "
                              "(size_t)(f2c_shifted_index < 0 ? 0 : f2c_shifted_index) : "
                              "coordinates[%zu]) * source_stride; source_stride *= "
                              "f2c_transform_source_extent_%zu; ",
                              dimension, dimension, dimension + 1U);
        if (end_off) {
            f2c_buffer_append(&context->output,
                              "if (f2c_shifted_index < 0 || f2c_shifted_index >= extent) { ");
            append_scalar_store(&context->output, target, "output",
                                boundary_value != NULL ? boundary_value : boundary_fallback,
                                boundary_length);
            f2c_buffer_append(&context->output, "} else { ");
            append_array_store(&context->output, target, "output", &source, "source_index");
            f2c_buffer_append(&context->output, "} ");
        } else {
            append_array_store(&context->output, target, "output", &source, "source_index");
        }
        free(shift_value);
        free(boundary_value);
        free(boundary_length);
    }
    f2c_buffer_append(&context->output, "}\n");
    emit_result_commit(context, unit, target, source.rank, depth + 1);
    free_array(&source);
    free(dimension_code);
    return 1;
}

static int emit_findloc(Context *context, Unit *unit, Symbol *target, const F2cExpr *call,
                        size_t line, int depth) {
    TransformArray source = {0};
    const F2cExpr *value_expression = transform_argument(call, "value", 1U);
    const F2cExpr *dim_expression = transform_argument(call, "dim", 2U);
    const F2cExpr *mask = transform_argument(call, "mask", 3U);
    const F2cExpr *back_expression = transform_argument(call, "back", 5U);
    char *value = emit_expression(unit, value_expression);
    char *back =
        back_expression != NULL ? emit_expression(unit, back_expression) : f2c_strdup("false");
    char *dimension_code = dim_expression != NULL ? emit_expression(unit, dim_expression) : NULL;
    char *condition = mask != NULL ? mask_test(unit, mask, "index") : f2c_strdup("true");
    char *comparison = NULL;
    size_t dimension;
    if (target->type != TYPE_INTEGER ||
        !array_view(unit, transform_argument(call, "array", 0U), &source) || value == NULL ||
        back == NULL || condition == NULL ||
        (dim_expression == NULL
             ? target->rank != 1U
             : source.rank < 2U || target->rank + 1U != source.rank || dimension_code == NULL)) {
        f2c_diagnostic(context, line, 1,
                       "FINDLOC requires stored ARRAY, scalar VALUE/BACK, conforming MASK, and a "
                       "result rank selected by DIM");
        free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        free(condition);
        return 1;
    }
    if (source.type == TYPE_DERIVED) {
        f2c_diagnostic(context, line, 1,
                       "FINDLOC does not accept a derived-type ARRAY without defined equality");
        free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        free(condition);
        return 1;
    }
    if (source.type == TYPE_CHARACTER) {
        char *value_length = f2c_character_length_expression(unit, value_expression);
        Buffer expression = {0};
        f2c_buffer_printf(&expression,
                          "f2c_character_compare(%s + index * (size_t)(%s), (size_t)(%s), "
                          "%s, (size_t)(%s)) == 0",
                          source.pointer,
                          source.element_length != NULL ? source.element_length : "0U",
                          source.element_length != NULL ? source.element_length : "0U", value,
                          value_length != NULL ? value_length : "0U");
        comparison = f2c_buffer_take(&expression);
        free(value_length);
    } else {
        Buffer expression = {0};
        f2c_buffer_printf(&expression, "%s[index] == (%s)", source.pointer, value);
        comparison = f2c_buffer_take(&expression);
    }
    if (comparison == NULL) {
        f2c_diagnostic(context, line, 1, "FINDLOC comparison could not be lowered");
        free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        free(condition);
        return 1;
    }
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    emit_source_extents(context, &source, depth + 1);
    if (dim_expression != NULL) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const int32_t f2c_transform_dimension = (int32_t)(%s); "
                          "if (f2c_transform_dimension < 1 || "
                          "f2c_transform_dimension > %zu) abort();\n",
                          dimension_code, source.rank);
        for (dimension = 0U; dimension < target->rank; ++dimension) {
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "const size_t f2c_transform_result_extent_%zu = "
                              "f2c_transform_dimension > %zu ? "
                              "f2c_transform_source_extent_%zu : "
                              "f2c_transform_source_extent_%zu;\n",
                              dimension + 1U, dimension + 1U, dimension + 1U, dimension + 2U);
        }
        emit_result_count(context, target->rank, depth + 1);
        emit_result_allocation(context, unit, target, NULL, depth + 1);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_extents[%zu] = {",
                          source.rank);
        for (dimension = 0U; dimension < source.rank; ++dimension)
            f2c_buffer_printf(&context->output, "%sf2c_transform_source_extent_%zu",
                              dimension == 0U ? "" : ", ", dimension + 1U);
        f2c_buffer_append(&context->output, "};\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(
            &context->output,
            "for (size_t output = 0U; output < f2c_transform_result_count; ++output) { "
            "size_t base = 0U, source_stride = 1U, result_stride = 1U, "
            "selected_stride = 1U; for (size_t d = 0U; d < %zuU; ++d) { "
            "if (f2c_transform_dimension == (int32_t)(d + 1U)) "
            "selected_stride = source_stride; else { size_t coordinate = "
            "(output / result_stride) %% f2c_transform_extents[d]; "
            "base += coordinate * source_stride; result_stride *= "
            "f2c_transform_extents[d]; } source_stride *= f2c_transform_extents[d]; } "
            "f2c_transform_result[output] = 0; size_t selected_extent = "
            "f2c_transform_extents[(size_t)f2c_transform_dimension - 1U]; "
            "for (size_t step = 0U; step < selected_extent; ++step) { "
            "size_t coordinate = (%s) ? selected_extent - 1U - step : step; "
            "size_t index = base + coordinate * selected_stride; "
            "if ((%s) && (%s)) { "
            "f2c_transform_result[output] = (int32_t)(coordinate + 1U); break; } } }\n",
            source.rank, back, condition, comparison);
        emit_result_commit(context, unit, target, target->rank, depth + 1);
        free_array(&source);
        free(value);
        free(back);
        free(dimension_code);
        free(condition);
        free(comparison);
        return 1;
    }
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_result_extent_1 = %zuU;\n",
                      source.rank);
    emit_result_count(context, 1U, depth + 1);
    emit_result_allocation(context, unit, target, NULL, depth + 1);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "for (size_t d = 0U; d < f2c_transform_result_count; ++d) "
                                        "f2c_transform_result[d] = 0;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (size_t step = 0U; step < f2c_transform_source_count; ++step) { "
                      "size_t index = (%s) ? f2c_transform_source_count - 1U - step : step; "
                      "if ((%s) && (%s)) { size_t stride = 1U; ",
                      back, condition, comparison);
    for (dimension = 0U; dimension < source.rank; ++dimension)
        f2c_buffer_printf(&context->output,
                          "f2c_transform_result[%zu] = (int32_t)((index / stride) %% "
                          "f2c_transform_source_extent_%zu) + 1; stride *= "
                          "f2c_transform_source_extent_%zu; ",
                          dimension, dimension + 1U, dimension + 1U);
    f2c_buffer_append(&context->output, "break; } }\n");
    emit_result_commit(context, unit, target, 1U, depth + 1);
    free_array(&source);
    free(value);
    free(back);
    free(dimension_code);
    free(condition);
    free(comparison);
    return 1;
}

int f2c_emit_transform_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                  const F2cExpr *right, size_t line, int depth) {
    Symbol *target = left != NULL ? left->symbol : NULL;
    const char *name = right != NULL ? right->text : NULL;
    if (context == NULL || unit == NULL || target == NULL || right == NULL ||
        right->kind != F2C_EXPR_CALL || name == NULL)
        return 0;
    if (strcmp(name, "reshape") != 0 && strcmp(name, "pack") != 0 && strcmp(name, "unpack") != 0 &&
        strcmp(name, "spread") != 0 && strcmp(name, "cshift") != 0 &&
        strcmp(name, "eoshift") != 0 && strcmp(name, "findloc") != 0)
        return 0;
    if (!supported_element_type(target)) {
        f2c_diagnostic(context, line, 1,
                       "%s result currently requires an intrinsic non-CHARACTER element type",
                       name);
        return 1;
    }
    if (strcmp(name, "reshape") == 0)
        return emit_reshape(context, unit, target, right, line, depth);
    if (strcmp(name, "pack") == 0)
        return emit_pack(context, unit, target, right, line, depth);
    if (strcmp(name, "unpack") == 0)
        return emit_unpack(context, unit, target, right, line, depth);
    if (strcmp(name, "spread") == 0)
        return emit_spread(context, unit, target, right, line, depth);
    if (strcmp(name, "cshift") == 0)
        return emit_shift(context, unit, target, right, line, depth, 0);
    if (strcmp(name, "eoshift") == 0)
        return emit_shift(context, unit, target, right, line, depth, 1);
    return emit_findloc(context, unit, target, right, line, depth);
}
