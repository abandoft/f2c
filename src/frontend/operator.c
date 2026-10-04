#include "frontend/operator.h"
#include "frontend/token.h"

#include <ctype.h>

typedef struct OperatorSpelling {
    const char *text;
    F2cOperator kind;
} OperatorSpelling;

static const OperatorSpelling spellings[] = {
    {"+", F2C_OPERATOR_ADD},
    {"-", F2C_OPERATOR_SUBTRACT},
    {"*", F2C_OPERATOR_MULTIPLY},
    {"/", F2C_OPERATOR_DIVIDE},
    {"**", F2C_OPERATOR_POWER},
    {"//", F2C_OPERATOR_CONCATENATE},
    {"==", F2C_OPERATOR_EQUAL},
    {".eq.", F2C_OPERATOR_EQUAL},
    {"/=", F2C_OPERATOR_NOT_EQUAL},
    {".ne.", F2C_OPERATOR_NOT_EQUAL},
    {"<", F2C_OPERATOR_LESS},
    {".lt.", F2C_OPERATOR_LESS},
    {"<=", F2C_OPERATOR_LESS_EQUAL},
    {".le.", F2C_OPERATOR_LESS_EQUAL},
    {">", F2C_OPERATOR_GREATER},
    {".gt.", F2C_OPERATOR_GREATER},
    {">=", F2C_OPERATOR_GREATER_EQUAL},
    {".ge.", F2C_OPERATOR_GREATER_EQUAL},
    {".not.", F2C_OPERATOR_NOT},
    {".and.", F2C_OPERATOR_AND},
    {".or.", F2C_OPERATOR_OR},
    {".eqv.", F2C_OPERATOR_EQUIVALENT},
    {".neqv.", F2C_OPERATOR_NOT_EQUIVALENT},
};

F2cOperator f2c_token_operator(const F2cToken *token) {
    size_t index;
    size_t letters = 0U;
    if (token == NULL || token->kind != F2C_TOKEN_OPERATOR || token->begin == NULL)
        return F2C_OPERATOR_NONE;
    for (index = 0U; index < sizeof(spellings) / sizeof(spellings[0]); ++index)
        if (f2c_token_equals(token, spellings[index].text))
            return spellings[index].kind;
    if (f2c_token_logical_literal(token) != 0 || token->length < 3U || token->begin[0] != '.' ||
        token->begin[token->length - 1U] != '.')
        return F2C_OPERATOR_NONE;
    for (index = 1U; index + 1U < token->length; ++index) {
        const unsigned char character = (unsigned char)token->begin[index];
        if (isspace(character))
            continue;
        if (!isalpha(character) || ++letters > 63U)
            return F2C_OPERATOR_NONE;
    }
    return letters != 0U ? F2C_OPERATOR_DEFINED : F2C_OPERATOR_NONE;
}

int f2c_operator_precedence(F2cOperator operator_kind) {
    switch (operator_kind) {
    case F2C_OPERATOR_DEFINED:
        return 1;
    case F2C_OPERATOR_EQUIVALENT:
    case F2C_OPERATOR_NOT_EQUIVALENT:
        return 2;
    case F2C_OPERATOR_OR:
        return 3;
    case F2C_OPERATOR_AND:
        return 4;
    case F2C_OPERATOR_EQUAL:
    case F2C_OPERATOR_NOT_EQUAL:
    case F2C_OPERATOR_LESS:
    case F2C_OPERATOR_LESS_EQUAL:
    case F2C_OPERATOR_GREATER:
    case F2C_OPERATOR_GREATER_EQUAL:
        return 5;
    case F2C_OPERATOR_CONCATENATE:
        return 6;
    case F2C_OPERATOR_ADD:
    case F2C_OPERATOR_SUBTRACT:
        return 7;
    case F2C_OPERATOR_MULTIPLY:
    case F2C_OPERATOR_DIVIDE:
        return 8;
    case F2C_OPERATOR_POWER:
        return 9;
    default:
        return 0;
    }
}

int f2c_operator_is_comparison(F2cOperator operator_kind) {
    return operator_kind >= F2C_OPERATOR_EQUAL && operator_kind <= F2C_OPERATOR_GREATER_EQUAL;
}

int f2c_operator_is_numeric(F2cOperator operator_kind) {
    return operator_kind >= F2C_OPERATOR_ADD && operator_kind <= F2C_OPERATOR_POWER;
}

const char *f2c_operator_c_spelling(F2cOperator operator_kind) {
    switch (operator_kind) {
    case F2C_OPERATOR_ADD:
        return "+";
    case F2C_OPERATOR_SUBTRACT:
        return "-";
    case F2C_OPERATOR_MULTIPLY:
        return "*";
    case F2C_OPERATOR_DIVIDE:
        return "/";
    case F2C_OPERATOR_EQUAL:
    case F2C_OPERATOR_EQUIVALENT:
        return "==";
    case F2C_OPERATOR_NOT_EQUAL:
    case F2C_OPERATOR_NOT_EQUIVALENT:
        return "!=";
    case F2C_OPERATOR_LESS:
        return "<";
    case F2C_OPERATOR_LESS_EQUAL:
        return "<=";
    case F2C_OPERATOR_GREATER:
        return ">";
    case F2C_OPERATOR_GREATER_EQUAL:
        return ">=";
    case F2C_OPERATOR_NOT:
        return "!";
    case F2C_OPERATOR_AND:
        return "&&";
    case F2C_OPERATOR_OR:
        return "||";
    default:
        return NULL;
    }
}
