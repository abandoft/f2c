#include "ir/expression.h"

const F2cExpr *f2c_substring_parent(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_SUBSTRING &&
                   expression->child_count == 2U
               ? expression->children[0]
               : NULL;
}

const F2cExpr *f2c_substring_range(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_SUBSTRING &&
                   expression->child_count == 2U
               ? expression->children[1]
               : NULL;
}

static const F2cExpr *bound(const F2cExpr *expression, size_t index) {
    const F2cExpr *range = f2c_substring_range(expression);
    if (range == NULL || range->kind != F2C_EXPR_ARRAY_SECTION || range->child_count != 3U ||
        range->children[index] == NULL || range->children[index]->kind == F2C_EXPR_INVALID)
        return NULL;
    return range->children[index];
}

const F2cExpr *f2c_substring_lower(const F2cExpr *expression) { return bound(expression, 0U); }

const F2cExpr *f2c_substring_upper(const F2cExpr *expression) { return bound(expression, 1U); }
