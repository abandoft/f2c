#include "semantic/semantic.h"

#include <stdint.h>
#include <string.h>

static int same_scalar_expression(const F2cExpr *left, const F2cExpr *right) {
    size_t child;
    if (left != NULL && left->kind == F2C_EXPR_CALL && left->intrinsic == F2C_INTRINSIC_NONE)
        return 0;
    if (left == right)
        return 1;
    if (left == NULL || right == NULL || left->kind != right->kind || left->type != right->type ||
        left->rank != 0U || right->rank != 0U || left->child_count != right->child_count ||
        left->symbol != right->symbol)
        return 0;
    if ((left->text == NULL) != (right->text == NULL) ||
        (left->text != NULL && strcmp(left->text, right->text) != 0))
        return 0;
    for (child = 0U; child < left->child_count; ++child) {
        if (!same_scalar_expression(left->children[child], right->children[child]))
            return 0;
    }
    return 1;
}

static int declared_character_length(Unit *unit, const Symbol *symbol, int64_t *length) {
    if (symbol == NULL || length == NULL)
        return 0;
    if (symbol->character_length_expression != NULL)
        return f2c_evaluate_integer_constant(unit, symbol->character_length_expression, length);
    if (symbol->character_length_syntax.count != 0U)
        return f2c_evaluate_integer_syntax(unit, symbol->character_length_syntax, length);
    if (symbol->character_length == NULL || strcmp(symbol->character_length, "1") == 0) {
        *length = 1;
        return 1;
    }
    return 0;
}

static int constant_substring_length(Unit *unit, const F2cExpr *expression, int64_t *length) {
    const F2cExpr *selector;
    const F2cExpr *lower = NULL;
    const F2cExpr *upper = NULL;
    int64_t lower_value = 1;
    int64_t upper_value;
    uint64_t distance;
    if (f2c_substring_parent(expression) == NULL ||
        (selector = f2c_substring_range(expression)) == NULL ||
        selector->kind != F2C_EXPR_ARRAY_SECTION || selector->child_count != 3U)
        return 0;
    lower = f2c_substring_lower(expression);
    upper = f2c_substring_upper(expression);
    if (lower != NULL && upper != NULL && same_scalar_expression(lower, upper)) {
        *length = 1;
        return 1;
    }
    if (lower != NULL && !f2c_evaluate_integer_constant(unit, lower, &lower_value))
        return 0;
    if (upper != NULL) {
        if (!f2c_evaluate_integer_constant(unit, upper, &upper_value))
            return 0;
    } else if (!f2c_character_constant_length(unit, f2c_substring_parent(expression),
                                              &upper_value)) {
        return 0;
    }
    if (upper_value < lower_value) {
        *length = 0;
        return 1;
    }
    distance = (uint64_t)upper_value - (uint64_t)lower_value;
    if (distance >= (uint64_t)INT64_MAX)
        return 0;
    *length = (int64_t)(distance + 1U);
    return 1;
}

int f2c_character_constant_length(Unit *unit, const F2cExpr *expression, int64_t *length) {
    const F2cExpr *source;
    if (expression == NULL || length == NULL || expression->type != TYPE_CHARACTER)
        return 0;
    if (expression->kind == F2C_EXPR_STRING_LITERAL) {
        const size_t value = f2c_character_literal_length(expression->text);
        if ((uint64_t)value > (uint64_t)INT64_MAX)
            return 0;
        *length = (int64_t)value;
        return 1;
    }
    if (expression->kind == F2C_EXPR_SUBSTRING)
        return constant_substring_length(unit, expression, length);
    if (expression->symbol != NULL)
        return declared_character_length(unit, expression->symbol, length);
    if (expression->kind != F2C_EXPR_CALL)
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_CHAR ||
        expression->intrinsic == F2C_INTRINSIC_ACHAR) {
        *length = 1;
        return 1;
    }
    if (expression->intrinsic != F2C_INTRINSIC_ADJUSTL &&
        expression->intrinsic != F2C_INTRINSIC_ADJUSTR)
        return 0;
    source = f2c_intrinsic_argument(expression->children, expression->child_count, "string", 0U);
    return f2c_character_constant_length(unit, source, length);
}
