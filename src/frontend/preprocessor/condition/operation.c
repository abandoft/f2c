#include "frontend/preprocessor/condition/private.h"

static int signed_multiply(int64_t left, int64_t right, int64_t *result) {
    if (left == 0 || right == 0) {
        *result = 0;
        return 1;
    }
    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN))
        return 0;
    if (left > 0 ? (right > 0 ? left > INT64_MAX / right : right < INT64_MIN / left)
                 : (right > 0 ? left < INT64_MIN / right : left < INT64_MAX / right))
        return 0;
    *result = left * right;
    return 1;
}

static int integer_compare(IntegerValue left, IntegerValue right) {
    if (left.is_unsigned || right.is_unsigned) {
        const uint64_t l = integer_unsigned(left), r = integer_unsigned(right);
        return l < r ? -1 : l > r;
    }
    return left.signed_value < right.signed_value ? -1 : left.signed_value > right.signed_value;
}

IntegerValue f2c_condition_unary(ExpressionParser *parser, ConditionOperator operation,
                                 IntegerValue value, int evaluate, const char *at) {
    if (operation == CONDITION_ADD)
        return value;
    if (operation == CONDITION_LOGICAL_NOT)
        return signed_integer(evaluate && !integer_true(value));
    if (!evaluate)
        return value.is_unsigned ? unsigned_integer(0U) : signed_integer(0);
    if (operation == CONDITION_BIT_NOT)
        return value.is_unsigned ? unsigned_integer(~value.unsigned_value)
                                 : signed_integer(signed_from_bits(~(uint64_t)value.signed_value));
    if (operation == CONDITION_SUBTRACT) {
        if (value.is_unsigned)
            return unsigned_integer(0U - value.unsigned_value);
        if (value.signed_value == INT64_MIN) {
            expression_error_at(parser, at, "signed negation overflows intmax_t");
            return signed_integer(0);
        }
        return signed_integer(-value.signed_value);
    }
    expression_error_at(parser, at, "invalid unary preprocessor operation");
    return signed_integer(0);
}

IntegerValue f2c_condition_binary(ExpressionParser *parser, ConditionOperator operation,
                                  IntegerValue left, IntegerValue right, int evaluate,
                                  const char *at) {
    if (operation == CONDITION_LOGICAL_AND || operation == CONDITION_LOGICAL_OR) {
        const int l = integer_true(left), r = integer_true(right);
        return signed_integer(evaluate && (operation == CONDITION_LOGICAL_AND ? l && r : l || r));
    }
    if (operation >= CONDITION_LESS && operation <= CONDITION_NOT_EQUAL) {
        if (!evaluate)
            return signed_integer(0);
        const int comparison = integer_compare(left, right);
        switch (operation) {
        case CONDITION_LESS:
            return signed_integer(comparison < 0);
        case CONDITION_LESS_EQUAL:
            return signed_integer(comparison <= 0);
        case CONDITION_GREATER:
            return signed_integer(comparison > 0);
        case CONDITION_GREATER_EQUAL:
            return signed_integer(comparison >= 0);
        case CONDITION_EQUAL:
            return signed_integer(comparison == 0);
        case CONDITION_NOT_EQUAL:
            return signed_integer(comparison != 0);
        default:
            break;
        }
    }
    if (operation == CONDITION_LEFT_SHIFT || operation == CONDITION_RIGHT_SHIFT) {
        if (!evaluate)
            return left;
        if ((!right.is_unsigned && right.signed_value < 0) || integer_unsigned(right) >= 64U) {
            expression_error_at(parser, at, "invalid shift count in preprocessor condition");
            return signed_integer(0);
        }
        const uint64_t count = integer_unsigned(right);
        const int right_shift = operation == CONDITION_RIGHT_SHIFT;
        if (left.is_unsigned)
            return unsigned_integer(right_shift ? left.unsigned_value >> count
                                                : left.unsigned_value << count);
        if (!right_shift) {
            if (left.signed_value < 0 ||
                (count != 0U && left.signed_value > (INT64_MAX >> count))) {
                expression_error_at(parser, at, "signed left shift overflows intmax_t");
                return signed_integer(0);
            }
            return signed_integer(left.signed_value << count);
        }
        return signed_integer(left.signed_value >= 0 ? left.signed_value >> count
                                                     : -1 - ((-1 - left.signed_value) >> count));
    }
    if (!evaluate)
        return common_zero(left, right);
    if (operation == CONDITION_BIT_AND || operation == CONDITION_BIT_XOR ||
        operation == CONDITION_BIT_OR) {
        const uint64_t l = integer_unsigned(left), r = integer_unsigned(right);
        const uint64_t bits = operation == CONDITION_BIT_AND   ? l & r
                              : operation == CONDITION_BIT_XOR ? l ^ r
                                                               : l | r;
        return left.is_unsigned || right.is_unsigned ? unsigned_integer(bits)
                                                     : signed_integer(signed_from_bits(bits));
    }
    if (left.is_unsigned || right.is_unsigned) {
        const uint64_t l = integer_unsigned(left), r = integer_unsigned(right);
        if ((operation == CONDITION_DIVIDE || operation == CONDITION_REMAINDER) && r == 0U) {
            expression_error_at(parser, at, "division by zero in preprocessor condition");
            return signed_integer(0);
        }
        switch (operation) {
        case CONDITION_ADD:
            return unsigned_integer(l + r);
        case CONDITION_SUBTRACT:
            return unsigned_integer(l - r);
        case CONDITION_MULTIPLY:
            return unsigned_integer(l * r);
        case CONDITION_DIVIDE:
            return unsigned_integer(l / r);
        case CONDITION_REMAINDER:
            return unsigned_integer(l % r);
        default:
            break;
        }
    } else {
        const int64_t l = left.signed_value, r = right.signed_value;
        if (operation == CONDITION_ADD || operation == CONDITION_SUBTRACT) {
            if ((operation == CONDITION_ADD &&
                 ((r > 0 && l > INT64_MAX - r) || (r < 0 && l < INT64_MIN - r))) ||
                (operation == CONDITION_SUBTRACT &&
                 ((r < 0 && l > INT64_MAX + r) || (r > 0 && l < INT64_MIN + r)))) {
                expression_error_at(parser, at, "signed addition overflows intmax_t");
                return signed_integer(0);
            }
            return signed_integer(operation == CONDITION_ADD ? l + r : l - r);
        }
        if (operation == CONDITION_MULTIPLY) {
            int64_t result;
            if (!signed_multiply(l, r, &result)) {
                expression_error_at(parser, at, "signed multiplication overflows intmax_t");
                return signed_integer(0);
            }
            return signed_integer(result);
        }
        if (operation == CONDITION_DIVIDE || operation == CONDITION_REMAINDER) {
            if (r == 0) {
                expression_error_at(parser, at, "division by zero in preprocessor condition");
                return signed_integer(0);
            }
            if (l == INT64_MIN && r == -1) {
                expression_error_at(parser, at, "signed division overflows intmax_t");
                return signed_integer(0);
            }
            return signed_integer(operation == CONDITION_DIVIDE ? l / r : l % r);
        }
    }
    expression_error_at(parser, at, "invalid binary preprocessor operation");
    return signed_integer(0);
}
