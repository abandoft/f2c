#include "semantic/semantic.h"

int f2c_expression_has_target_attribute(const F2cExpr *expression) {
    if (expression == NULL || expression->symbol == NULL ||
        (expression->kind != F2C_EXPR_NAME && expression->kind != F2C_EXPR_ARRAY_REFERENCE &&
         expression->kind != F2C_EXPR_COMPONENT && expression->kind != F2C_EXPR_SUBSTRING))
        return 0;
    if (expression->symbol->target || expression->symbol->pointer)
        return 1;
    if (expression->kind == F2C_EXPR_SUBSTRING)
        return f2c_expression_has_target_attribute(f2c_substring_parent(expression));
    if (expression->kind == F2C_EXPR_COMPONENT && expression->child_count != 0U)
        return f2c_expression_has_target_attribute(expression->children[0]);
    return 0;
}

int f2c_expression_has_vector_subscript(const F2cExpr *expression) {
    size_t selector;
    if (expression != NULL && expression->kind == F2C_EXPR_SUBSTRING)
        return f2c_expression_has_vector_subscript(f2c_substring_parent(expression));
    if (expression == NULL ||
        (expression->kind != F2C_EXPR_ARRAY_REFERENCE && expression->kind != F2C_EXPR_COMPONENT))
        return 0;
    if (expression->kind == F2C_EXPR_COMPONENT && expression->child_count != 0U &&
        f2c_expression_has_vector_subscript(expression->children[0]))
        return 1;
    for (selector = expression->kind == F2C_EXPR_COMPONENT ? 1U : 0U;
         selector < expression->child_count; ++selector) {
        const F2cExpr *subscript = expression->children[selector];
        if (subscript != NULL && subscript->kind != F2C_EXPR_ARRAY_SECTION && subscript->rank != 0U)
            return 1;
    }
    return 0;
}
