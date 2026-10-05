#include "semantic/constant/private.h"
#include "semantic/constant/relation.h"

#include "ir/expression.h"
#include "semantic/operator.h"

int f2c_constant_evaluate_logical_operator(F2cConstantEvaluation *evaluation,
                                           const F2cExpr *expression, int64_t *value,
                                           size_t depth) {
    F2cOperatorTyping typing;
    int64_t left;
    int64_t right;
    const int unary = expression->kind == F2C_EXPR_UNARY;
    if (expression->type != TYPE_LOGICAL || expression->resolved_procedure != NULL ||
        expression->child_count != (unary ? 1U : 2U) ||
        f2c_operator_typing(expression->operator_kind, unary,
                            f2c_expression_scalar_type(expression->children[0]),
                            unary ? f2c_scalar_type(TYPE_UNKNOWN, 0)
                                  : f2c_expression_scalar_type(expression->children[1]),
                            &typing) != F2C_OPERATOR_VALID)
        return 0;
    if (unary || expression->operator_kind >= F2C_OPERATOR_AND) {
        if (!f2c_constant_evaluate_integer(evaluation, expression->children[0], &left, depth + 1U))
            return 0;
        if (unary) {
            *value = !left;
            return 1;
        }
        if (!f2c_constant_evaluate_integer(evaluation, expression->children[1], &right, depth + 1U))
            return 0;
        switch (expression->operator_kind) {
        case F2C_OPERATOR_AND:
            *value = left != 0 && right != 0;
            break;
        case F2C_OPERATOR_OR:
            *value = left != 0 || right != 0;
            break;
        case F2C_OPERATOR_EQUIVALENT:
            *value = (left != 0) == (right != 0);
            break;
        case F2C_OPERATOR_NOT_EQUIVALENT:
            *value = (left != 0) != (right != 0);
            break;
        default:
            return 0;
        }
        return 1;
    }
    if (!f2c_operator_is_comparison(expression->operator_kind))
        return 0;
    F2cConstantValue a = {0}, b = {0};
    const int result =
        f2c_constant_evaluate_value(evaluation, expression->children[0], &a, depth + 1U) &&
        f2c_constant_evaluate_value(evaluation, expression->children[1], &b, depth + 1U) &&
        f2c_constant_value_relation(expression->operator_kind, &a, &b, value);
    f2c_constant_value_free(&a);
    f2c_constant_value_free(&b);
    return result;
}
