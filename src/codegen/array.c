#include "codegen/array/private.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void f2c_array_indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

char *f2c_array_emit_expression(Unit *unit, const F2cExpr *expression) {
    int supported = 0;
    char *result = f2c_emit_expression_ast(unit, expression, &supported);
    if (!supported) {
        free(result);
        return NULL;
    }
    return result;
}

static const F2cExpr *section_bound(const F2cExpr *section, size_t index) {
    if (section == NULL || section->kind != F2C_EXPR_ARRAY_SECTION || section->child_count != 3U ||
        section->children[index]->kind == F2C_EXPR_INVALID)
        return NULL;
    return section->children[index];
}

static char *dimension_bound(Unit *unit, Symbol *symbol, size_t dimension, int upper) {
    if (symbol == NULL || dimension >= symbol->rank)
        return NULL;
    return upper ? f2c_symbol_dimension_upper(unit, symbol, dimension)
                 : f2c_symbol_dimension_lower(unit, symbol, dimension);
}

static F2cExpr *clone_expression(Unit *unit, const F2cExpr *expression);

static F2cExpr *section_index_expression(Unit *unit, const F2cExpr *section, Symbol *symbol,
                                         size_t dimension, size_t ordinal) {
    const F2cExpr *lower_expression = section_bound(section, 0U);
    const F2cExpr *step_expression = section_bound(section, 2U);
    char *lower = lower_expression != NULL ? f2c_array_emit_expression(unit, lower_expression)
                                           : dimension_bound(unit, symbol, dimension, 0);
    char *step = step_expression != NULL ? f2c_array_emit_expression(unit, step_expression)
                                         : f2c_strdup("1");
    Buffer code = {0};
    F2cExpr *index;
    if (lower == NULL || step == NULL) {
        free(lower);
        free(step);
        return NULL;
    }
    f2c_buffer_printf(&code, "((%s) + f2c_section_%zu * (%s))", lower, ordinal, step);
    free(lower);
    free(step);
    index = (F2cExpr *)calloc(1U, sizeof(*index));
    if (index == NULL) {
        free(f2c_buffer_take(&code));
        return NULL;
    }
    index->kind = F2C_EXPR_NAME;
    index->type = TYPE_INTEGER;
    index->text = f2c_buffer_take(&code);
    if (index->text == NULL) {
        free(index);
        return NULL;
    }
    return index;
}

static F2cExpr *lowered_integer_expression(char *code) {
    F2cExpr *expression;
    if (code == NULL)
        return NULL;
    expression =
        f2c_expr_new(F2C_EXPR_NAME, TYPE_INTEGER, "f2c_vector_index", strlen("f2c_vector_index"));
    if (expression == NULL) {
        free(code);
        return NULL;
    }
    expression->lowered_c = code;
    expression->rank = 0U;
    return expression;
}

static F2cExpr *vector_index_expression(Unit *unit, const F2cExpr *selector, size_t ordinal) {
    Buffer code = {0};
    if (selector == NULL || selector->rank != 1U || selector->type != TYPE_INTEGER)
        return NULL;
    if (selector->kind == F2C_EXPR_NAME && selector->symbol != NULL &&
        selector->symbol->rank == 1U) {
        char *lower = dimension_bound(unit, selector->symbol, 0U, 0);
        char *index;
        char *reference;
        if (lower == NULL)
            return NULL;
        f2c_buffer_printf(&code, "((%s) + f2c_section_%zu)", lower, ordinal);
        free(lower);
        index = f2c_buffer_take(&code);
        if (index == NULL)
            return NULL;
        {
            char *indices[1] = {index};
            reference = f2c_emit_array_reference(unit, selector->symbol, indices, 1U);
        }
        free(index);
        return lowered_integer_expression(reference);
    }
    if (selector->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        char *constructor = f2c_array_emit_expression(unit, selector);
        if (constructor == NULL)
            return NULL;
        f2c_buffer_printf(&code, "((%s)[f2c_section_%zu])", constructor, ordinal);
        free(constructor);
        return lowered_integer_expression(f2c_buffer_take(&code));
    }
    return NULL;
}

static F2cExpr *clone_array_reference(Unit *unit, const F2cExpr *expression) {
    F2cExpr *clone = (F2cExpr *)calloc(1U, sizeof(*clone));
    size_t dimension;
    size_t ordinal = 0U;
    if (clone == NULL)
        return NULL;
    *clone = *expression;
    clone->text = expression->text != NULL ? f2c_strdup(expression->text) : NULL;
    clone->source = expression->source != NULL ? f2c_strdup(expression->source) : NULL;
    clone->lowered_c = expression->lowered_c != NULL ? f2c_strdup(expression->lowered_c) : NULL;
    clone->children = expression->child_count != 0U
                          ? (F2cExpr **)calloc(expression->child_count, sizeof(*clone->children))
                          : NULL;
    clone->child_count = 0U;
    clone->child_capacity = expression->child_count;
    if ((expression->text != NULL && clone->text == NULL) ||
        (expression->source != NULL && clone->source == NULL) ||
        (expression->lowered_c != NULL && clone->lowered_c == NULL) ||
        (expression->child_count != 0U && clone->children == NULL))
        goto failed;
    for (dimension = 0U; dimension < expression->child_count; ++dimension) {
        if (expression->children[dimension]->kind == F2C_EXPR_ARRAY_SECTION) {
            clone->children[dimension] = section_index_expression(
                unit, expression->children[dimension], expression->symbol, dimension, ordinal++);
        } else if (expression->children[dimension]->rank == 1U) {
            clone->children[dimension] =
                vector_index_expression(unit, expression->children[dimension], ordinal++);
        } else {
            clone->children[dimension] = clone_expression(unit, expression->children[dimension]);
        }
        if (clone->children[dimension] == NULL)
            goto failed;
        ++clone->child_count;
    }
    return clone;

failed:
    f2c_expr_free(clone);
    return NULL;
}

static F2cExpr *clone_expression(Unit *unit, const F2cExpr *expression) {
    F2cExpr *clone;
    size_t i;
    if (expression == NULL)
        return NULL;
    if (expression->kind == F2C_EXPR_ARRAY_REFERENCE)
        return clone_array_reference(unit, expression);
    clone = (F2cExpr *)calloc(1U, sizeof(*clone));
    if (clone == NULL)
        return NULL;
    *clone = *expression;
    clone->text = expression->text != NULL ? f2c_strdup(expression->text) : NULL;
    clone->source = expression->source != NULL ? f2c_strdup(expression->source) : NULL;
    clone->lowered_c = expression->lowered_c != NULL ? f2c_strdup(expression->lowered_c) : NULL;
    clone->children = expression->child_count != 0U
                          ? (F2cExpr **)calloc(expression->child_count, sizeof(*clone->children))
                          : NULL;
    clone->child_count = 0U;
    clone->child_capacity = expression->child_count;
    if ((expression->text != NULL && clone->text == NULL) ||
        (expression->source != NULL && clone->source == NULL) ||
        (expression->lowered_c != NULL && clone->lowered_c == NULL) ||
        (expression->child_count != 0U && clone->children == NULL))
        goto failed;
    for (i = 0U; i < expression->child_count; ++i) {
        clone->children[i] = clone_expression(unit, expression->children[i]);
        if (clone->children[i] == NULL)
            goto failed;
        ++clone->child_count;
    }
    return clone;

failed:
    f2c_expr_free(clone);
    return NULL;
}

static int section_extent(Unit *unit, const F2cExpr *section, Symbol *symbol, size_t dimension,
                          char **extent) {
    const F2cExpr *lower_expression = section_bound(section, 0U);
    const F2cExpr *upper_expression = section_bound(section, 1U);
    const F2cExpr *step_expression = section_bound(section, 2U);
    char *lower = lower_expression != NULL ? f2c_array_emit_expression(unit, lower_expression)
                                           : dimension_bound(unit, symbol, dimension, 0);
    char *upper = upper_expression != NULL ? f2c_array_emit_expression(unit, upper_expression)
                                           : dimension_bound(unit, symbol, dimension, 1);
    char *step = step_expression != NULL ? f2c_array_emit_expression(unit, step_expression)
                                         : f2c_strdup("1");
    Buffer result = {0};
    if (lower == NULL || upper == NULL || step == NULL) {
        free(lower);
        free(upper);
        free(step);
        return 0;
    }
    f2c_buffer_printf(&result, "(((%s) - (%s)) / (%s) + 1)", upper, lower, step);
    free(lower);
    free(upper);
    free(step);
    *extent = f2c_buffer_take(&result);
    return *extent != NULL;
}

static int vector_extent(Unit *unit, const F2cExpr *selector, char **extent) {
    Buffer result = {0};
    if (selector == NULL || selector->rank != 1U || selector->type != TYPE_INTEGER)
        return 0;
    if (selector->shape.rank == 1U && selector->shape.dimensions[0].extent_known) {
        f2c_buffer_printf(&result, "%llu",
                          (unsigned long long)selector->shape.dimensions[0].extent);
        *extent = f2c_buffer_take(&result);
        return *extent != NULL;
    }
    if (selector->kind == F2C_EXPR_NAME && selector->symbol != NULL &&
        selector->symbol->rank == 1U) {
        *extent = f2c_symbol_dimension_extent(unit, selector->symbol, 0U);
        return *extent != NULL;
    }
    return 0;
}

static F2cExpr *clone_whole_array_element(Unit *unit, const F2cExpr *expression,
                                          size_t section_count) {
    F2cExpr *element;
    size_t dimension;
    if (expression != NULL && expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR && section_count == 1U)
        return vector_index_expression(unit, expression, 0U);
    if (expression != NULL && expression->kind == F2C_EXPR_ARRAY_REFERENCE &&
        expression->rank == section_count)
        return clone_array_reference(unit, expression);
    if (expression == NULL || expression->kind != F2C_EXPR_NAME || expression->symbol == NULL ||
        expression->symbol->rank == 0U || expression->symbol->rank != section_count)
        return clone_expression(unit, expression);
    element = f2c_expr_new(F2C_EXPR_ARRAY_REFERENCE, expression->type, expression->text,
                           expression->text != NULL ? strlen(expression->text) : 0U);
    if (element == NULL)
        return NULL;
    element->symbol = expression->symbol;
    element->definable = expression->definable;
    for (dimension = 0U; dimension < expression->symbol->rank; ++dimension) {
        char *lower = dimension_bound(unit, expression->symbol, dimension, 0);
        Buffer index_text = {0};
        F2cExpr *index;
        if (lower == NULL) {
            f2c_expr_free(element);
            return NULL;
        }
        f2c_buffer_printf(&index_text, "((%s) + f2c_section_%zu)", lower, dimension);
        free(lower);
        index = f2c_expr_new(F2C_EXPR_NAME, TYPE_INTEGER, index_text.data, index_text.length);
        free(f2c_buffer_take(&index_text));
        if (index == NULL || !f2c_expr_push(element, index)) {
            f2c_expr_free(index);
            f2c_expr_free(element);
            return NULL;
        }
    }
    element->rank = 0U;
    return element;
}

int f2c_emit_array_section_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                      const F2cExpr *right, int depth) {
    F2cExpr *lowered_left;
    F2cExpr *lowered_right;
    char *left_code;
    char *right_code;
    char *character_length = NULL;
    char *extents[F2C_MAX_RANK] = {0};
    size_t dimension;
    size_t section_count = 0U;
    const int character_assignment = left != NULL && left->symbol != NULL &&
                                     left->symbol->type == TYPE_CHARACTER && right != NULL &&
                                     right->type == TYPE_CHARACTER;
    const int scalar_character_source = character_assignment && right->rank == 0U;
    int emitted_depth = depth;
    int result = 0;
    if (left == NULL || left->kind != F2C_EXPR_ARRAY_REFERENCE || left->symbol == NULL)
        return 0;
    for (dimension = 0U; dimension < left->child_count; ++dimension) {
        if (left->children[dimension]->kind != F2C_EXPR_ARRAY_SECTION &&
            left->children[dimension]->rank == 0U)
            continue;
        if (section_count == F2C_MAX_RANK ||
            !(left->children[dimension]->kind == F2C_EXPR_ARRAY_SECTION
                  ? section_extent(unit, left->children[dimension], left->symbol, dimension,
                                   &extents[section_count])
                  : vector_extent(unit, left->children[dimension], &extents[section_count])))
            goto cleanup;
        ++section_count;
    }
    if (section_count == 0U)
        goto cleanup;
    lowered_left = clone_array_reference(unit, left);
    lowered_right = clone_whole_array_element(unit, right, section_count);
    if (lowered_left == NULL || lowered_right == NULL) {
        f2c_expr_free(lowered_left);
        f2c_expr_free(lowered_right);
        goto cleanup;
    }
    left_code = f2c_array_emit_expression(unit, lowered_left);
    right_code = f2c_array_emit_expression(unit, lowered_right);
    if (left_code == NULL || right_code == NULL) {
        free(left_code);
        free(right_code);
        goto lowered_cleanup;
    }
    if (character_assignment) {
        character_length = f2c_character_length_expression(unit, left);
        if (character_length == NULL) {
            free(left_code);
            free(right_code);
            goto lowered_cleanup;
        }
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "{\n");
    ++emitted_depth;
    for (dimension = 0U; dimension < section_count; ++dimension) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "int32_t f2c_extent_%zu = (int32_t)(%s);\n", dimension,
                          extents[dimension]);
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "size_t f2c_section_count = 1U;\n");
    for (dimension = 0U; dimension < section_count; ++dimension) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "size_t f2c_section_extent_%zu = f2c_extent_%zu > 0 ? "
                          "(size_t)f2c_extent_%zu : 0U;\n",
                          dimension, dimension, dimension);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_section_extent_%zu != 0U && f2c_section_count > "
                          "SIZE_MAX / f2c_section_extent_%zu) abort();\n",
                          dimension, dimension);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "f2c_section_count *= f2c_section_extent_%zu;\n",
                          dimension);
    }
    if (character_assignment) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "const size_t f2c_character_length = (size_t)(%s);\n",
                          character_length);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "if (f2c_character_length != 0U && f2c_section_count > "
                                            "SIZE_MAX / f2c_character_length) abort();\n");
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "const size_t f2c_section_bytes = f2c_section_count * "
                                            "f2c_character_length;\n");
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output,
                          "char *f2c_section_values = f2c_section_count == 0U ? NULL : "
                          "(char *)malloc(f2c_section_bytes == 0U ? 1U : f2c_section_bytes);\n");
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output,
                          "if (f2c_section_count != 0U && f2c_section_values == NULL) abort();\n");
        if (scalar_character_source) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_append(&context->output,
                              "char *f2c_section_scalar = (char *)malloc("
                              "f2c_character_length == 0U ? 1U : f2c_character_length);\n");
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_append(&context->output, "if (f2c_section_scalar == NULL) abort();\n");
            if (!f2c_emit_character_storage_assignment(context, unit, "f2c_section_scalar",
                                                       "f2c_character_length", lowered_right,
                                                       right_code, emitted_depth)) {
                free(left_code);
                free(right_code);
                goto lowered_cleanup;
            }
        }
    } else {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "%s *f2c_section_values = f2c_section_count == 0U ? NULL : "
                          "(%s *)malloc(f2c_section_count * sizeof(*f2c_section_values));\n",
                          f2c_symbol_c_type(left->symbol), f2c_symbol_c_type(left->symbol));
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output,
                          "if (f2c_section_count != 0U && f2c_section_values == NULL) abort();\n");
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "size_t f2c_section_linear = 0U;\n");
    for (dimension = 0U; dimension < section_count; ++dimension) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "for (int32_t f2c_section_%zu = 0; f2c_section_%zu < f2c_extent_%zu; "
                          "++f2c_section_%zu) {\n",
                          dimension, dimension, dimension, dimension);
        ++emitted_depth;
    }
    if (character_assignment) {
        if (scalar_character_source) {
            f2c_array_indent(&context->output, emitted_depth);
            f2c_buffer_append(&context->output, "if (f2c_character_length != 0U) "
                                                "memmove(f2c_section_values + f2c_section_linear * "
                                                "f2c_character_length, f2c_section_scalar, "
                                                "f2c_character_length);\n");
        } else if (!f2c_emit_character_storage_assignment(
                       context, unit,
                       "f2c_section_values + f2c_section_linear * f2c_character_length",
                       "f2c_character_length", lowered_right, right_code, emitted_depth)) {
            free(left_code);
            free(right_code);
            goto lowered_cleanup;
        }
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_section_linear;\n");
    } else {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "f2c_section_values[f2c_section_linear++] = %s;\n",
                          right_code);
    }
    while (dimension != 0U) {
        --dimension;
        --emitted_depth;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "f2c_section_linear = 0U;\n");
    for (dimension = 0U; dimension < section_count; ++dimension) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "for (int32_t f2c_section_%zu = 0; f2c_section_%zu < f2c_extent_%zu; "
                          "++f2c_section_%zu) {\n",
                          dimension, dimension, dimension, dimension);
        ++emitted_depth;
    }
    f2c_array_indent(&context->output, emitted_depth);
    if (character_assignment) {
        f2c_buffer_printf(&context->output,
                          "if (f2c_character_length != 0U) memmove(&%s, "
                          "f2c_section_values + f2c_section_linear * "
                          "f2c_character_length, f2c_character_length);\n",
                          left_code);
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_section_linear;\n");
    } else {
        f2c_buffer_printf(&context->output, "%s = f2c_section_values[f2c_section_linear++];\n",
                          left_code);
    }
    while (dimension != 0U) {
        --dimension;
        --emitted_depth;
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "free(f2c_section_values);\n");
    if (scalar_character_source) {
        f2c_array_indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "free(f2c_section_scalar);\n");
    }
    --emitted_depth;
    f2c_array_indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "}\n");
    free(left_code);
    free(right_code);
    result = 1;

lowered_cleanup:
    free(character_length);
    f2c_expr_free(lowered_left);
    f2c_expr_free(lowered_right);
cleanup:
    for (dimension = 0U; dimension < F2C_MAX_RANK; ++dimension)
        free(extents[dimension]);
    return result;
}

static char *rank2_section_bound(Unit *unit, const F2cExpr *section, const Symbol *symbol,
                                 size_t dimension, size_t child) {
    int supported = 1;
    if (section->children[child]->kind == F2C_EXPR_INVALID)
        return child == 0U ? f2c_symbol_dimension_lower(unit, symbol, dimension)
                           : f2c_symbol_dimension_upper(unit, symbol, dimension);
    return f2c_emit_expression_ast(unit, section->children[child], &supported);
}

int f2c_emit_rank2_section_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                      const F2cExpr *right, int depth) {
    Symbol *left_symbol = left != NULL ? left->symbol : NULL;
    Symbol *right_symbol = right != NULL ? right->symbol : NULL;
    char *lower0 = NULL;
    char *upper0 = NULL;
    char *lower1 = NULL;
    char *upper1 = NULL;
    char *source_lower0 = NULL;
    char *source_upper0 = NULL;
    char *source_lower1 = NULL;
    char *left_lower0 = NULL;
    char *left_upper0 = NULL;
    char *left_lower1 = NULL;
    char *left_upper1 = NULL;
    int emitted = 0;
    if (left == NULL || left->kind != F2C_EXPR_NAME || left_symbol == NULL ||
        left_symbol->rank != 2U || right == NULL || right->kind != F2C_EXPR_ARRAY_REFERENCE ||
        right_symbol == NULL || right_symbol->rank != 2U || right->child_count != 2U ||
        right->children[0]->kind != F2C_EXPR_ARRAY_SECTION ||
        right->children[1]->kind != F2C_EXPR_ARRAY_SECTION ||
        right->children[0]->child_count != 3U || right->children[1]->child_count != 3U ||
        right->children[0]->children[2]->kind != F2C_EXPR_INVALID ||
        right->children[1]->children[2]->kind != F2C_EXPR_INVALID)
        return 0;
    lower0 = rank2_section_bound(unit, right->children[0], right_symbol, 0U, 0U);
    upper0 = rank2_section_bound(unit, right->children[0], right_symbol, 0U, 1U);
    lower1 = rank2_section_bound(unit, right->children[1], right_symbol, 1U, 0U);
    upper1 = rank2_section_bound(unit, right->children[1], right_symbol, 1U, 1U);
    source_lower0 = f2c_symbol_dimension_lower(unit, right_symbol, 0U);
    source_upper0 = f2c_symbol_dimension_upper(unit, right_symbol, 0U);
    source_lower1 = f2c_symbol_dimension_lower(unit, right_symbol, 1U);
    left_lower0 = f2c_symbol_dimension_lower(unit, left_symbol, 0U);
    left_upper0 = f2c_symbol_dimension_upper(unit, left_symbol, 0U);
    left_lower1 = f2c_symbol_dimension_lower(unit, left_symbol, 1U);
    left_upper1 = f2c_symbol_dimension_upper(unit, left_symbol, 1U);
    if (lower0 == NULL || upper0 == NULL || lower1 == NULL || upper1 == NULL ||
        source_lower0 == NULL || source_upper0 == NULL || source_lower1 == NULL ||
        left_lower0 == NULL || left_upper0 == NULL || left_lower1 == NULL || left_upper1 == NULL)
        goto cleanup;
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (((%s) - (%s)) != ((%s) - (%s)) || "
                      "((%s) - (%s)) != ((%s) - (%s))) abort();\n",
                      left_upper0, left_lower0, upper0, lower0, left_upper1, left_lower1, upper1,
                      lower1);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "int32_t f2c_row, f2c_column;\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (f2c_column = 0; f2c_column <= (%s) - (%s); ++f2c_column) {\n", upper1,
                      lower1);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "for (f2c_row = 0; f2c_row <= (%s) - (%s); ++f2c_row) {\n",
                      upper0, lower0);
    f2c_array_indent(&context->output, depth + 3);
    f2c_buffer_printf(&context->output,
                      "%s[f2c_row + ((%s) - (%s) + 1) * f2c_column] = "
                      "%s[((%s) + f2c_row - (%s)) + ((%s) - (%s) + 1) * "
                      "((%s) + f2c_column - (%s))];\n",
                      f2c_symbol_c_name(unit, left_symbol), left_upper0, left_lower0,
                      f2c_symbol_c_name(unit, right_symbol), lower0, source_lower0, source_upper0,
                      source_lower0, lower1, source_lower1);
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    emitted = 1;
cleanup:
    free(left_upper1);
    free(left_lower1);
    free(left_upper0);
    free(left_lower0);
    free(source_lower1);
    free(source_upper0);
    free(source_lower0);
    free(upper1);
    free(lower1);
    free(upper0);
    free(lower0);
    return emitted;
}

char *f2c_symbol_element_count(Unit *unit, Symbol *symbol) {
    Buffer count = {0};
    size_t d;
    for (d = 0U; d < symbol->rank; ++d) {
        char *extent = f2c_symbol_dimension_extent(unit, symbol, d);
        if (extent == NULL) {
            free(f2c_buffer_take(&count));
            return NULL;
        }
        f2c_buffer_printf(&count, "%s(size_t)(%s)", d == 0U ? "" : " * ", extent);
        free(extent);
    }
    return f2c_buffer_take(&count);
}

static const F2cExpr *transform_argument(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static int emit_rank2_transform_assignment(Context *context, Unit *unit, Symbol *target,
                                           const F2cExpr *right, size_t line, int depth) {
    const F2cExpr *left_argument;
    const F2cExpr *right_argument = NULL;
    Symbol *left_source;
    Symbol *right_source = NULL;
    char *target_rows = NULL;
    char *target_columns = NULL;
    char *left_rows = NULL;
    char *left_columns = NULL;
    char *right_rows = NULL;
    char *right_columns = NULL;
    const char *target_name;
    const char *left_name;
    const char *right_name = NULL;
    const char *c_type;
    int matmul;
    if (right == NULL || right->kind != F2C_EXPR_CALL || right->text == NULL || target == NULL ||
        target->rank != 2U ||
        (strcmp(right->text, "transpose") != 0 && strcmp(right->text, "matmul") != 0))
        return 0;
    matmul = strcmp(right->text, "matmul") == 0;
    if (right->child_count != (matmul ? 2U : 1U))
        goto unsupported;
    left_argument = transform_argument(right->children[0]);
    if (matmul)
        right_argument = transform_argument(right->children[1]);
    left_source = left_argument != NULL && left_argument->kind == F2C_EXPR_NAME
                      ? left_argument->symbol
                      : NULL;
    right_source = right_argument != NULL && right_argument->kind == F2C_EXPR_NAME
                       ? right_argument->symbol
                       : NULL;
    if (left_source == NULL || left_source->rank != 2U ||
        (matmul && (right_source == NULL || right_source->rank != 2U)))
        goto unsupported;
    if (!matmul && (left_source->type != target->type || left_source->kind != target->kind))
        goto incompatible;
    if (matmul &&
        ((!f2c_type_is_numeric(left_source->type) && left_source->type != TYPE_LOGICAL) ||
         left_source->type != right_source->type || left_source->kind != right_source->kind ||
         target->type != left_source->type || target->kind != left_source->kind))
        goto incompatible;
    if (target->allocatable || target->pointer) {
        f2c_diagnostic(context, line, 1,
                       "%s assignment currently requires an allocated rank-two target with "
                       "explicit shape",
                       matmul ? "MATMUL" : "TRANSPOSE");
        return 1;
    }
    target_rows = f2c_symbol_dimension_extent(unit, target, 0U);
    target_columns = f2c_symbol_dimension_extent(unit, target, 1U);
    left_rows = f2c_symbol_dimension_extent(unit, left_source, 0U);
    left_columns = f2c_symbol_dimension_extent(unit, left_source, 1U);
    if (matmul) {
        right_rows = f2c_symbol_dimension_extent(unit, right_source, 0U);
        right_columns = f2c_symbol_dimension_extent(unit, right_source, 1U);
    }
    if (target_rows == NULL || target_columns == NULL || left_rows == NULL ||
        left_columns == NULL || (matmul && (right_rows == NULL || right_columns == NULL)))
        goto unsupported;
    target_name = f2c_symbol_c_name(unit, target);
    left_name = f2c_symbol_c_name(unit, left_source);
    if (matmul)
        right_name = f2c_symbol_c_name(unit, right_source);
    c_type = f2c_symbol_c_type(target);
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_rows = (size_t)(%s);\n",
                      target_rows);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_columns = (size_t)(%s);\n",
                      target_columns);
    f2c_array_indent(&context->output, depth + 1);
    if (matmul) {
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_inner = (size_t)(%s);\n",
                          left_columns);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if ((size_t)(%s) != f2c_transform_rows || (size_t)(%s) != "
                          "f2c_transform_inner || (size_t)(%s) != f2c_transform_columns) "
                          "abort();\n",
                          left_rows, right_rows, right_columns);
    } else {
        f2c_buffer_printf(&context->output,
                          "if ((size_t)(%s) != f2c_transform_columns || (size_t)(%s) != "
                          "f2c_transform_rows) abort();\n",
                          left_rows, left_columns);
    }
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_transform_columns != 0U && f2c_transform_rows > "
                                        "SIZE_MAX / f2c_transform_columns) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "const size_t f2c_transform_count = f2c_transform_rows * "
                                        "f2c_transform_columns;\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "%s *f2c_transform_values = f2c_transform_count == 0U ? NULL : "
                      "(%s *)malloc(f2c_transform_count * sizeof(*f2c_transform_values));\n",
                      c_type, c_type);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_transform_count != 0U && f2c_transform_values == NULL) abort();\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "for (size_t f2c_transform_column = 0U; f2c_transform_column < "
                      "f2c_transform_columns; ++f2c_transform_column) {\n");
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "for (size_t f2c_transform_row = 0U; f2c_transform_row < "
                                        "f2c_transform_rows; ++f2c_transform_row) {\n");
    if (matmul) {
        f2c_array_indent(&context->output, depth + 3);
        f2c_buffer_printf(&context->output, "%s f2c_transform_value = (%s)0;\n", c_type, c_type);
        f2c_array_indent(&context->output, depth + 3);
        f2c_buffer_append(&context->output,
                          "for (size_t f2c_transform_index = 0U; f2c_transform_index < "
                          "f2c_transform_inner; ++f2c_transform_index)\n");
        f2c_array_indent(&context->output, depth + 4);
        if (target->type == TYPE_LOGICAL) {
            f2c_buffer_printf(&context->output,
                              "f2c_transform_value = (int32_t)(f2c_transform_value || "
                              "(%s[f2c_transform_row + f2c_transform_rows * "
                              "f2c_transform_index] && %s[f2c_transform_index + "
                              "f2c_transform_inner * f2c_transform_column]));\n",
                              left_name, right_name);
        } else {
            f2c_buffer_printf(&context->output,
                              "f2c_transform_value += %s[f2c_transform_row + "
                              "f2c_transform_rows * f2c_transform_index] * "
                              "%s[f2c_transform_index + f2c_transform_inner * "
                              "f2c_transform_column];\n",
                              left_name, right_name);
        }
        f2c_array_indent(&context->output, depth + 3);
        f2c_buffer_append(&context->output,
                          "f2c_transform_values[f2c_transform_row + f2c_transform_rows * "
                          "f2c_transform_column] = f2c_transform_value;\n");
    } else {
        f2c_array_indent(&context->output, depth + 3);
        f2c_buffer_printf(&context->output,
                          "f2c_transform_values[f2c_transform_row + f2c_transform_rows * "
                          "f2c_transform_column] = %s[f2c_transform_column + "
                          "f2c_transform_columns * f2c_transform_row];\n",
                          left_name);
    }
    f2c_array_indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_transform_count != 0U) memmove(%s, f2c_transform_values, "
                      "f2c_transform_count * sizeof(*f2c_transform_values));\n",
                      target_name);
    f2c_array_indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_transform_values);\n");
    f2c_array_indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(target_rows);
    free(target_columns);
    free(left_rows);
    free(left_columns);
    free(right_rows);
    free(right_columns);
    return 1;

incompatible:
    f2c_diagnostic(context, line, 1,
                   "%s operands and target must have matching intrinsic type and kind",
                   matmul ? "MATMUL" : "TRANSPOSE");
    return 1;
unsupported:
    free(target_rows);
    free(target_columns);
    free(left_rows);
    free(left_columns);
    free(right_rows);
    free(right_columns);
    f2c_diagnostic(context, line, 1, "%s currently requires whole named rank-two arrays",
                   right != NULL && right->text != NULL ? right->text : "array transformation");
    return 1;
}

int f2c_emit_whole_array_assignment(Context *context, Unit *unit, const F2cExpr *left,
                                    const F2cExpr *right, size_t line, int depth) {
    Symbol *left_symbol = left != NULL ? left->symbol : NULL;
    Symbol *right_symbol = right != NULL && right->kind == F2C_EXPR_NAME ? right->symbol : NULL;
    char *element_count;
    const int has_constructor = right != NULL && right->kind == F2C_EXPR_ARRAY_CONSTRUCTOR;
    if (left == NULL || left->kind != F2C_EXPR_NAME || left_symbol == NULL ||
        left_symbol->rank == 0U)
        return 0;
    if (f2c_emit_transform_assignment(context, unit, left, right, line, depth))
        return 1;
    if (emit_rank2_transform_assignment(context, unit, left_symbol, right, line, depth))
        return 1;
    if (left_symbol->allocatable && right != NULL && right->kind == F2C_EXPR_CALL &&
        right->symbol != NULL && right->symbol->external_result_allocatable) {
        char *result = f2c_array_emit_expression(unit, right);
        const char *name = f2c_symbol_c_name(unit, left_symbol);
        size_t dimension;
        if (result == NULL || right->symbol->external_result_rank != left_symbol->rank ||
            right->type != left_symbol->type || right->type_kind != left_symbol->kind) {
            free(result);
            f2c_diagnostic(context, line, 1,
                           "allocatable function-result assignment requires matching type, kind, "
                           "and rank");
            return 1;
        }
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_descriptor f2c_function_result = %s;\n", result);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_function_result.rank != %zuU || "
                          "f2c_function_result.element_size != sizeof(%s)) abort();\n",
                          left_symbol->rank, f2c_symbol_c_type(left_symbol));
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "free(%s);\n", name);
        f2c_array_indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = (%s *)f2c_function_result.data;\n", name,
                          f2c_symbol_c_type(left_symbol));
        if (left_symbol->deferred_character) {
            f2c_array_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "f2c_char_len_%s = f2c_function_result.character_length;\n", name);
        }
        for (dimension = 0U; dimension < left_symbol->rank; ++dimension) {
            f2c_array_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (f2c_function_result.lower[%zu] < INT32_MIN || "
                              "f2c_function_result.lower[%zu] > INT32_MAX || "
                              "f2c_function_result.extent[%zu] < 0 || "
                              "f2c_function_result.extent[%zu] > INT32_MAX) abort();\n",
                              dimension, dimension, dimension, dimension);
            f2c_array_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "%s_lower_%zu = (int32_t)f2c_function_result.lower[%zu];\n", name,
                              dimension + 1U, dimension);
            f2c_array_indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "%s_extent_%zu = (int32_t)f2c_function_result.extent[%zu];\n", name,
                              dimension + 1U, dimension);
        }
        f2c_array_indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(result);
        return 1;
    }
    if (f2c_emit_allocatable_array_assignment(context, unit, left, right, depth))
        return 1;
    if (left_symbol->allocatable && has_constructor) {
        const int emitted = left_symbol->type == TYPE_CHARACTER
                                ? f2c_array_emit_allocatable_character_constructor(
                                      context, unit, left_symbol, right, depth)
                                : f2c_array_emit_allocatable_numeric_constructor(
                                      context, unit, left_symbol, right, depth);
        if (!emitted)
            f2c_diagnostic(context, line, 1,
                           "allocatable array-constructor assignment requires a supported "
                           "rank-one intrinsic target and compatible element values");
        return 1;
    }
    if (left_symbol->allocatable || left_symbol->pointer) {
        f2c_array_indent(&context->output, depth);
        f2c_buffer_printf(&context->output, "if (%s == NULL) abort();\n",
                          f2c_symbol_c_name(unit, left_symbol));
    }
    element_count = f2c_symbol_element_count(unit, left_symbol);
    if (element_count == NULL) {
        f2c_diagnostic(context, line, 1, "whole-array assignment requires a known target shape");
        return 1;
    }
    if (has_constructor) {
        const int emitted =
            left_symbol->type == TYPE_CHARACTER
                ? f2c_array_emit_whole_character_assignment(context, unit, left_symbol, right,
                                                            right_symbol, element_count, depth)
                : f2c_array_emit_numeric_constructor(context, unit, left_symbol, right,
                                                     element_count, depth);
        if (!emitted)
            f2c_diagnostic(context, line, 1,
                           "array constructor contains an unsupported value or incompatible "
                           "element type");
        goto cleanup;
    }
    if (left_symbol->type == TYPE_CHARACTER &&
        f2c_array_emit_whole_character_assignment(context, unit, left_symbol, right, right_symbol,
                                                  element_count, depth))
        goto cleanup;
    f2c_array_indent(&context->output, depth);
    if (right_symbol != NULL && right_symbol->rank != 0U &&
        right_symbol->type == left_symbol->type) {
        char *right_count = f2c_symbol_element_count(unit, right_symbol);
        if (left_symbol->type == TYPE_DERIVED && left_symbol->derived_type != NULL &&
            left_symbol->derived_type == right_symbol->derived_type) {
            f2c_buffer_printf(
                &context->output,
                "{ const size_t f2c_whole_count = (size_t)(%s); "
                "if ((size_t)(%s) != f2c_whole_count) abort(); "
                "%s *f2c_whole_temporary = (%s *)calloc("
                "f2c_whole_count == 0U ? 1U : f2c_whole_count, "
                "sizeof(*f2c_whole_temporary)); "
                "if (f2c_whole_temporary == NULL) abort(); "
                "for (size_t i = 0U; i < f2c_whole_count; ++i) "
                "f2c_copy_%s(&f2c_whole_temporary[i], &%s[i]); "
                "f2c_destroy_array_%s(%s, f2c_whole_count, %zuU); "
                "if (f2c_whole_count != 0U) memmove(%s, f2c_whole_temporary, "
                "f2c_whole_count * sizeof(*%s)); free(f2c_whole_temporary); }\n",
                element_count, right_count, f2c_symbol_c_type(left_symbol),
                f2c_symbol_c_type(left_symbol), left_symbol->derived_type->c_name,
                f2c_symbol_c_name(unit, right_symbol), left_symbol->derived_type->c_name,
                f2c_symbol_c_name(unit, left_symbol), left_symbol->rank,
                f2c_symbol_c_name(unit, left_symbol), f2c_symbol_c_name(unit, left_symbol));
        } else {
            f2c_buffer_printf(&context->output,
                              "{ const size_t f2c_whole_count = (size_t)(%s); "
                              "if ((size_t)(%s) != f2c_whole_count) abort(); "
                              "if (f2c_whole_count != 0U) memmove(%s, %s, "
                              "f2c_whole_count * sizeof(*%s)); }\n",
                              element_count, right_count, f2c_symbol_c_name(unit, left_symbol),
                              f2c_symbol_c_name(unit, right_symbol),
                              f2c_symbol_c_name(unit, left_symbol));
        }
        free(right_count);
    } else {
        char *value = f2c_array_emit_expression(unit, right);
        if (value == NULL) {
            f2c_diagnostic(context, line, 1,
                           "whole-array assignment has an unsupported right-hand expression");
            goto cleanup;
        }
        f2c_buffer_printf(&context->output,
                          "{ const %s f2c_whole_scalar = (%s)(%s); "
                          "size_t f2c_fill_index; for (f2c_fill_index = 0; ",
                          f2c_symbol_c_type(left_symbol), f2c_symbol_c_type(left_symbol), value);
        f2c_buffer_printf(&context->output,
                          "f2c_fill_index < %s; ++f2c_fill_index) %s[f2c_fill_index] = "
                          "f2c_whole_scalar; }\n",
                          element_count, f2c_symbol_c_name(unit, left_symbol));
        free(value);
    }
cleanup:
    free(element_count);
    return 1;
}
