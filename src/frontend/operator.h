#ifndef F2C_FRONTEND_OPERATOR_H
#define F2C_FRONTEND_OPERATOR_H

typedef enum F2cOperator {
    F2C_OPERATOR_NONE,
    F2C_OPERATOR_DEFINED,
    F2C_OPERATOR_ADD,
    F2C_OPERATOR_SUBTRACT,
    F2C_OPERATOR_MULTIPLY,
    F2C_OPERATOR_DIVIDE,
    F2C_OPERATOR_POWER,
    F2C_OPERATOR_CONCATENATE,
    F2C_OPERATOR_EQUAL,
    F2C_OPERATOR_NOT_EQUAL,
    F2C_OPERATOR_LESS,
    F2C_OPERATOR_LESS_EQUAL,
    F2C_OPERATOR_GREATER,
    F2C_OPERATOR_GREATER_EQUAL,
    F2C_OPERATOR_NOT,
    F2C_OPERATOR_AND,
    F2C_OPERATOR_OR,
    F2C_OPERATOR_EQUIVALENT,
    F2C_OPERATOR_NOT_EQUIVALENT
} F2cOperator;

struct F2cToken;
F2cOperator f2c_token_operator(const struct F2cToken *token);
int f2c_operator_precedence(F2cOperator operator_kind);
int f2c_operator_is_comparison(F2cOperator operator_kind);
int f2c_operator_is_numeric(F2cOperator operator_kind);
const char *f2c_operator_c_spelling(F2cOperator operator_kind);

#endif
