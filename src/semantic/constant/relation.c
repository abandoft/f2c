#include "semantic/constant/relation.h"

#include "semantic/operator.h"

static int store_relation(F2cOperator operation, int equal, int less, int greater,
                          int64_t *result) {
    switch (operation) {
    case F2C_OPERATOR_EQUAL:
        *result = equal;
        break;
    case F2C_OPERATOR_NOT_EQUAL:
        *result = !equal;
        break;
    case F2C_OPERATOR_LESS:
        *result = less;
        break;
    case F2C_OPERATOR_LESS_EQUAL:
        *result = less || equal;
        break;
    case F2C_OPERATOR_GREATER:
        *result = greater;
        break;
    case F2C_OPERATOR_GREATER_EQUAL:
        *result = greater || equal;
        break;
    default:
        return 0;
    }
    return 1;
}

static double real_value(const F2cConstantValue *value, int kind) {
    if (value->type.type == TYPE_INTEGER)
        /* Direct INTEGER(8) -> REAL(4) avoids an intermediate double rounding. */
        return kind == 4 ? (double)(float)value->payload.integer : (double)value->payload.integer;
    return kind == 4 ? (double)(float)value->payload.number.real : value->payload.number.real;
}

static double imaginary_value(const F2cConstantValue *value, int kind) {
    if (value->type.type != TYPE_COMPLEX && value->type.type != TYPE_DOUBLE_COMPLEX)
        return 0.0;
    return kind == 4 ? (double)(float)value->payload.number.imaginary
                     : value->payload.number.imaginary;
}

static int character_order(const F2cConstantValue *left, const F2cConstantValue *right,
                           int *order) {
    const size_t a_length = left->payload.character.length;
    const size_t b_length = right->payload.character.length;
    if ((a_length != 0U && left->payload.character.bytes == NULL) ||
        (b_length != 0U && right->payload.character.bytes == NULL))
        return 0;
    *order = 0;
    for (size_t index = 0U; index < a_length || index < b_length; ++index) {
        const unsigned char a = index < a_length
                                    ? (unsigned char)left->payload.character.bytes[index]
                                    : (unsigned char)' ';
        const unsigned char b = index < b_length
                                    ? (unsigned char)right->payload.character.bytes[index]
                                    : (unsigned char)' ';
        if (a != b) {
            *order = a < b ? -1 : 1;
            break;
        }
    }
    return 1;
}

int f2c_constant_value_relation(F2cOperator operation, const F2cConstantValue *left,
                                 const F2cConstantValue *right, int64_t *result) {
    F2cOperatorTyping typing;
    if (left == NULL || right == NULL || result == NULL ||
        f2c_operator_typing(operation, 0, f2c_scalar_type(left->type.type, left->type.kind),
                            f2c_scalar_type(right->type.type, right->type.kind), &typing) !=
            F2C_OPERATOR_VALID)
        return 0;
    if (operation == F2C_OPERATOR_EQUIVALENT || operation == F2C_OPERATOR_NOT_EQUIVALENT) {
        const int equal = (left->payload.integer != 0) == (right->payload.integer != 0);
        *result = operation == F2C_OPERATOR_EQUIVALENT ? equal : !equal;
        return 1;
    }
    if (!f2c_operator_is_comparison(operation))
        return 0;
    if (typing.left.type == TYPE_INTEGER) {
        const int64_t a = left->payload.integer, b = right->payload.integer;
        return store_relation(operation, a == b, a < b, a > b, result);
    }
    if (typing.left.type == TYPE_CHARACTER) {
        int order;
        return typing.left.kind == 1 && character_order(left, right, &order) &&
               store_relation(operation, order == 0, order < 0, order > 0, result);
    }
    if (typing.left.kind != 4 && typing.left.kind != 8)
        return 0;
    const double a = real_value(left, typing.left.kind), b = real_value(right, typing.right.kind);
    if (typing.left.type == TYPE_COMPLEX || typing.left.type == TYPE_DOUBLE_COMPLEX)
        return store_relation(operation,
                              a == b && imaginary_value(left, typing.left.kind) ==
                                            imaginary_value(right, typing.right.kind),
                              0, 0, result);
    return store_relation(operation, a == b, a < b, a > b, result);
}
