#include "ast/internal.h"

#include <stdint.h>
#include <stdlib.h>

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
