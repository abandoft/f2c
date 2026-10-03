#include "semantic/semantic.h"

static int contiguous_object(const Symbol *symbol) {
    size_t dimension;
    if (symbol == NULL)
        return 0;
    if (symbol->contiguous)
        return 1;
    if (symbol->pointer)
        return 0;
    for (dimension = 0U; dimension < symbol->rank; ++dimension)
        if (symbol->dimensions[dimension].kind == F2C_DIMENSION_ASSUMED_SHAPE)
            return 0;
    return 1;
}

static int omitted(const F2cExpr *expression) {
    return expression != NULL && expression->kind == F2C_EXPR_INVALID;
}

static int contiguous_section(const F2cExpr *expression, size_t first_selector) {
    size_t selector;
    size_t last_triplet = SIZE_MAX;
    int saw_scalar = 0;
    for (selector = first_selector; selector < expression->child_count; ++selector) {
        const F2cExpr *subscript = expression->children[selector];
        if (subscript == NULL)
            return 0;
        if (subscript->kind == F2C_EXPR_ARRAY_SECTION) {
            if (saw_scalar || subscript->child_count != 3U)
                return 0;
            last_triplet = selector;
        } else if (subscript->rank != 0U) {
            return 0;
        } else {
            saw_scalar = 1;
        }
    }
    if (last_triplet == SIZE_MAX)
        return 0;
    for (selector = first_selector; selector <= last_triplet; ++selector) {
        const F2cExpr *triplet = expression->children[selector];
        if (!omitted(triplet->children[2]) ||
            (selector != last_triplet &&
             (!omitted(triplet->children[0]) || !omitted(triplet->children[1]))))
            return 0;
    }
    return 1;
}

int f2c_expression_is_simply_contiguous(const F2cExpr *expression) {
    if (expression != NULL && expression->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
        expression->child_count == 1U)
        expression = expression->children[0];
    if (expression == NULL || expression->rank == 0U || expression->symbol == NULL)
        return 0;
    /* F2018 9.5.4 is a syntactic guarantee, not an IS_CONTIGUOUS runtime query.
     * Even an explicit unit stride is not a simply contiguous triplet. */
    switch (expression->kind) {
    case F2C_EXPR_NAME:
        return contiguous_object(expression->symbol);
    case F2C_EXPR_ARRAY_REFERENCE:
        return contiguous_object(expression->symbol) && contiguous_section(expression, 0U);
    case F2C_EXPR_COMPONENT:
        if (expression->child_count == 0U || expression->children[0] == NULL ||
            expression->children[0]->rank != 0U || expression->symbol->rank == 0U ||
            !contiguous_object(expression->symbol))
            return 0;
        return expression->child_count == 1U || contiguous_section(expression, 1U);
    case F2C_EXPR_CALL:
        return expression->symbol->external_result_pointer &&
               expression->symbol->external_result_contiguous;
    default:
        /* Computed/parenthesized values have no designator attributes. */
        return 0;
    }
}
