#include "ast/internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void indent(Buffer *output, int depth) {
    int i;
    for (i = 0; i < depth; ++i)
        f2c_buffer_append(output, "    ");
}

static char *emit_expression(Unit *unit, const F2cExpr *expression) {
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
    char *lower = lower_expression != NULL ? emit_expression(unit, lower_expression)
                                           : dimension_bound(unit, symbol, dimension, 0);
    char *step = step_expression != NULL ? emit_expression(unit, step_expression) : f2c_strdup("1");
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
        char *constructor = emit_expression(unit, selector);
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
    char *lower = lower_expression != NULL ? emit_expression(unit, lower_expression)
                                           : dimension_bound(unit, symbol, dimension, 0);
    char *upper = upper_expression != NULL ? emit_expression(unit, upper_expression)
                                           : dimension_bound(unit, symbol, dimension, 1);
    char *step = step_expression != NULL ? emit_expression(unit, step_expression) : f2c_strdup("1");
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
    left_code = emit_expression(unit, lowered_left);
    right_code = emit_expression(unit, lowered_right);
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
    indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "{\n");
    ++emitted_depth;
    for (dimension = 0U; dimension < section_count; ++dimension) {
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "int32_t f2c_extent_%zu = (int32_t)(%s);\n", dimension,
                          extents[dimension]);
    }
    indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "size_t f2c_section_count = 1U;\n");
    for (dimension = 0U; dimension < section_count; ++dimension) {
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "size_t f2c_section_extent_%zu = f2c_extent_%zu > 0 ? "
                          "(size_t)f2c_extent_%zu : 0U;\n",
                          dimension, dimension, dimension);
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "if (f2c_section_extent_%zu != 0U && f2c_section_count > "
                          "SIZE_MAX / f2c_section_extent_%zu) abort();\n",
                          dimension, dimension);
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "f2c_section_count *= f2c_section_extent_%zu;\n",
                          dimension);
    }
    if (character_assignment) {
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "const size_t f2c_character_length = (size_t)(%s);\n",
                          character_length);
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "if (f2c_character_length != 0U && f2c_section_count > "
                                            "SIZE_MAX / f2c_character_length) abort();\n");
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "const size_t f2c_section_bytes = f2c_section_count * "
                                            "f2c_character_length;\n");
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output,
                          "char *f2c_section_values = f2c_section_count == 0U ? NULL : "
                          "(char *)malloc(f2c_section_bytes == 0U ? 1U : f2c_section_bytes);\n");
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output,
                          "if (f2c_section_count != 0U && f2c_section_values == NULL) abort();\n");
        if (scalar_character_source) {
            indent(&context->output, emitted_depth);
            f2c_buffer_append(&context->output,
                              "char *f2c_section_scalar = (char *)malloc("
                              "f2c_character_length == 0U ? 1U : f2c_character_length);\n");
            indent(&context->output, emitted_depth);
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
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "%s *f2c_section_values = f2c_section_count == 0U ? NULL : "
                          "(%s *)malloc(f2c_section_count * sizeof(*f2c_section_values));\n",
                          f2c_symbol_c_type(left->symbol), f2c_symbol_c_type(left->symbol));
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output,
                          "if (f2c_section_count != 0U && f2c_section_values == NULL) abort();\n");
    }
    indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "size_t f2c_section_linear = 0U;\n");
    for (dimension = 0U; dimension < section_count; ++dimension) {
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "for (int32_t f2c_section_%zu = 0; f2c_section_%zu < f2c_extent_%zu; "
                          "++f2c_section_%zu) {\n",
                          dimension, dimension, dimension, dimension);
        ++emitted_depth;
    }
    if (character_assignment) {
        if (scalar_character_source) {
            indent(&context->output, emitted_depth);
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
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_section_linear;\n");
    } else {
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output, "f2c_section_values[f2c_section_linear++] = %s;\n",
                          right_code);
    }
    while (dimension != 0U) {
        --dimension;
        --emitted_depth;
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "f2c_section_linear = 0U;\n");
    for (dimension = 0U; dimension < section_count; ++dimension) {
        indent(&context->output, emitted_depth);
        f2c_buffer_printf(&context->output,
                          "for (int32_t f2c_section_%zu = 0; f2c_section_%zu < f2c_extent_%zu; "
                          "++f2c_section_%zu) {\n",
                          dimension, dimension, dimension, dimension);
        ++emitted_depth;
    }
    indent(&context->output, emitted_depth);
    if (character_assignment) {
        f2c_buffer_printf(&context->output,
                          "if (f2c_character_length != 0U) memmove(&%s, "
                          "f2c_section_values + f2c_section_linear * "
                          "f2c_character_length, f2c_character_length);\n",
                          left_code);
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "++f2c_section_linear;\n");
    } else {
        f2c_buffer_printf(&context->output, "%s = f2c_section_values[f2c_section_linear++];\n",
                          left_code);
    }
    while (dimension != 0U) {
        --dimension;
        --emitted_depth;
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "}\n");
    }
    indent(&context->output, emitted_depth);
    f2c_buffer_append(&context->output, "free(f2c_section_values);\n");
    if (scalar_character_source) {
        indent(&context->output, emitted_depth);
        f2c_buffer_append(&context->output, "free(f2c_section_scalar);\n");
    }
    --emitted_depth;
    indent(&context->output, emitted_depth);
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

int f2c_emit_rank2_section_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                      const char *left_text, const char *right_text, int depth) {
    size_t consumed = 0U;
    char *right_name;
    Symbol *right_symbol;
    const char *open;
    char **ranges;
    size_t count = 0U;
    char *colon0;
    char *colon1;
    char *lower0;
    char *upper0;
    char *lower1;
    char *upper1;
    char *source_lower0;
    char *source_upper0;
    char *source_lower1;
    char *left_lower0;
    char *left_upper0;
    if (left_symbol == NULL || left_symbol->rank != 2U || strchr(left_text, '(') != NULL)
        return 0;
    right_name = f2c_identifier(f2c_trim((char *)right_text), &consumed);
    right_symbol = right_name != NULL ? f2c_find_symbol(unit, right_name) : NULL;
    open = strchr(right_text, '(');
    if (right_symbol == NULL || right_symbol->rank != 2U || open == NULL) {
        free(right_name);
        return 0;
    }
    ranges = f2c_split_arguments(open, &count);
    if (ranges == NULL || count != 2U) {
        while (count != 0U)
            free(ranges[--count]);
        free(ranges);
        free(right_name);
        return 0;
    }
    colon0 = strchr(ranges[0], ':');
    colon1 = strchr(ranges[1], ':');
    if (colon0 == NULL || colon1 == NULL) {
        free(ranges[0]);
        free(ranges[1]);
        free(ranges);
        free(right_name);
        return 0;
    }
    *colon0 = '\0';
    *colon1 = '\0';
    lower0 = f2c_translate_expression(unit, f2c_trim(ranges[0]));
    upper0 = f2c_translate_expression(unit, f2c_trim(colon0 + 1));
    lower1 = f2c_translate_expression(unit, f2c_trim(ranges[1]));
    upper1 = f2c_translate_expression(unit, f2c_trim(colon1 + 1));
    source_lower0 = f2c_symbol_dimension_lower(unit, right_symbol, 0U);
    source_upper0 = f2c_symbol_dimension_upper(unit, right_symbol, 0U);
    source_lower1 = f2c_symbol_dimension_lower(unit, right_symbol, 1U);
    left_lower0 = f2c_symbol_dimension_lower(unit, left_symbol, 0U);
    left_upper0 = f2c_symbol_dimension_upper(unit, left_symbol, 0U);
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "int32_t f2c_row, f2c_column;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "for (f2c_column = 0; f2c_column <= (%s) - (%s); ++f2c_column) {\n", upper1,
                      lower1);
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "for (f2c_row = 0; f2c_row <= (%s) - (%s); ++f2c_row) {\n",
                      upper0, lower0);
    indent(&context->output, depth + 3);
    f2c_buffer_printf(&context->output,
                      "%s[f2c_row + ((%s) - (%s) + 1) * f2c_column] = "
                      "%s[((%s) + f2c_row - (%s)) + ((%s) - (%s) + 1) * "
                      "((%s) + f2c_column - (%s))];\n",
                      f2c_symbol_c_name(unit, left_symbol), left_upper0, left_lower0,
                      f2c_symbol_c_name(unit, right_symbol), lower0, source_lower0, source_upper0,
                      source_lower0, lower1, source_lower1);
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(left_upper0);
    free(left_lower0);
    free(source_lower1);
    free(source_upper0);
    free(source_lower0);
    free(upper1);
    free(lower1);
    free(upper0);
    free(lower0);
    free(ranges[0]);
    free(ranges[1]);
    free(ranges);
    free(right_name);
    return 1;
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

typedef struct ConstructorSubstitution {
    const Symbol *symbol;
    const char *name;
    const char *replacement;
} ConstructorSubstitution;

typedef struct ConstructorEmitter {
    Context *context;
    Unit *unit;
    Symbol *target;
    const char *storage;
    const char *count;
    const char *index;
    const char *capacity;
    const char *character_length;
    const char *character_length_set;
    ConstructorSubstitution substitutions[64];
    size_t substitution_count;
    size_t next_temporary;
    int character;
    int dynamic;
    int infer_character_length;
} ConstructorEmitter;

static void emit_constructor_capacity(ConstructorEmitter *emitter, int depth) {
    if (!emitter->dynamic) {
        indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "if (%s >= %s) abort();\n", emitter->index,
                          emitter->count);
        return;
    }
    indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output, "if (%s == %s) {\n", emitter->index,
                      emitter->capacity);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "size_t f2c_constructor_new_capacity = %s < 8U ? 8U : %s;\n",
                      emitter->capacity, emitter->capacity);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output, "if (f2c_constructor_new_capacity != 8U) {\n");
    indent(&emitter->context->output, depth + 2);
    f2c_buffer_append(&emitter->context->output,
                      "if (f2c_constructor_new_capacity > SIZE_MAX / 2U) abort();\n");
    indent(&emitter->context->output, depth + 2);
    f2c_buffer_append(&emitter->context->output, "f2c_constructor_new_capacity *= 2U;\n");
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output, "}\n");
    indent(&emitter->context->output, depth + 1);
    if (emitter->character) {
        f2c_buffer_printf(&emitter->context->output,
                          "if ((size_t)(%s) != 0U && f2c_constructor_new_capacity > "
                          "SIZE_MAX / (size_t)(%s)) abort();\n",
                          emitter->character_length, emitter->character_length);
        indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "const size_t f2c_constructor_new_bytes = "
                          "f2c_constructor_new_capacity * (size_t)(%s);\n",
                          emitter->character_length);
        indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "char *f2c_constructor_replacement = (char *)realloc(%s, "
                          "f2c_constructor_new_bytes == 0U ? 1U : "
                          "f2c_constructor_new_bytes);\n",
                          emitter->storage);
    } else {
        f2c_buffer_printf(&emitter->context->output,
                          "if (f2c_constructor_new_capacity > SIZE_MAX / sizeof(*%s)) abort();\n",
                          emitter->storage);
        indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "%s *f2c_constructor_replacement = (%s *)realloc(%s, "
                          "f2c_constructor_new_capacity * sizeof(*%s));\n",
                          f2c_symbol_c_type(emitter->target), f2c_symbol_c_type(emitter->target),
                          emitter->storage, emitter->storage);
    }
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output,
                      "if (f2c_constructor_replacement == NULL) abort();\n");
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s = f2c_constructor_replacement;\n",
                      emitter->storage);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s = f2c_constructor_new_capacity;\n",
                      emitter->capacity);
    indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
}

static int emit_constructor_character_length(ConstructorEmitter *emitter, const F2cExpr *expression,
                                             int depth) {
    char *length;
    const size_t temporary = emitter->next_temporary++;
    if (!emitter->character || !emitter->infer_character_length)
        return 1;
    length = f2c_character_length_expression(emitter->unit, expression);
    if (length == NULL)
        return 0;
    indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output,
                      "const size_t f2c_constructor_item_length_%zu = (size_t)(%s);\n", temporary,
                      length);
    indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output, "if (!%s) {\n", emitter->character_length_set);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s = f2c_constructor_item_length_%zu;\n",
                      emitter->character_length, temporary);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "%s = true;\n", emitter->character_length_set);
    indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output,
                      "} else if (%s != f2c_constructor_item_length_%zu) {\n",
                      emitter->character_length, temporary);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output, "abort();\n");
    indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
    free(length);
    return 1;
}

static const char *constructor_substitution(const ConstructorEmitter *emitter,
                                            const F2cExpr *expression) {
    size_t i;
    if (expression == NULL || expression->kind != F2C_EXPR_NAME)
        return NULL;
    for (i = emitter->substitution_count; i != 0U; --i) {
        const ConstructorSubstitution *substitution = &emitter->substitutions[i - 1U];
        if ((expression->symbol != NULL && expression->symbol == substitution->symbol) ||
            (expression->text != NULL && substitution->name != NULL &&
             strcmp(expression->text, substitution->name) == 0))
            return substitution->replacement;
    }
    return NULL;
}

static F2cExpr *clone_constructor_expression(const ConstructorEmitter *emitter,
                                             const F2cExpr *expression) {
    F2cExpr *clone;
    const char *replacement;
    size_t i;
    if (expression == NULL)
        return NULL;
    clone = (F2cExpr *)calloc(1U, sizeof(*clone));
    if (clone == NULL)
        return NULL;
    clone->kind = expression->kind;
    clone->type = expression->type;
    clone->rank = expression->rank;
    clone->definable = expression->definable;
    clone->source_offset = expression->source_offset;
    clone->source_length = expression->source_length;
    clone->parse_error_offset = expression->parse_error_offset;
    clone->symbol = expression->symbol;
    clone->temporary_index = expression->temporary_index;
    clone->text = expression->text != NULL ? f2c_strdup(expression->text) : NULL;
    clone->source = expression->source != NULL ? f2c_strdup(expression->source) : NULL;
    replacement = constructor_substitution(emitter, expression);
    clone->lowered_c =
        replacement != NULL
            ? f2c_strdup(replacement)
            : (expression->lowered_c != NULL ? f2c_strdup(expression->lowered_c) : NULL);
    if ((expression->text != NULL && clone->text == NULL) ||
        (expression->source != NULL && clone->source == NULL) ||
        ((replacement != NULL || expression->lowered_c != NULL) && clone->lowered_c == NULL))
        goto failed;
    if (replacement != NULL) {
        clone->kind = F2C_EXPR_NAME;
        clone->type = TYPE_INTEGER;
        clone->rank = 0U;
        clone->definable = 0;
        clone->symbol = NULL;
        return clone;
    }
    if (expression->child_count != 0U) {
        clone->children = (F2cExpr **)calloc(expression->child_count, sizeof(*clone->children));
        if (clone->children == NULL)
            goto failed;
        clone->child_capacity = expression->child_count;
        for (i = 0U; i < expression->child_count; ++i) {
            clone->children[i] = clone_constructor_expression(emitter, expression->children[i]);
            if (clone->children[i] == NULL)
                goto failed;
            ++clone->child_count;
        }
    }
    return clone;

failed:
    f2c_expr_free(clone);
    return NULL;
}

static int emit_constructor_value(ConstructorEmitter *emitter, const F2cExpr *expression,
                                  int depth);

static int emit_constructor_scalar(ConstructorEmitter *emitter, const F2cExpr *expression,
                                   int depth) {
    F2cExpr *substituted = clone_constructor_expression(emitter, expression);
    char *code = substituted != NULL ? emit_expression(emitter->unit, substituted) : NULL;
    Buffer target = {0};
    int result = 0;
    if (substituted == NULL || code == NULL)
        goto cleanup;
    if ((emitter->character && substituted->type != TYPE_CHARACTER) ||
        (!emitter->character && substituted->type == TYPE_CHARACTER))
        goto cleanup;
    if (!emit_constructor_character_length(emitter, substituted, depth))
        goto cleanup;
    emit_constructor_capacity(emitter, depth);
    if (emitter->character) {
        f2c_buffer_printf(&target, "%s + %s * %s", emitter->storage, emitter->index,
                          emitter->character_length);
        if (!f2c_emit_character_storage_assignment(emitter->context, emitter->unit, target.data,
                                                   emitter->character_length, substituted, code,
                                                   depth))
            goto cleanup;
        indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "++%s;\n", emitter->index);
    } else {
        indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "%s[%s++] = (%s)(%s);\n", emitter->storage,
                          emitter->index, f2c_symbol_c_type(emitter->target), code);
    }
    result = 1;

cleanup:
    free(f2c_buffer_take(&target));
    free(code);
    f2c_expr_free(substituted);
    return result;
}

static int emit_constructor_whole_array(ConstructorEmitter *emitter, const F2cExpr *expression,
                                        int depth) {
    Symbol *source = expression != NULL ? expression->symbol : NULL;
    char *source_count;
    char *source_length = NULL;
    const size_t temporary = emitter->next_temporary++;
    if (expression == NULL || expression->kind != F2C_EXPR_NAME || source == NULL ||
        source->rank == 0U)
        return 0;
    source_count = f2c_symbol_element_count(emitter->unit, source);
    if (source_count == NULL || *source_count == '\0') {
        free(source_count);
        return 0;
    }
    if (emitter->character) {
        if (source->type != TYPE_CHARACTER) {
            free(source_count);
            return 0;
        }
        source_length = f2c_symbol_character_length(emitter->unit, source);
        if (source_length == NULL) {
            free(source_count);
            return 0;
        }
    }
    if (source->allocatable || source->pointer) {
        indent(&emitter->context->output, depth);
        f2c_buffer_printf(&emitter->context->output, "if (%s == NULL) abort();\n",
                          f2c_symbol_c_name(emitter->unit, source));
    }
    if (!emit_constructor_character_length(emitter, expression, depth)) {
        free(source_length);
        free(source_count);
        return 0;
    }
    indent(&emitter->context->output, depth);
    f2c_buffer_printf(&emitter->context->output,
                      "for (size_t f2c_constructor_source_%zu = 0U; "
                      "f2c_constructor_source_%zu < (size_t)(%s); "
                      "++f2c_constructor_source_%zu) {\n",
                      temporary, temporary, source_count, temporary);
    emit_constructor_capacity(emitter, depth + 1);
    if (emitter->character) {
        indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "const size_t f2c_constructor_copy_%zu = "
                          "F2C_MIN((size_t)(%s), (size_t)(%s));\n",
                          temporary, emitter->character_length, source_length);
        indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "if (f2c_constructor_copy_%zu != 0U) "
                          "memmove(%s + %s * %s, %s + f2c_constructor_source_%zu * "
                          "(size_t)(%s), f2c_constructor_copy_%zu);\n",
                          temporary, emitter->storage, emitter->index, emitter->character_length,
                          f2c_symbol_c_name(emitter->unit, source), temporary, source_length,
                          temporary);
        indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "if ((size_t)(%s) > f2c_constructor_copy_%zu) "
                          "memset(%s + %s * %s + f2c_constructor_copy_%zu, ' ', "
                          "(size_t)(%s) - f2c_constructor_copy_%zu);\n",
                          emitter->character_length, temporary, emitter->storage, emitter->index,
                          emitter->character_length, temporary, emitter->character_length,
                          temporary);
        indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output, "++%s;\n", emitter->index);
    } else {
        if (!f2c_type_is_numeric(source->type) && source->type != TYPE_LOGICAL) {
            free(source_length);
            free(source_count);
            return 0;
        }
        indent(&emitter->context->output, depth + 1);
        f2c_buffer_printf(&emitter->context->output,
                          "%s[%s++] = (%s)%s["
                          "f2c_constructor_source_%zu];\n",
                          emitter->storage, emitter->index, f2c_symbol_c_type(emitter->target),
                          f2c_symbol_c_name(emitter->unit, source), temporary);
    }
    indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
    free(source_length);
    free(source_count);
    return 1;
}

static int emit_constructor_implied_do(ConstructorEmitter *emitter, const F2cExpr *expression,
                                       int depth) {
    F2cExpr *initial_expression = NULL;
    F2cExpr *limit_expression = NULL;
    F2cExpr *step_expression = NULL;
    char *initial = NULL;
    char *limit = NULL;
    char *step = NULL;
    char iterator_name[64];
    const size_t value_count = expression->child_count >= 3U ? expression->child_count - 3U : 0U;
    const size_t temporary = emitter->next_temporary++;
    size_t i;
    int result = 0;
    if (value_count == 0U || emitter->substitution_count == 64U)
        return 0;
    initial_expression = clone_constructor_expression(emitter, expression->children[value_count]);
    limit_expression =
        clone_constructor_expression(emitter, expression->children[value_count + 1U]);
    step_expression = clone_constructor_expression(emitter, expression->children[value_count + 2U]);
    initial =
        initial_expression != NULL ? emit_expression(emitter->unit, initial_expression) : NULL;
    limit = limit_expression != NULL ? emit_expression(emitter->unit, limit_expression) : NULL;
    step = step_expression != NULL ? emit_expression(emitter->unit, step_expression) : NULL;
    if (initial == NULL || limit == NULL || step == NULL)
        goto cleanup;
    (void)snprintf(iterator_name, sizeof(iterator_name), "f2c_constructor_value_%zu", temporary);
    indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "{\n");
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "const int64_t f2c_constructor_first_%zu = (int64_t)(%s);\n", temporary,
                      initial);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "const int64_t f2c_constructor_last_%zu = (int64_t)(%s);\n", temporary,
                      limit);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "const int64_t f2c_constructor_step_%zu = (int64_t)(%s);\n", temporary, step);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output, "if (f2c_constructor_step_%zu == 0) abort();\n",
                      temporary);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "const uint64_t f2c_constructor_iterations_%zu = "
                      "f2c_constructor_step_%zu > 0 ? "
                      "(f2c_constructor_first_%zu <= f2c_constructor_last_%zu ? "
                      "(uint64_t)((f2c_constructor_last_%zu - f2c_constructor_first_%zu) / "
                      "f2c_constructor_step_%zu) + UINT64_C(1) : UINT64_C(0)) : "
                      "(f2c_constructor_first_%zu >= f2c_constructor_last_%zu ? "
                      "(uint64_t)((f2c_constructor_first_%zu - f2c_constructor_last_%zu) / "
                      "(-f2c_constructor_step_%zu)) + UINT64_C(1) : UINT64_C(0));\n",
                      temporary, temporary, temporary, temporary, temporary, temporary, temporary,
                      temporary, temporary, temporary, temporary, temporary);
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_printf(&emitter->context->output,
                      "for (uint64_t f2c_constructor_iteration_%zu = UINT64_C(0); "
                      "f2c_constructor_iteration_%zu < f2c_constructor_iterations_%zu; "
                      "++f2c_constructor_iteration_%zu) {\n",
                      temporary, temporary, temporary, temporary);
    indent(&emitter->context->output, depth + 2);
    f2c_buffer_printf(&emitter->context->output,
                      "const int32_t %s = (int32_t)(f2c_constructor_first_%zu + "
                      "(int64_t)f2c_constructor_iteration_%zu * "
                      "f2c_constructor_step_%zu);\n",
                      iterator_name, temporary, temporary, temporary);
    indent(&emitter->context->output, depth + 2);
    f2c_buffer_printf(&emitter->context->output, "(void)%s;\n", iterator_name);
    emitter->substitutions[emitter->substitution_count].symbol = expression->symbol;
    emitter->substitutions[emitter->substitution_count].name = expression->text;
    emitter->substitutions[emitter->substitution_count].replacement = iterator_name;
    ++emitter->substitution_count;
    for (i = 0U; i < value_count; ++i) {
        if (!emit_constructor_value(emitter, expression->children[i], depth + 2)) {
            --emitter->substitution_count;
            goto cleanup;
        }
    }
    --emitter->substitution_count;
    indent(&emitter->context->output, depth + 1);
    f2c_buffer_append(&emitter->context->output, "}\n");
    indent(&emitter->context->output, depth);
    f2c_buffer_append(&emitter->context->output, "}\n");
    result = 1;

cleanup:
    free(initial);
    free(limit);
    free(step);
    f2c_expr_free(initial_expression);
    f2c_expr_free(limit_expression);
    f2c_expr_free(step_expression);
    return result;
}

static int emit_constructor_value(ConstructorEmitter *emitter, const F2cExpr *expression,
                                  int depth) {
    size_t i;
    if (expression == NULL)
        return 0;
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        for (i = 0U; i < expression->child_count; ++i) {
            if (!emit_constructor_value(emitter, expression->children[i], depth))
                return 0;
        }
        return 1;
    }
    if (expression->kind == F2C_EXPR_IMPLIED_DO)
        return emit_constructor_implied_do(emitter, expression, depth);
    if (expression->kind == F2C_EXPR_NAME && expression->rank != 0U)
        return emit_constructor_whole_array(emitter, expression, depth);
    if (expression->rank != 0U)
        return 0;
    return emit_constructor_scalar(emitter, expression, depth);
}

static int emit_numeric_constructor_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                               const F2cExpr *constructor,
                                               const char *element_count, int depth) {
    const size_t output_start = context->output.length;
    ConstructorEmitter emitter;
    if (constructor == NULL || constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR ||
        element_count == NULL)
        return 0;
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context;
    emitter.unit = unit;
    emitter.target = left_symbol;
    emitter.storage = "f2c_constructor_values";
    emitter.count = "f2c_constructor_count";
    emitter.index = "f2c_constructor_index";
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_constructor_count = (size_t)(%s);\n",
                      element_count);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_constructor_count > SIZE_MAX / sizeof(%s)) abort();\n",
                      f2c_symbol_c_type(left_symbol));
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "%s *f2c_constructor_values = f2c_constructor_count == 0U ? NULL : "
                      "(%s *)malloc(f2c_constructor_count * "
                      "sizeof(*f2c_constructor_values));\n",
                      f2c_symbol_c_type(left_symbol), f2c_symbol_c_type(left_symbol));
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_constructor_count != 0U && "
                                        "f2c_constructor_values == NULL) abort();\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_index = 0U;\n");
    if (!emit_constructor_value(&emitter, constructor, depth + 1)) {
        if (context->output.data != NULL && output_start <= context->output.length) {
            context->output.length = output_start;
            context->output.data[output_start] = '\0';
        }
        return 0;
    }
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_constructor_index != f2c_constructor_count) abort();\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_constructor_count != 0U) memmove(%s, "
                      "f2c_constructor_values, f2c_constructor_count * "
                      "sizeof(*f2c_constructor_values));\n",
                      f2c_symbol_c_name(unit, left_symbol));
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_constructor_values);\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    return 1;
}

static int emit_allocatable_numeric_constructor_assignment(Context *context, Unit *unit,
                                                           Symbol *target,
                                                           const F2cExpr *constructor, int depth) {
    const size_t output_start = context->output.length;
    const char *name;
    ConstructorEmitter emitter;
    if (constructor == NULL || constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || target == NULL ||
        !target->allocatable || target->rank != 1U || target->type == TYPE_CHARACTER)
        return 0;
    name = f2c_symbol_c_name(unit, target);
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context;
    emitter.unit = unit;
    emitter.target = target;
    emitter.storage = "f2c_constructor_values";
    emitter.index = "f2c_constructor_index";
    emitter.capacity = "f2c_constructor_capacity";
    emitter.dynamic = 1;

    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "%s *f2c_constructor_values = NULL;\n",
                      f2c_symbol_c_type(target));
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_index = 0U;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_capacity = 0U;\n");
    if (!emit_constructor_value(&emitter, constructor, depth + 1))
        goto failed;
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_constructor_index > (size_t)INT32_MAX) abort();\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_constructor_reallocate = %s == NULL || "
                      "(size_t)%s_extent_1 != f2c_constructor_index;\n",
                      name, name);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_constructor_reallocate) {\n");
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "if (f2c_constructor_values == NULL) {\n");
    indent(&context->output, depth + 3);
    f2c_buffer_append(&context->output, "f2c_constructor_values = malloc(1U);\n");
    indent(&context->output, depth + 3);
    f2c_buffer_append(&context->output, "if (f2c_constructor_values == NULL) abort();\n");
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "free(%s);\n", name);
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s = f2c_constructor_values;\n", name);
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "f2c_constructor_values = NULL;\n");
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s_lower_1 = 1;\n", name);
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s_extent_1 = (int32_t)f2c_constructor_index;\n", name);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "} else if (f2c_constructor_index != 0U) {\n");
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output,
                      "memmove(%s, f2c_constructor_values, f2c_constructor_index * "
                      "sizeof(*f2c_constructor_values));\n",
                      name);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_constructor_values);\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    return 1;

failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    return 0;
}

static int emit_allocatable_character_constructor_assignment(Context *context, Unit *unit,
                                                             Symbol *target,
                                                             const F2cExpr *constructor,
                                                             int depth) {
    const size_t output_start = context->output.length;
    const char *name;
    char *fixed_length = NULL;
    ConstructorEmitter emitter;
    if (constructor == NULL || constructor->kind != F2C_EXPR_ARRAY_CONSTRUCTOR || target == NULL ||
        !target->allocatable || target->rank != 1U || target->type != TYPE_CHARACTER)
        return 0;
    name = f2c_symbol_c_name(unit, target);
    if (!target->deferred_character) {
        fixed_length = f2c_symbol_character_length(unit, target);
        if (fixed_length == NULL)
            return 0;
    }
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context;
    emitter.unit = unit;
    emitter.target = target;
    emitter.storage = "f2c_constructor_values";
    emitter.index = "f2c_constructor_index";
    emitter.capacity = "f2c_constructor_capacity";
    emitter.character_length = "f2c_constructor_character_length";
    emitter.character_length_set = "f2c_constructor_character_length_set";
    emitter.character = 1;
    emitter.dynamic = 1;
    emitter.infer_character_length = target->deferred_character;

    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "char *f2c_constructor_values = NULL;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_index = 0U;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "size_t f2c_constructor_capacity = 0U;\n");
    indent(&context->output, depth + 1);
    if (target->deferred_character) {
        f2c_buffer_append(&context->output, "size_t f2c_constructor_character_length = 0U;\n");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "bool f2c_constructor_character_length_set = false;\n");
    } else {
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_constructor_character_length = (size_t)(%s);\n",
                          fixed_length);
    }
    if (!emit_constructor_value(&emitter, constructor, depth + 1))
        goto failed;
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_constructor_index > (size_t)INT32_MAX) abort();\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_constructor_character_length != 0U && "
                                        "f2c_constructor_index > SIZE_MAX / "
                                        "f2c_constructor_character_length) abort();\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "const size_t f2c_constructor_bytes = f2c_constructor_index * "
                      "f2c_constructor_character_length;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "const bool f2c_constructor_reallocate = %s == NULL || "
                      "(size_t)%s_extent_1 != f2c_constructor_index",
                      name, name);
    if (target->deferred_character)
        f2c_buffer_printf(&context->output,
                          " || f2c_char_len_%s != f2c_constructor_character_length", name);
    f2c_buffer_append(&context->output, ";\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_constructor_reallocate) {\n");
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "if (f2c_constructor_values == NULL) {\n");
    indent(&context->output, depth + 3);
    f2c_buffer_append(&context->output, "f2c_constructor_values = (char *)malloc(1U);\n");
    indent(&context->output, depth + 3);
    f2c_buffer_append(&context->output, "if (f2c_constructor_values == NULL) abort();\n");
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "free(%s);\n", name);
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s = f2c_constructor_values;\n", name);
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "f2c_constructor_values = NULL;\n");
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s_lower_1 = 1;\n", name);
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output, "%s_extent_1 = (int32_t)f2c_constructor_index;\n", name);
    if (target->deferred_character) {
        indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output, "f2c_char_len_%s = f2c_constructor_character_length;\n",
                          name);
    }
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "} else if (f2c_constructor_bytes != 0U) {\n");
    indent(&context->output, depth + 2);
    f2c_buffer_printf(&context->output,
                      "memmove(%s, f2c_constructor_values, f2c_constructor_bytes);\n", name);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_constructor_values);\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    free(fixed_length);
    return 1;

failed:
    if (context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    free(fixed_length);
    return 0;
}

static int emit_whole_character_array_assignment(Context *context, Unit *unit, Symbol *left_symbol,
                                                 const F2cExpr *right, Symbol *right_symbol,
                                                 const char *element_count, int depth) {
    const size_t output_start = context->output.length;
    const int has_constructor = right != NULL && right->kind == F2C_EXPR_ARRAY_CONSTRUCTOR;
    char *left_length = NULL;
    char *right_length = NULL;
    char *right_count = NULL;
    char *scalar_code = NULL;
    int result = 0;
    if (left_symbol == NULL || left_symbol->type != TYPE_CHARACTER || element_count == NULL)
        return 0;
    if (!has_constructor && right_symbol != NULL && right_symbol->rank != 0U &&
        right_symbol->type == TYPE_CHARACTER) {
        right_length = f2c_symbol_character_length(unit, right_symbol);
        right_count = f2c_symbol_element_count(unit, right_symbol);
        if (right_length == NULL || right_count == NULL)
            goto cleanup;
    } else if (!has_constructor && right != NULL && right->rank == 0U &&
               right->type == TYPE_CHARACTER) {
        scalar_code = emit_expression(unit, right);
        if (scalar_code == NULL)
            goto cleanup;
    } else if (!has_constructor) {
        goto cleanup;
    }
    left_length = f2c_symbol_character_length(unit, left_symbol);
    if (left_length == NULL)
        goto cleanup;

    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_whole_count = (size_t)(%s);\n",
                      element_count);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_whole_length = (size_t)(%s);\n",
                      left_length);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_whole_length != 0U && f2c_whole_count > "
                                        "SIZE_MAX / f2c_whole_length) abort();\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "const size_t f2c_whole_bytes = f2c_whole_count * f2c_whole_length;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "char *f2c_whole_values = f2c_whole_count == 0U ? NULL : "
                      "(char *)malloc(f2c_whole_bytes == 0U ? 1U : f2c_whole_bytes);\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_whole_count != 0U && f2c_whole_values == NULL) abort();\n");
    if (has_constructor) {
        ConstructorEmitter emitter;
        memset(&emitter, 0, sizeof(emitter));
        emitter.context = context;
        emitter.unit = unit;
        emitter.target = left_symbol;
        emitter.storage = "f2c_whole_values";
        emitter.count = "f2c_whole_count";
        emitter.index = "f2c_whole_index";
        emitter.character_length = "f2c_whole_length";
        emitter.character = 1;
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "size_t f2c_whole_index = 0U;\n");
        if (!emit_constructor_value(&emitter, right, depth + 1))
            goto cleanup;
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "if (f2c_whole_index != f2c_whole_count) abort();\n");
    } else if (right_symbol != NULL && right_symbol->rank != 0U) {
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "const size_t f2c_whole_source_count = (size_t)(%s);\n",
                          right_count);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "const size_t f2c_whole_source_length = (size_t)(%s);\n", right_length);
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "if (f2c_whole_source_count != f2c_whole_count) abort();\n");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "const size_t f2c_whole_copy_length = "
                          "F2C_MIN(f2c_whole_length, f2c_whole_source_length);\n");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "for (size_t f2c_whole_index = 0U; "
                          "f2c_whole_index < f2c_whole_count; ++f2c_whole_index) {\n");
        indent(&context->output, depth + 2);
        f2c_buffer_printf(&context->output,
                          "memmove(f2c_whole_values + f2c_whole_index * f2c_whole_length, "
                          "%s + f2c_whole_index * f2c_whole_source_length, "
                          "f2c_whole_copy_length);\n",
                          f2c_symbol_c_name(unit, right_symbol));
        indent(&context->output, depth + 2);
        f2c_buffer_append(&context->output,
                          "if (f2c_whole_length > f2c_whole_copy_length) "
                          "memset(f2c_whole_values + f2c_whole_index * f2c_whole_length + "
                          "f2c_whole_copy_length, ' ', "
                          "f2c_whole_length - f2c_whole_copy_length);\n");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "}\n");
    } else {
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "char *f2c_whole_scalar = (char *)malloc("
                                            "f2c_whole_length == 0U ? 1U : f2c_whole_length);\n");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "if (f2c_whole_scalar == NULL) abort();\n");
        if (!f2c_emit_character_storage_assignment(context, unit, "f2c_whole_scalar",
                                                   "f2c_whole_length", right, scalar_code,
                                                   depth + 1))
            goto cleanup;
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output,
                          "for (size_t f2c_whole_index = 0U; "
                          "f2c_whole_index < f2c_whole_count; ++f2c_whole_index) "
                          "if (f2c_whole_length != 0U) "
                          "memmove(f2c_whole_values + f2c_whole_index * f2c_whole_length, "
                          "f2c_whole_scalar, f2c_whole_length);\n");
        indent(&context->output, depth + 1);
        f2c_buffer_append(&context->output, "free(f2c_whole_scalar);\n");
    }
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_whole_bytes != 0U) memmove(%s, f2c_whole_values, "
                      "f2c_whole_bytes);\n",
                      f2c_symbol_c_name(unit, left_symbol));
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_whole_values);\n");
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "}\n");
    result = 1;

cleanup:
    if (!result && context->output.data != NULL && output_start <= context->output.length) {
        context->output.length = output_start;
        context->output.data[output_start] = '\0';
    }
    free(left_length);
    free(right_length);
    free(right_count);
    free(scalar_code);
    return result;
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
    indent(&context->output, depth);
    f2c_buffer_append(&context->output, "{\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_rows = (size_t)(%s);\n",
                      target_rows);
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output, "const size_t f2c_transform_columns = (size_t)(%s);\n",
                      target_columns);
    indent(&context->output, depth + 1);
    if (matmul) {
        f2c_buffer_printf(&context->output, "const size_t f2c_transform_inner = (size_t)(%s);\n",
                          left_columns);
        indent(&context->output, depth + 1);
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
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "if (f2c_transform_columns != 0U && f2c_transform_rows > "
                                        "SIZE_MAX / f2c_transform_columns) abort();\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "const size_t f2c_transform_count = f2c_transform_rows * "
                                        "f2c_transform_columns;\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "%s *f2c_transform_values = f2c_transform_count == 0U ? NULL : "
                      "(%s *)malloc(f2c_transform_count * sizeof(*f2c_transform_values));\n",
                      c_type, c_type);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "if (f2c_transform_count != 0U && f2c_transform_values == NULL) abort();\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output,
                      "for (size_t f2c_transform_column = 0U; f2c_transform_column < "
                      "f2c_transform_columns; ++f2c_transform_column) {\n");
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "for (size_t f2c_transform_row = 0U; f2c_transform_row < "
                                        "f2c_transform_rows; ++f2c_transform_row) {\n");
    if (matmul) {
        indent(&context->output, depth + 3);
        f2c_buffer_printf(&context->output, "%s f2c_transform_value = (%s)0;\n", c_type, c_type);
        indent(&context->output, depth + 3);
        f2c_buffer_append(&context->output,
                          "for (size_t f2c_transform_index = 0U; f2c_transform_index < "
                          "f2c_transform_inner; ++f2c_transform_index)\n");
        indent(&context->output, depth + 4);
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
        indent(&context->output, depth + 3);
        f2c_buffer_append(&context->output,
                          "f2c_transform_values[f2c_transform_row + f2c_transform_rows * "
                          "f2c_transform_column] = f2c_transform_value;\n");
    } else {
        indent(&context->output, depth + 3);
        f2c_buffer_printf(&context->output,
                          "f2c_transform_values[f2c_transform_row + f2c_transform_rows * "
                          "f2c_transform_column] = %s[f2c_transform_column + "
                          "f2c_transform_columns * f2c_transform_row];\n",
                          left_name);
    }
    indent(&context->output, depth + 2);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "}\n");
    indent(&context->output, depth + 1);
    f2c_buffer_printf(&context->output,
                      "if (f2c_transform_count != 0U) memmove(%s, f2c_transform_values, "
                      "f2c_transform_count * sizeof(*f2c_transform_values));\n",
                      target_name);
    indent(&context->output, depth + 1);
    f2c_buffer_append(&context->output, "free(f2c_transform_values);\n");
    indent(&context->output, depth);
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
        char *result = emit_expression(unit, right);
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
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "{\n");
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "f2c_descriptor f2c_function_result = %s;\n", result);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output,
                          "if (f2c_function_result.rank != %zuU || "
                          "f2c_function_result.element_size != sizeof(%s)) abort();\n",
                          left_symbol->rank, f2c_symbol_c_type(left_symbol));
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "free(%s);\n", name);
        indent(&context->output, depth + 1);
        f2c_buffer_printf(&context->output, "%s = (%s *)f2c_function_result.data;\n", name,
                          f2c_symbol_c_type(left_symbol));
        if (left_symbol->deferred_character) {
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "f2c_char_len_%s = f2c_function_result.character_length;\n", name);
        }
        for (dimension = 0U; dimension < left_symbol->rank; ++dimension) {
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "if (f2c_function_result.lower[%zu] < INT32_MIN || "
                              "f2c_function_result.lower[%zu] > INT32_MAX || "
                              "f2c_function_result.extent[%zu] < 0 || "
                              "f2c_function_result.extent[%zu] > INT32_MAX) abort();\n",
                              dimension, dimension, dimension, dimension);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "%s_lower_%zu = (int32_t)f2c_function_result.lower[%zu];\n", name,
                              dimension + 1U, dimension);
            indent(&context->output, depth + 1);
            f2c_buffer_printf(&context->output,
                              "%s_extent_%zu = (int32_t)f2c_function_result.extent[%zu];\n", name,
                              dimension + 1U, dimension);
        }
        indent(&context->output, depth);
        f2c_buffer_append(&context->output, "}\n");
        free(result);
        return 1;
    }
    if (f2c_emit_allocatable_array_assignment(context, unit, left, right, depth))
        return 1;
    if (left_symbol->allocatable && has_constructor) {
        const int emitted = left_symbol->type == TYPE_CHARACTER
                                ? emit_allocatable_character_constructor_assignment(
                                      context, unit, left_symbol, right, depth)
                                : emit_allocatable_numeric_constructor_assignment(
                                      context, unit, left_symbol, right, depth);
        if (!emitted)
            f2c_diagnostic(context, line, 1,
                           "allocatable array-constructor assignment requires a supported "
                           "rank-one intrinsic target and compatible element values");
        return 1;
    }
    if (left_symbol->allocatable || left_symbol->pointer) {
        indent(&context->output, depth);
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
                ? emit_whole_character_array_assignment(context, unit, left_symbol, right,
                                                        right_symbol, element_count, depth)
                : emit_numeric_constructor_assignment(context, unit, left_symbol, right,
                                                      element_count, depth);
        if (!emitted)
            f2c_diagnostic(context, line, 1,
                           "array constructor contains an unsupported value or incompatible "
                           "element type");
        goto cleanup;
    }
    if (left_symbol->type == TYPE_CHARACTER &&
        emit_whole_character_array_assignment(context, unit, left_symbol, right, right_symbol,
                                              element_count, depth))
        goto cleanup;
    indent(&context->output, depth);
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
        char *value = emit_expression(unit, right);
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
