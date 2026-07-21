#include "codegen/array/private.h"

#include "codegen/descriptor/private.h"
#include "semantic/intrinsic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static F2cExpr *clone_exact(const F2cExpr *expression);

static F2cExpr *clone_shell(const F2cExpr *expression) {
    F2cExpr *clone;
    if (expression == NULL)
        return NULL;
    clone = (F2cExpr *)calloc(1U, sizeof(*clone));
    if (clone == NULL)
        return NULL;
    *clone = *expression;
    clone->text = expression->text != NULL ? f2c_strdup(expression->text) : NULL;
    clone->source = expression->source != NULL ? f2c_strdup(expression->source) : NULL;
    clone->lowered_c = expression->lowered_c != NULL ? f2c_strdup(expression->lowered_c) : NULL;
    clone->lowered_extent_c =
        expression->lowered_extent_c != NULL ? f2c_strdup(expression->lowered_extent_c) : NULL;
    clone->lowered_character_length_c = expression->lowered_character_length_c != NULL
                                            ? f2c_strdup(expression->lowered_character_length_c)
                                            : NULL;
    clone->children = NULL;
    clone->child_count = 0U;
    clone->child_capacity = 0U;
    if ((expression->text != NULL && clone->text == NULL) ||
        (expression->source != NULL && clone->source == NULL) ||
        (expression->lowered_c != NULL && clone->lowered_c == NULL) ||
        (expression->lowered_extent_c != NULL && clone->lowered_extent_c == NULL) ||
        (expression->lowered_character_length_c != NULL &&
         clone->lowered_character_length_c == NULL)) {
        f2c_expr_free(clone);
        return NULL;
    }
    return clone;
}

static F2cExpr *clone_exact(const F2cExpr *expression) {
    F2cExpr *clone = clone_shell(expression);
    size_t child;
    if (clone == NULL)
        return NULL;
    for (child = 0U; child < expression->child_count; ++child) {
        F2cExpr *copy = clone_exact(expression->children[child]);
        if (copy == NULL || !f2c_expr_push(clone, copy)) {
            f2c_expr_free(copy);
            f2c_expr_free(clone);
            return NULL;
        }
    }
    return clone;
}

F2cExpr *f2c_array_clone_expression(const F2cExpr *expression) { return clone_exact(expression); }

static F2cExpr *lowered_expression(char *code, Type type, int type_kind) {
    F2cExpr *index;
    if (code == NULL)
        return NULL;
    index = f2c_expr_new(F2C_EXPR_NAME, type, "f2c_element_index", strlen("f2c_element_index"));
    if (index == NULL) {
        free(code);
        return NULL;
    }
    index->type_kind = type_kind;
    index->lowered_c = code;
    return index;
}

static F2cExpr *lowered_integer(char *code) {
    return lowered_expression(code, TYPE_INTEGER, f2c_default_kind(TYPE_INTEGER));
}

static F2cExpr *ordinal_subscript(Unit *unit, const F2cExpr *expression, size_t dimension,
                                  const char *ordinal) {
    char *lower = f2c_descriptor_dimension_lower(unit, expression, dimension);
    Buffer code = {0};
    if (lower == NULL)
        return NULL;
    f2c_buffer_printf(&code, "((%s) + (int32_t)(%s))", lower, ordinal);
    free(lower);
    return lowered_integer(f2c_buffer_take(&code));
}

static F2cExpr *section_subscript(Unit *unit, const F2cExpr *expression, const F2cExpr *section,
                                  size_t dimension, const char *ordinal) {
    const F2cExpr *lower_expression;
    const F2cExpr *step_expression;
    char *lower;
    char *step;
    Buffer code = {0};
    if (section == NULL || section->child_count != 3U)
        return NULL;
    lower_expression = section->children[0]->kind == F2C_EXPR_INVALID ? NULL : section->children[0];
    step_expression = section->children[2]->kind == F2C_EXPR_INVALID ? NULL : section->children[2];
    step = step_expression != NULL ? f2c_array_emit_expression(unit, step_expression)
                                   : f2c_strdup("1");
    lower = lower_expression != NULL ? f2c_array_emit_expression(unit, lower_expression)
                                     : f2c_descriptor_dimension_lower(unit, expression, dimension);
    if (lower == NULL || step == NULL) {
        free(lower);
        free(step);
        return NULL;
    }
    f2c_buffer_printf(&code, "((%s) + (int32_t)(%s) * (%s))", lower, ordinal, step);
    free(lower);
    free(step);
    return lowered_integer(f2c_buffer_take(&code));
}

static char *section_extent(Unit *unit, const F2cExpr *expression, const F2cExpr *section,
                            size_t dimension) {
    const F2cExpr *lower_expression;
    const F2cExpr *upper_expression;
    const F2cExpr *step_expression;
    char *declared_lower;
    char *declared_upper;
    char *lower = NULL;
    char *upper = NULL;
    char *step;
    Buffer result = {0};
    if (section == NULL || section->child_count != 3U || expression == NULL ||
        expression->symbol == NULL)
        return NULL;
    lower_expression = section->children[0]->kind == F2C_EXPR_INVALID ? NULL : section->children[0];
    upper_expression = section->children[1]->kind == F2C_EXPR_INVALID ? NULL : section->children[1];
    step_expression = section->children[2]->kind == F2C_EXPR_INVALID ? NULL : section->children[2];
    declared_lower = f2c_descriptor_dimension_lower(unit, expression, dimension);
    declared_upper = f2c_descriptor_dimension_upper(unit, expression, dimension);
    step = step_expression != NULL ? f2c_array_emit_expression(unit, step_expression)
                                   : f2c_strdup("1");
    lower = lower_expression != NULL ? f2c_array_emit_expression(unit, lower_expression)
                                     : (declared_lower != NULL ? f2c_strdup(declared_lower) : NULL);
    upper = upper_expression != NULL ? f2c_array_emit_expression(unit, upper_expression)
                                     : (declared_upper != NULL ? f2c_strdup(declared_upper) : NULL);
    free(declared_lower);
    free(declared_upper);
    if (lower == NULL || upper == NULL || step == NULL) {
        free(lower);
        free(upper);
        free(step);
        return NULL;
    }
    f2c_buffer_printf(&result,
                      "((%s) == 0 ? (abort(), (size_t)0) : ((%s) > 0 ? "
                      "((%s) >= (%s) ? (size_t)(((%s) - (%s)) / (%s) + 1) : 0U) : "
                      "((%s) >= (%s) ? (size_t)(((%s) - (%s)) / (-(%s)) + 1) : 0U)))",
                      step, step, upper, lower, upper, lower, step, lower, upper, lower, upper,
                      step);
    free(lower);
    free(upper);
    free(step);
    return f2c_buffer_take(&result);
}

static F2cExpr *whole_array_element(Unit *unit, const F2cExpr *expression, size_t rank,
                                    const char *const *ordinals) {
    F2cExpr *element;
    size_t dimension;
    if (expression->symbol == NULL || expression->symbol->rank != rank)
        return NULL;
    element = f2c_expr_new(F2C_EXPR_ARRAY_REFERENCE, expression->type, expression->text,
                           expression->text != NULL ? strlen(expression->text) : 0U);
    if (element == NULL)
        return NULL;
    element->symbol = expression->symbol;
    element->derived_type = expression->derived_type;
    element->definable = expression->definable;
    element->type_kind = expression->type_kind;
    for (dimension = 0U; dimension < rank; ++dimension) {
        F2cExpr *index = ordinal_subscript(unit, expression, dimension, ordinals[dimension]);
        if (index == NULL || !f2c_expr_push(element, index)) {
            f2c_expr_free(index);
            f2c_expr_free(element);
            return NULL;
        }
    }
    return element;
}

static F2cExpr *array_reference_element(Unit *unit, const F2cExpr *expression, size_t rank,
                                        const char *const *ordinals) {
    F2cExpr *element = clone_shell(expression);
    const size_t selector_offset = f2c_descriptor_selector_offset(expression);
    size_t dimension;
    size_t ordinal = 0U;
    if (element == NULL || expression->symbol == NULL)
        goto failed;
    element->rank = 0U;
    memset(&element->shape, 0, sizeof(element->shape));
    element->shape.kind = F2C_SHAPE_SCALAR;
    for (dimension = 0U; dimension < selector_offset; ++dimension) {
        F2cExpr *base = clone_exact(expression->children[dimension]);
        if (base == NULL || !f2c_expr_push(element, base)) {
            f2c_expr_free(base);
            goto failed;
        }
    }
    for (dimension = selector_offset; dimension < expression->child_count; ++dimension) {
        const F2cExpr *selector = expression->children[dimension];
        const size_t source_dimension = dimension - selector_offset;
        F2cExpr *index;
        if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
            if (ordinal >= rank)
                goto failed;
            index = section_subscript(unit, expression, selector, source_dimension,
                                      ordinals[ordinal++]);
        } else if (selector->rank != 0U) {
            const char *vector_ordinal[1];
            if (ordinal >= rank)
                goto failed;
            vector_ordinal[0] = ordinals[ordinal++];
            index = f2c_array_element_expression(unit, selector, 1U, vector_ordinal);
        } else {
            index = clone_exact(selector);
        }
        if (index == NULL || !f2c_expr_push(element, index)) {
            f2c_expr_free(index);
            goto failed;
        }
    }
    if (ordinal != rank)
        goto failed;
    return element;
failed:
    f2c_expr_free(element);
    return NULL;
}

static F2cExpr *elemental_tree(Unit *unit, const F2cExpr *expression, size_t rank,
                               const char *const *ordinals) {
    F2cExpr *element = clone_shell(expression);
    size_t child;
    if (element == NULL)
        return NULL;
    element->rank = 0U;
    memset(&element->shape, 0, sizeof(element->shape));
    element->shape.kind = F2C_SHAPE_SCALAR;
    for (child = 0U; child < expression->child_count; ++child) {
        const F2cExpr *source = expression->children[child];
        F2cExpr *copy;
        if (source->kind == F2C_EXPR_KEYWORD_ARGUMENT && source->child_count == 1U) {
            copy = clone_shell(source);
            if (copy != NULL) {
                F2cExpr *value =
                    source->children[0]->rank != 0U
                        ? f2c_array_element_expression(unit, source->children[0], rank, ordinals)
                        : clone_exact(source->children[0]);
                if (value == NULL || !f2c_expr_push(copy, value)) {
                    f2c_expr_free(value);
                    f2c_expr_free(copy);
                    copy = NULL;
                }
            }
        } else {
            copy = source->rank != 0U ? f2c_array_element_expression(unit, source, rank, ordinals)
                                      : clone_exact(source);
        }
        if (copy == NULL || !f2c_expr_push(element, copy)) {
            f2c_expr_free(copy);
            f2c_expr_free(element);
            return NULL;
        }
    }
    return element;
}

static const F2cExpr *transform_argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static F2cExpr *transpose_element(Unit *unit, const F2cExpr *expression,
                                  const char *const *ordinals) {
    const F2cExpr *source;
    const char *source_ordinals[2];
    if (expression == NULL || expression->child_count != 1U || expression->rank != 2U)
        return NULL;
    source = transform_argument_value(expression->children[0]);
    if (source == NULL || source->rank != 2U)
        return NULL;
    source_ordinals[0] = ordinals[1];
    source_ordinals[1] = ordinals[0];
    return f2c_array_element_expression(unit, source, 2U, source_ordinals);
}

F2cExpr *f2c_array_element_expression(Unit *unit, const F2cExpr *expression, size_t rank,
                                      const char *const *ordinals) {
    const F2cIntrinsicSignature *intrinsic;
    if (expression == NULL || rank == 0U || ordinals == NULL)
        return NULL;
    if (expression->rank == 0U)
        return clone_exact(expression);
    if (expression->rank != rank)
        return NULL;
    if (expression->kind == F2C_EXPR_NAME)
        return whole_array_element(unit, expression, rank, ordinals);
    if (expression->kind == F2C_EXPR_COMPONENT && expression->child_count == 1U) {
        F2cExpr *element = clone_shell(expression);
        size_t dimension;
        F2cExpr *base = clone_exact(expression->children[0]);
        if (element == NULL || base == NULL || !f2c_expr_push(element, base)) {
            f2c_expr_free(base);
            f2c_expr_free(element);
            return NULL;
        }
        element->rank = 0U;
        memset(&element->shape, 0, sizeof(element->shape));
        element->shape.kind = F2C_SHAPE_SCALAR;
        for (dimension = 0U; dimension < rank; ++dimension) {
            F2cExpr *index = ordinal_subscript(unit, expression, dimension, ordinals[dimension]);
            if (index == NULL || !f2c_expr_push(element, index)) {
                f2c_expr_free(index);
                f2c_expr_free(element);
                return NULL;
            }
        }
        return element;
    }
    if (expression->kind == F2C_EXPR_ARRAY_REFERENCE || expression->kind == F2C_EXPR_COMPONENT)
        return array_reference_element(unit, expression, rank, ordinals);
    if (expression->kind == F2C_EXPR_UNARY || expression->kind == F2C_EXPR_BINARY ||
        expression->kind == F2C_EXPR_KEYWORD_ARGUMENT)
        return elemental_tree(unit, expression, rank, ordinals);
    if (expression->kind == F2C_EXPR_CALL) {
        if (expression->text != NULL && strcmp(expression->text, "transpose") == 0)
            return transpose_element(unit, expression, ordinals);
        if (rank == 1U && expression->text != NULL &&
            (strcmp(expression->text, "shape") == 0 || strcmp(expression->text, "lbound") == 0 ||
             strcmp(expression->text, "ubound") == 0))
            return lowered_expression(f2c_array_inquiry_element(unit, expression, ordinals[0]),
                                      expression->type, expression->type_kind);
        intrinsic = f2c_find_intrinsic(expression->text);
        return ((intrinsic != NULL && intrinsic->rank_rule == F2C_INTRINSIC_RANK_ELEMENTAL) ||
                (expression->resolved_procedure != NULL &&
                 expression->resolved_procedure->elemental))
                   ? elemental_tree(unit, expression, rank, ordinals)
                   : NULL;
    }
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR && rank == 1U) {
        char *array = f2c_array_emit_expression(unit, expression);
        Buffer code = {0};
        F2cExpr *element;
        if (array == NULL)
            return NULL;
        if (expression->type == TYPE_CHARACTER && expression->lowered_character_length_c != NULL)
            f2c_buffer_printf(&code, "(&(%s)[(size_t)(%s) * (size_t)(%s)])", array, ordinals[0],
                              expression->lowered_character_length_c);
        else
            f2c_buffer_printf(&code, "((%s)[(size_t)(%s)])", array, ordinals[0]);
        free(array);
        element =
            lowered_expression(f2c_buffer_take(&code), expression->type, expression->type_kind);
        if (element != NULL)
            element->derived_type = expression->derived_type;
        if (element != NULL && expression->type == TYPE_CHARACTER &&
            expression->lowered_character_length_c != NULL) {
            element->lowered_character_length_c =
                f2c_strdup(expression->lowered_character_length_c);
            if (element->lowered_character_length_c == NULL) {
                f2c_expr_free(element);
                return NULL;
            }
        }
        return element;
    }
    return NULL;
}

static const F2cExpr *shape_carrier(const F2cExpr *expression, size_t rank) {
    size_t child;
    if (expression == NULL)
        return NULL;
    if ((expression->kind == F2C_EXPR_NAME || expression->kind == F2C_EXPR_ARRAY_REFERENCE ||
         expression->kind == F2C_EXPR_COMPONENT ||
         expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) &&
        expression->rank == rank)
        return expression;
    for (child = 0U; child < expression->child_count; ++child) {
        const F2cExpr *carrier = shape_carrier(expression->children[child], rank);
        if (carrier != NULL)
            return carrier;
    }
    return NULL;
}

char *f2c_array_expression_extent(Unit *unit, const F2cExpr *expression, size_t dimension) {
    const F2cExpr *carrier;
    char literal[32];
    if (expression == NULL || dimension >= expression->rank)
        return NULL;
    if (expression->kind == F2C_EXPR_CALL && expression->text != NULL &&
        strcmp(expression->text, "transpose") == 0 && expression->child_count == 1U &&
        expression->rank == 2U) {
        const F2cExpr *source = transform_argument_value(expression->children[0]);
        return source != NULL && source->rank == 2U
                   ? f2c_array_expression_extent(unit, source, 1U - dimension)
                   : NULL;
    }
    if (expression->shape.dimensions[dimension].extent_known) {
        (void)snprintf(literal, sizeof(literal), "%llu",
                       (unsigned long long)expression->shape.dimensions[dimension].extent);
        return f2c_strdup(literal);
    }
    carrier = shape_carrier(expression, expression->rank);
    if (carrier == NULL)
        return NULL;
    if (dimension == 0U && carrier->lowered_extent_c != NULL)
        return f2c_strdup(carrier->lowered_extent_c);
    if (carrier->kind == F2C_EXPR_NAME && carrier->symbol != NULL)
        return f2c_symbol_dimension_extent(unit, carrier->symbol, dimension);
    if (carrier->kind == F2C_EXPR_COMPONENT && carrier->symbol != NULL &&
        carrier->child_count == 1U)
        return f2c_descriptor_dimension_extent(unit, carrier, dimension);
    if (carrier->kind == F2C_EXPR_ARRAY_CONSTRUCTOR && dimension == 0U) {
        (void)snprintf(literal, sizeof(literal), "%zu", carrier->child_count);
        return f2c_strdup(literal);
    }
    if (carrier->kind == F2C_EXPR_ARRAY_REFERENCE || carrier->kind == F2C_EXPR_COMPONENT) {
        const size_t selector_offset = f2c_descriptor_selector_offset(carrier);
        size_t selector;
        size_t result_dimension = 0U;
        for (selector = selector_offset; selector < carrier->child_count; ++selector) {
            const F2cExpr *index = carrier->children[selector];
            if (index->kind != F2C_EXPR_ARRAY_SECTION && index->rank == 0U)
                continue;
            if (result_dimension++ != dimension)
                continue;
            if (index->kind == F2C_EXPR_ARRAY_SECTION && carrier->symbol != NULL)
                return section_extent(unit, carrier, index, selector - selector_offset);
            if (index->rank == 1U)
                return f2c_array_expression_extent(unit, index, 0U);
            return NULL;
        }
    }
    return NULL;
}
