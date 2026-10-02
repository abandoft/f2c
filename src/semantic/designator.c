#include "semantic/semantic.h"

void f2c_validate_designator_components(Context *context, const F2cExpr *expression) {
    const Symbol *component;
    if (expression == NULL || expression->kind != F2C_EXPR_COMPONENT ||
        expression->child_count == 0U || expression->children[0] == NULL ||
        expression->children[0]->rank == 0U || (component = expression->symbol) == NULL ||
        (!component->allocatable && !component->pointer))
        return;
    /* Data-ref constraint C919 applies even to a scalar component: an array
     * part-ref cannot be followed by an independently allocated/pointer object. */
    f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &expression->span, 1,
                             "ALLOCATABLE or POINTER component '%s' cannot follow an array "
                             "part-reference",
                             component->name);
}

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
