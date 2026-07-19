#ifndef F2C_AST_DECLARATION_MODULE_PROCEDURE_H
#define F2C_AST_DECLARATION_MODULE_PROCEDURE_H

#include "internal/f2c.h"

typedef enum F2cModuleProcedureStatementStatus {
    F2C_MODULE_PROCEDURE_NOT_MATCHED,
    F2C_MODULE_PROCEDURE_PARSED,
    F2C_MODULE_PROCEDURE_INVALID,
    F2C_MODULE_PROCEDURE_NO_MEMORY
} F2cModuleProcedureStatementStatus;

typedef enum F2cModuleProcedureStatementError {
    F2C_MODULE_PROCEDURE_ERROR_NONE,
    F2C_MODULE_PROCEDURE_ERROR_EMPTY_LIST,
    F2C_MODULE_PROCEDURE_ERROR_NAME,
    F2C_MODULE_PROCEDURE_ERROR_SEPARATOR,
    F2C_MODULE_PROCEDURE_ERROR_DUPLICATE_NAME,
    F2C_MODULE_PROCEDURE_ERROR_TRAILING_COMMA
} F2cModuleProcedureStatementError;

typedef struct F2cModuleProcedureStatementSyntax {
    const F2cToken *module_keyword;
    const F2cToken *procedure_keyword;
    const F2cToken *double_colon;
    const F2cToken **names;
    size_t name_count;
    size_t name_capacity;
    F2cSourceSpan span;
    F2cModuleProcedureStatementError error;
    const F2cToken *error_token;
} F2cModuleProcedureStatementSyntax;

F2cModuleProcedureStatementStatus
f2c_parse_module_procedure_statement_syntax(const Line *line,
                                            F2cModuleProcedureStatementSyntax *syntax);
void f2c_module_procedure_statement_syntax_discard(F2cModuleProcedureStatementSyntax *syntax);

#endif
