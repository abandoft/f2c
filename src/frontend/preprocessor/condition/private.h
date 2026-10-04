#ifndef F2C_FRONTEND_PREPROCESSOR_CONDITION_PRIVATE_H
#define F2C_FRONTEND_PREPROCESSOR_CONDITION_PRIVATE_H

#include "frontend/preprocessor/private.h"

#include <ctype.h>
#include <string.h>

static inline int condition_identifier_start(char value) {
    return isalpha((unsigned char)value) != 0 || value == '_';
}

static inline int condition_identifier_continue(char value) {
    return isalnum((unsigned char)value) != 0 || value == '_';
}

static inline int condition_word_equal(const char *begin, size_t length, const char *word) {
    return strlen(word) == length && strncmp(begin, word, length) == 0;
}

void f2c_preprocessor_condition_diagnose(Preprocessor *preprocessor, F2cDiagnosticCode code,
                                         size_t line, size_t column, const char *message);
int f2c_preprocessor_expand_condition(Preprocessor *preprocessor, const char *text, size_t length,
                                      size_t line, size_t column, Buffer *output);

typedef struct ExpressionParser {
    Preprocessor *preprocessor;
    const char *text;
    const char *cursor;
    size_t line;
    size_t column;
    int failed;
} ExpressionParser;

typedef struct IntegerValue {
    uint64_t unsigned_value;
    int64_t signed_value;
    int is_unsigned;
} IntegerValue;

static inline IntegerValue signed_integer(int64_t value) {
    IntegerValue result = {(uint64_t)value, value, 0};
    return result;
}

static inline IntegerValue unsigned_integer(uint64_t value) {
    IntegerValue result = {value, 0, 1};
    return result;
}

static inline uint64_t integer_unsigned(IntegerValue value) {
    return value.is_unsigned ? value.unsigned_value : (uint64_t)value.signed_value;
}

static inline int64_t signed_from_bits(uint64_t value) {
    if (value <= (uint64_t)INT64_MAX)
        return (int64_t)value;
    return -1 - (int64_t)(UINT64_MAX - value);
}

static inline int integer_true(IntegerValue value) {
    return value.is_unsigned ? value.unsigned_value != 0U : value.signed_value != 0;
}

static inline IntegerValue common_zero(IntegerValue left, IntegerValue right) {
    return left.is_unsigned || right.is_unsigned ? unsigned_integer(0U) : signed_integer(0);
}

typedef enum ConditionOperator {
    CONDITION_NONE,
    CONDITION_ADD,
    CONDITION_SUBTRACT,
    CONDITION_MULTIPLY,
    CONDITION_DIVIDE,
    CONDITION_REMAINDER,
    CONDITION_LEFT_SHIFT,
    CONDITION_RIGHT_SHIFT,
    CONDITION_LESS,
    CONDITION_LESS_EQUAL,
    CONDITION_GREATER,
    CONDITION_GREATER_EQUAL,
    CONDITION_EQUAL,
    CONDITION_NOT_EQUAL,
    CONDITION_BIT_AND,
    CONDITION_BIT_XOR,
    CONDITION_BIT_OR,
    CONDITION_LOGICAL_AND,
    CONDITION_LOGICAL_OR,
    CONDITION_LOGICAL_NOT,
    CONDITION_BIT_NOT
} ConditionOperator;

void f2c_condition_error_code(ExpressionParser *parser, F2cDiagnosticCode code, const char *at,
                              const char *message);
static inline void expression_error_at(ExpressionParser *parser, const char *at,
                                       const char *message) {
    f2c_condition_error_code(parser, F2C_DIAGNOSTIC_SYNTAX, at, message);
}
static inline void expression_error(ExpressionParser *parser, const char *message) {
    expression_error_at(parser, parser->cursor, message);
}
static inline void expression_space(ExpressionParser *parser) {
    while (isspace((unsigned char)*parser->cursor))
        ++parser->cursor;
}
static inline int expression_consume(ExpressionParser *parser, const char *token) {
    expression_space(parser);
    const size_t length = strlen(token);
    if (strncmp(parser->cursor, token, length) != 0)
        return 0;
    parser->cursor += length;
    return 1;
}
IntegerValue f2c_condition_atom(ExpressionParser *parser, int evaluate);
IntegerValue f2c_condition_unary(ExpressionParser *parser, ConditionOperator operation,
                                 IntegerValue value, int evaluate, const char *at);
IntegerValue f2c_condition_binary(ExpressionParser *parser, ConditionOperator operation,
                                  IntegerValue left, IntegerValue right, int evaluate,
                                  const char *at);

#endif
