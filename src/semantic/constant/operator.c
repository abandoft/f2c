#include "semantic/constant/private.h"

#include "ir/expression.h"
#include "semantic/operator.h"

#include <stdlib.h>

static int store_relation(F2cOperator operator_kind, int equal, int less, int greater,
                          int64_t *value) {
    switch (operator_kind) {
    case F2C_OPERATOR_EQUAL:
        *value = equal;
        break;
    case F2C_OPERATOR_NOT_EQUAL:
        *value = !equal;
        break;
    case F2C_OPERATOR_LESS:
        *value = less;
        break;
    case F2C_OPERATOR_LESS_EQUAL:
        *value = less || equal;
        break;
    case F2C_OPERATOR_GREATER:
        *value = greater;
        break;
    case F2C_OPERATOR_GREATER_EQUAL:
        *value = greater || equal;
        break;
    default:
        return 0;
    }
    return 1;
}

static int real_operand(F2cConstantEvaluation *evaluation, const F2cExpr *expression, int kind,
                        double *value, size_t depth) {
    if (expression->type == TYPE_INTEGER) {
        int64_t integer;
        if (!f2c_constant_evaluate_integer(evaluation, expression, &integer, depth))
            return 0;
        /* Convert directly to the selected model, avoiding double rounding of
         * an INTEGER(kind=8) through double when the operand model is REAL(4). */
        *value = kind == 4 ? (double)(float)integer : (double)integer;
        return 1;
    }
    if (!f2c_constant_evaluate_real(evaluation, expression, value, depth))
        return 0;
    if (kind == 4)
        *value = (double)(float)*value;
    return 1;
}

static int complex_operand(F2cConstantEvaluation *evaluation, const F2cExpr *expression, int kind,
                           F2cComplexConstant *value, size_t depth) {
    if (expression->type != TYPE_COMPLEX && expression->type != TYPE_DOUBLE_COMPLEX) {
        value->imaginary = 0.0;
        return real_operand(evaluation, expression, kind, &value->real, depth);
    }
    if (!f2c_constant_evaluate_complex(evaluation, expression, value, depth))
        return 0;
    if (kind == 4) {
        value->real = (double)(float)value->real;
        value->imaginary = (double)(float)value->imaginary;
    }
    return 1;
}

static int character_relation(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                              int64_t *value, size_t depth) {
    char *left = NULL;
    char *right = NULL;
    size_t left_length = 0U;
    size_t right_length = 0U;
    int comparison = 0;
    int result = 0;
    if (!f2c_constant_evaluate_character(evaluation, expression->children[0], &left, &left_length,
                                         depth + 1U) ||
        !f2c_constant_evaluate_character(evaluation, expression->children[1], &right, &right_length,
                                         depth + 1U))
        goto cleanup;
    for (size_t i = 0U; i < left_length || i < right_length; ++i) {
        const unsigned char a = i < left_length ? (unsigned char)left[i] : (unsigned char)' ';
        const unsigned char b = i < right_length ? (unsigned char)right[i] : (unsigned char)' ';
        if (a != b) {
            comparison = a < b ? -1 : 1;
            break;
        }
    }
    result = store_relation(expression->operator_kind, comparison == 0, comparison < 0,
                            comparison > 0, value);
cleanup:
    free(left);
    free(right);
    return result;
}

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
    if (typing.left.type == TYPE_INTEGER) {
        if (!f2c_constant_evaluate_integer(evaluation, expression->children[0], &left,
                                           depth + 1U) ||
            !f2c_constant_evaluate_integer(evaluation, expression->children[1], &right, depth + 1U))
            return 0;
        return store_relation(expression->operator_kind, left == right, left<right, left> right,
                              value);
    }
    if (typing.left.type == TYPE_CHARACTER)
        return typing.left.kind == 1 && character_relation(evaluation, expression, value, depth);
    if (typing.left.kind != 4 && typing.left.kind != 8)
        return 0;
    if (typing.left.type == TYPE_COMPLEX || typing.left.type == TYPE_DOUBLE_COMPLEX) {
        F2cComplexConstant a;
        F2cComplexConstant b;
        if (!complex_operand(evaluation, expression->children[0], typing.left.kind, &a,
                             depth + 1U) ||
            !complex_operand(evaluation, expression->children[1], typing.right.kind, &b,
                             depth + 1U))
            return 0;
        return store_relation(expression->operator_kind,
                              a.real == b.real && a.imaginary == b.imaginary, 0, 0, value);
    }
    {
        double a;
        double b;
        if (!real_operand(evaluation, expression->children[0], typing.left.kind, &a, depth + 1U) ||
            !real_operand(evaluation, expression->children[1], typing.right.kind, &b, depth + 1U))
            return 0;
        return store_relation(expression->operator_kind, a == b, a<b, a> b, value);
    }
}
