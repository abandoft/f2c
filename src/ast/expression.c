#include "ast/internal.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

F2cExpr *f2c_expr_new(F2cExprKind kind, Type type, const char *text, size_t length) {
    F2cExpr *expression = (F2cExpr *)calloc(1U, sizeof(*expression));
    if (expression == NULL)
        return NULL;
    expression->kind = kind;
    expression->type = type;
    expression->type_kind = f2c_default_kind(type);
    expression->shape.kind = F2C_SHAPE_SCALAR;
    switch (kind) {
    case F2C_EXPR_INTEGER_LITERAL:
    case F2C_EXPR_REAL_LITERAL:
    case F2C_EXPR_STRING_LITERAL:
    case F2C_EXPR_LOGICAL_LITERAL:
        expression->value_category = F2C_VALUE_CONSTANT;
        break;
    case F2C_EXPR_NAME:
    case F2C_EXPR_ARRAY_REFERENCE:
    case F2C_EXPR_ARRAY_SECTION:
    case F2C_EXPR_SUBSTRING:
        expression->value_category = F2C_VALUE_VARIABLE;
        break;
    case F2C_EXPR_INVALID:
    case F2C_EXPR_ABSENT_ARGUMENT:
        expression->value_category = F2C_VALUE_INVALID;
        break;
    default:
        expression->value_category = F2C_VALUE_TEMPORARY;
        break;
    }
    expression->temporary_index = SIZE_MAX;
    expression->statement_temporary_index = SIZE_MAX;
    expression->statement_nested_temporary_begin = SIZE_MAX;
    expression->source_offset = SIZE_MAX;
    expression->parse_error_offset = SIZE_MAX;
    expression->tree_depth = 1U;
    if (text != NULL) {
        expression->text = f2c_strdup_n(text, length);
        if (expression->text == NULL) {
            free(expression);
            return NULL;
        }
    }
    return expression;
}

F2cExpr *f2c_expr_new_absent(Type type, size_t rank) {
    F2cExpr *expression = f2c_expr_new(F2C_EXPR_ABSENT_ARGUMENT, type, NULL, 0U);
    if (expression != NULL) {
        expression->rank = rank;
        expression->shape.kind = rank == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_UNKNOWN;
        expression->shape.rank = rank;
    }
    return expression;
}

static const F2cIntegerSubstitution *find_substitution(
    const F2cExpr *expression, const F2cIntegerSubstitution *substitutions,
    size_t substitution_count) {
    size_t index;
    if (expression == NULL || expression->kind != F2C_EXPR_NAME)
        return NULL;
    for (index = substitution_count; index != 0U; --index) {
        const F2cIntegerSubstitution *candidate = &substitutions[index - 1U];
        if ((expression->symbol != NULL && expression->symbol == candidate->symbol) ||
            (expression->text != NULL && candidate->name != NULL &&
             strcmp(expression->text, candidate->name) == 0))
            return candidate;
    }
    return NULL;
}

static int clone_owned_text(F2cExpr *clone, const F2cExpr *source) {
    clone->text = source->text != NULL ? f2c_strdup(source->text) : NULL;
    clone->source = source->source != NULL ? f2c_strdup(source->source) : NULL;
    clone->lowered_c = source->lowered_c != NULL ? f2c_strdup(source->lowered_c) : NULL;
    clone->lowered_extent_c =
        source->lowered_extent_c != NULL ? f2c_strdup(source->lowered_extent_c) : NULL;
    clone->lowered_character_length_c = source->lowered_character_length_c != NULL
                                            ? f2c_strdup(source->lowered_character_length_c)
                                            : NULL;
    return (source->text == NULL || clone->text != NULL) &&
           (source->source == NULL || clone->source != NULL) &&
           (source->lowered_c == NULL || clone->lowered_c != NULL) &&
           (source->lowered_extent_c == NULL || clone->lowered_extent_c != NULL) &&
           (source->lowered_character_length_c == NULL ||
            clone->lowered_character_length_c != NULL);
}

F2cExpr *f2c_expr_clone_substitute_integers(const F2cExpr *expression,
                                            const F2cIntegerSubstitution *substitutions,
                                            size_t substitution_count) {
    const F2cIntegerSubstitution *substitution;
    F2cExpr *clone;
    size_t index;
    if (expression == NULL || (substitution_count != 0U && substitutions == NULL))
        return NULL;
    substitution = find_substitution(expression, substitutions, substitution_count);
    if (substitution != NULL) {
        char literal[32];
        const int length =
            snprintf(literal, sizeof(literal), "%" PRId64, substitution->value);
        if (length <= 0 || (size_t)length >= sizeof(literal))
            return NULL;
        clone = f2c_expr_new(F2C_EXPR_INTEGER_LITERAL, TYPE_INTEGER, literal, (size_t)length);
        if (clone == NULL)
            return NULL;
        clone->type_kind = f2c_default_kind(TYPE_INTEGER);
        clone->span = expression->span;
        clone->source_offset = expression->source_offset;
        clone->source_length = expression->source_length;
        return clone;
    }
    clone = (F2cExpr *)calloc(1U, sizeof(*clone));
    if (clone == NULL)
        return NULL;
    clone->kind = expression->kind;
    clone->type = expression->type;
    clone->type_kind = expression->type_kind;
    clone->rank = expression->rank;
    clone->definable = expression->definable;
    clone->value_category = expression->value_category;
    clone->shape = expression->shape;
    clone->span = expression->span;
    clone->parse_error_span = expression->parse_error_span;
    clone->source_offset = expression->source_offset;
    clone->source_length = expression->source_length;
    clone->parse_error_offset = expression->parse_error_offset;
    clone->symbol = expression->symbol;
    clone->resolved_procedure = expression->resolved_procedure;
    clone->derived_type = expression->derived_type;
    clone->temporary_index = expression->temporary_index;
    clone->statement_temporary_index = expression->statement_temporary_index;
    clone->statement_nested_temporary_begin = expression->statement_nested_temporary_begin;
    clone->tree_depth = expression->tree_depth;
    if (!clone_owned_text(clone, expression))
        goto failed;
    if (expression->child_count != 0U) {
        if (expression->child_count > SIZE_MAX / sizeof(*clone->children))
            goto failed;
        clone->children =
            (F2cExpr **)calloc(expression->child_count, sizeof(*clone->children));
        if (clone->children == NULL)
            goto failed;
        clone->child_capacity = expression->child_count;
        for (index = 0U; index < expression->child_count; ++index) {
            clone->children[index] = f2c_expr_clone_substitute_integers(
                expression->children[index], substitutions, substitution_count);
            if (clone->children[index] == NULL)
                goto failed;
            ++clone->child_count;
        }
    }
    return clone;

failed:
    f2c_expr_free(clone);
    return NULL;
}

void f2c_expr_free(F2cExpr *expression) {
    size_t i;
    if (expression == NULL)
        return;
    for (i = 0U; i < expression->child_count; ++i)
        f2c_expr_free(expression->children[i]);
    free(expression->children);
    free(expression->text);
    free(expression->source);
    free(expression->lowered_c);
    free(expression->lowered_extent_c);
    free(expression->lowered_character_length_c);
    free(expression);
}

int f2c_expr_push(F2cExpr *parent, F2cExpr *child) {
    F2cExpr **replacement;
    if (parent == NULL || child == NULL)
        return 0;
    if (parent->child_count == parent->child_capacity) {
        const size_t capacity = parent->child_capacity == 0U ? 4U : parent->child_capacity * 2U;
        replacement = (F2cExpr **)realloc(parent->children, capacity * sizeof(*parent->children));
        if (replacement == NULL)
            return 0;
        parent->children = replacement;
        parent->child_capacity = capacity;
    }
    parent->children[parent->child_count++] = child;
    if (child->tree_depth < SIZE_MAX && child->tree_depth + 1U > parent->tree_depth)
        parent->tree_depth = child->tree_depth + 1U;
    return 1;
}
