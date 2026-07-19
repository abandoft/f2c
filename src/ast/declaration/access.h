#ifndef F2C_AST_DECLARATION_ACCESS_H
#define F2C_AST_DECLARATION_ACCESS_H

#include "ast/declaration/designator.h"

typedef enum F2cAccessStatementStatus {
    F2C_ACCESS_STATEMENT_NOT_MATCHED,
    F2C_ACCESS_STATEMENT_PARSED,
    F2C_ACCESS_STATEMENT_INVALID,
    F2C_ACCESS_STATEMENT_NO_MEMORY
} F2cAccessStatementStatus;

typedef enum F2cAccessKind { F2C_ACCESS_PUBLIC, F2C_ACCESS_PRIVATE } F2cAccessKind;

typedef enum F2cAccessStatementError {
    F2C_ACCESS_ERROR_NONE,
    F2C_ACCESS_ERROR_EMPTY_LIST,
    F2C_ACCESS_ERROR_ITEM,
    F2C_ACCESS_ERROR_LIST_SEPARATOR,
    F2C_ACCESS_ERROR_DUPLICATE_ITEM,
    F2C_ACCESS_ERROR_TRAILING_COMMA
} F2cAccessStatementError;

typedef struct F2cAccessStatementSyntax {
    F2cSourceSpan span;
    const F2cToken *keyword;
    F2cAccessKind kind;
    const F2cToken *double_colon;
    F2cGenericDesignatorSyntax *items;
    size_t item_count;
    size_t item_capacity;
    F2cAccessStatementError error;
    const F2cToken *error_token;
} F2cAccessStatementSyntax;

int f2c_access_statement_candidate(const Line *line);
F2cAccessStatementStatus f2c_parse_access_statement_syntax(const Line *line,
                                                           F2cAccessStatementSyntax *syntax);
void f2c_access_statement_syntax_discard(F2cAccessStatementSyntax *syntax);

#endif
