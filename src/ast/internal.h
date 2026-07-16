#ifndef F2C_EXPRESSION_INTERNAL_H
#define F2C_EXPRESSION_INTERNAL_H

#include "internal/f2c.h"

typedef enum AstTokenKind {
    AST_TOKEN_END,
    AST_TOKEN_NAME,
    AST_TOKEN_NUMBER,
    AST_TOKEN_STRING,
    AST_TOKEN_HOLLERITH,
    AST_TOKEN_BOZ,
    AST_TOKEN_LEFT_PAREN,
    AST_TOKEN_RIGHT_PAREN,
    AST_TOKEN_ARRAY_BEGIN,
    AST_TOKEN_ARRAY_END,
    AST_TOKEN_COMMA,
    AST_TOKEN_COLON,
    AST_TOKEN_PERCENT,
    AST_TOKEN_OPERATOR,
    AST_TOKEN_INVALID
} AstTokenKind;

typedef struct AstToken {
    AstTokenKind kind;
    const char *begin;
    size_t length;
    size_t line;
    size_t column;
} AstToken;

typedef struct AstParser {
    Unit *unit;
    const char *source;
    const char *cursor;
    const char *error_at;
    AstToken token;
} AstParser;

F2cExpr *f2c_expr_new(F2cExprKind kind, Type type, const char *text, size_t length);
int f2c_expr_push(F2cExpr *parent, F2cExpr *child);

int f2c_ast_token_equals(const AstToken *token, const char *text);
void f2c_ast_parser_error(AstParser *parser, const char *at);
void f2c_ast_next_token(AstParser *parser);

#endif
