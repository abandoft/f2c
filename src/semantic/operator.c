#include "semantic/operator.h"

#include "ir/expression.h"
#include "semantic/numeric_model.h"

static int is_complex(Type type) { return type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX; }

F2cScalarType f2c_scalar_type(Type type, int kind) {
    F2cScalarType result = {type, kind > 0 ? kind : f2c_default_kind(type)};
    if (type == TYPE_REAL || type == TYPE_DOUBLE)
        result.type = result.kind == 8 ? TYPE_DOUBLE : TYPE_REAL;
    else if (is_complex(type))
        result.type = result.kind == 8 ? TYPE_DOUBLE_COMPLEX : TYPE_COMPLEX;
    return result;
}

F2cScalarType f2c_expression_scalar_type(const F2cExpr *expression) {
    return expression != NULL ? f2c_scalar_type(expression->type, expression->type_kind)
                              : f2c_scalar_type(TYPE_UNKNOWN, 0);
}

F2cOperator f2c_value_equality_operator(Type left, Type right) {
    return left == TYPE_LOGICAL && right == TYPE_LOGICAL ? F2C_OPERATOR_EQUIVALENT
                                                        : F2C_OPERATOR_EQUAL;
}

static int wider_kind(F2cScalarType left, F2cScalarType right) {
    const F2cNumericModel *left_model = f2c_numeric_model(left.type, left.kind);
    const F2cNumericModel *right_model = f2c_numeric_model(right.type, right.kind);
    if (left_model != NULL && right_model != NULL) {
        const int left_precision =
            left.type == TYPE_INTEGER ? left_model->range : left_model->precision;
        const int right_precision =
            right.type == TYPE_INTEGER ? right_model->range : right_model->precision;
        if (left_precision != right_precision)
            return left_precision > right_precision ? left.kind : right.kind;
    }
    /* Equal model precision and LOGICAL kind selection are processor policy.
     * Extended model kinds retain their identity; support validation is separate.
     */
    return left.kind > right.kind ? left.kind : right.kind;
}

static F2cScalarType common_numeric_type(F2cScalarType left, F2cScalarType right) {
    const Type type = is_complex(left.type) || is_complex(right.type)           ? TYPE_COMPLEX
                      : left.type != TYPE_INTEGER || right.type != TYPE_INTEGER ? TYPE_REAL
                                                                                : TYPE_INTEGER;
    const int kind = left.type == TYPE_INTEGER && right.type != TYPE_INTEGER ? right.kind
                     : right.type == TYPE_INTEGER && left.type != TYPE_INTEGER
                         ? left.kind
                         : wider_kind(left, right);
    return f2c_scalar_type(type, kind);
}

F2cOperatorStatus f2c_operator_typing(F2cOperator operator_kind, int unary, F2cScalarType left,
                                      F2cScalarType right, F2cOperatorTyping *typing) {
    F2cOperatorTyping result = {{TYPE_UNKNOWN, 0}, left, right};
    if (typing == NULL)
        return F2C_OPERATOR_INVALID_OPERANDS;
    *typing = result;
    if (operator_kind == F2C_OPERATOR_NONE || operator_kind == F2C_OPERATOR_DEFINED ||
        left.type == TYPE_UNKNOWN || (!unary && right.type == TYPE_UNKNOWN))
        return F2C_OPERATOR_DEFERRED;
    if (unary) {
        if ((operator_kind == F2C_OPERATOR_NOT && left.type == TYPE_LOGICAL) ||
            ((operator_kind == F2C_OPERATOR_ADD || operator_kind == F2C_OPERATOR_SUBTRACT) &&
             f2c_type_is_numeric(left.type)))
            result.result = left;
        else
            return F2C_OPERATOR_INVALID_OPERANDS;
    } else if (f2c_operator_is_numeric(operator_kind)) {
        if (!f2c_type_is_numeric(left.type) || !f2c_type_is_numeric(right.type))
            return F2C_OPERATOR_INVALID_OPERANDS;
        result.result = common_numeric_type(left, right);
        result.left = result.result;
        /* Preserve an INTEGER exponent as an exact integer, not a floating
         * approximation. Both INTEGER operands still choose the wider range.
         */
        result.right = operator_kind == F2C_OPERATOR_POWER && right.type == TYPE_INTEGER
                           ? right
                           : result.result;
    } else if (operator_kind == F2C_OPERATOR_CONCATENATE ||
               (f2c_operator_is_comparison(operator_kind) && left.type == TYPE_CHARACTER &&
                right.type == TYPE_CHARACTER)) {
        if (left.type != TYPE_CHARACTER || right.type != TYPE_CHARACTER)
            return F2C_OPERATOR_INVALID_OPERANDS;
        if (left.kind != right.kind)
            return F2C_OPERATOR_INCOMPATIBLE_KINDS;
        result.result =
            operator_kind == F2C_OPERATOR_CONCATENATE ? left : f2c_scalar_type(TYPE_LOGICAL, 4);
    } else if (f2c_operator_is_comparison(operator_kind)) {
        if (!f2c_type_is_numeric(left.type) || !f2c_type_is_numeric(right.type) ||
            ((is_complex(left.type) || is_complex(right.type)) &&
             operator_kind != F2C_OPERATOR_EQUAL && operator_kind != F2C_OPERATOR_NOT_EQUAL))
            return F2C_OPERATOR_INVALID_OPERANDS;
        result.left = common_numeric_type(left, right);
        result.right = result.left;
        result.result = f2c_scalar_type(TYPE_LOGICAL, 4);
    } else if (operator_kind >= F2C_OPERATOR_AND && operator_kind <= F2C_OPERATOR_NOT_EQUIVALENT) {
        if (left.type != TYPE_LOGICAL || right.type != TYPE_LOGICAL)
            return F2C_OPERATOR_INVALID_OPERANDS;
        result.result = f2c_scalar_type(TYPE_LOGICAL, wider_kind(left, right));
        result.left = result.result;
        result.right = result.result;
    } else {
        return F2C_OPERATOR_INVALID_OPERANDS;
    }
    *typing = result;
    return F2C_OPERATOR_VALID;
}

F2cOperatorStatus f2c_expression_refresh_operator_type(F2cExpr *expression) {
    F2cOperatorTyping typing;
    F2cOperatorStatus status;
    int unary;
    if (expression == NULL || expression->resolved_procedure != NULL ||
        (expression->kind != F2C_EXPR_UNARY && expression->kind != F2C_EXPR_BINARY))
        return F2C_OPERATOR_DEFERRED;
    unary = expression->kind == F2C_EXPR_UNARY;
    if (expression->child_count != (unary ? 1U : 2U))
        return F2C_OPERATOR_DEFERRED;
    status = f2c_operator_typing(expression->operator_kind, unary,
                                 f2c_expression_scalar_type(expression->children[0]),
                                 unary ? f2c_scalar_type(TYPE_UNKNOWN, 0)
                                       : f2c_expression_scalar_type(expression->children[1]),
                                 &typing);
    if (status == F2C_OPERATOR_VALID) {
        expression->type = typing.result.type;
        expression->type_kind = typing.result.kind;
    }
    return status;
}
