#ifndef F2C_AST_DECLARATION_USE_H
#define F2C_AST_DECLARATION_USE_H

#include "frontend/token.h"

typedef enum F2cUseStatementStatus {
    F2C_USE_STATEMENT_NOT_MATCHED,
    F2C_USE_STATEMENT_PARSED,
    F2C_USE_STATEMENT_INVALID,
    F2C_USE_STATEMENT_NO_MEMORY
} F2cUseStatementStatus;

typedef enum F2cUseModuleNature {
    F2C_USE_NATURE_UNSPECIFIED,
    F2C_USE_NATURE_INTRINSIC,
    F2C_USE_NATURE_NON_INTRINSIC
} F2cUseModuleNature;

typedef enum F2cUseDesignatorKind {
    F2C_USE_DESIGNATOR_NAME,
    F2C_USE_DESIGNATOR_OPERATOR,
    F2C_USE_DESIGNATOR_ASSIGNMENT,
    F2C_USE_DESIGNATOR_DEFINED_IO
} F2cUseDesignatorKind;

typedef struct F2cUseDesignatorSyntax {
    F2cUseDesignatorKind kind;
    F2cTokenRange range;
    F2cSourceSpan span;
    const F2cToken *name;
} F2cUseDesignatorSyntax;

typedef struct F2cUseAssociationSyntax {
    F2cSourceSpan span;
    F2cUseDesignatorSyntax local;
    F2cUseDesignatorSyntax remote;
    int renamed;
} F2cUseAssociationSyntax;

typedef enum F2cUseStatementError {
    F2C_USE_ERROR_NONE,
    F2C_USE_ERROR_MODULE_NATURE,
    F2C_USE_ERROR_DOUBLE_COLON,
    F2C_USE_ERROR_MODULE_NAME,
    F2C_USE_ERROR_LIST_SEPARATOR,
    F2C_USE_ERROR_ONLY_COLON,
    F2C_USE_ERROR_ITEM,
    F2C_USE_ERROR_RENAME_REQUIRED,
    F2C_USE_ERROR_RENAME_TARGET,
    F2C_USE_ERROR_RENAME_KIND,
    F2C_USE_ERROR_DUPLICATE_LOCAL_NAME,
    F2C_USE_ERROR_TRAILING_COMMA
} F2cUseStatementError;

typedef struct F2cUseStatementSyntax {
    F2cSourceSpan span;
    const F2cToken *keyword;
    F2cUseModuleNature nature;
    const F2cToken *nature_token;
    const F2cToken *module_name;
    const F2cToken *only_token;
    F2cUseAssociationSyntax *items;
    size_t item_count;
    size_t item_capacity;
    F2cUseStatementError error;
    const F2cToken *error_token;
} F2cUseStatementSyntax;

F2cUseStatementStatus f2c_parse_use_statement_syntax(const Line *line,
                                                     F2cUseStatementSyntax *syntax);
int f2c_use_statement_candidate(const Line *line);
void f2c_use_statement_syntax_discard(F2cUseStatementSyntax *syntax);

#endif
