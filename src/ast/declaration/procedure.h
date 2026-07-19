#ifndef F2C_AST_DECLARATION_PROCEDURE_H
#define F2C_AST_DECLARATION_PROCEDURE_H

#include "frontend/token.h"

typedef enum F2cProcedureDeclarationStatus {
    F2C_PROCEDURE_DECLARATION_NOT_MATCHED,
    F2C_PROCEDURE_DECLARATION_PARSED,
    F2C_PROCEDURE_DECLARATION_INVALID,
    F2C_PROCEDURE_DECLARATION_NO_MEMORY
} F2cProcedureDeclarationStatus;

typedef enum F2cProcedureIntentSyntax {
    F2C_PROCEDURE_INTENT_NONE,
    F2C_PROCEDURE_INTENT_IN,
    F2C_PROCEDURE_INTENT_OUT,
    F2C_PROCEDURE_INTENT_INOUT
} F2cProcedureIntentSyntax;

typedef enum F2cProcedureDeclarationError {
    F2C_PROCEDURE_DECLARATION_ERROR_NONE,
    F2C_PROCEDURE_DECLARATION_ERROR_INTERFACE,
    F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE_SEPARATOR,
    F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE,
    F2C_PROCEDURE_DECLARATION_ERROR_UNKNOWN_ATTRIBUTE,
    F2C_PROCEDURE_DECLARATION_ERROR_DUPLICATE_ATTRIBUTE,
    F2C_PROCEDURE_DECLARATION_ERROR_INTENT,
    F2C_PROCEDURE_DECLARATION_ERROR_ENTITY_LIST,
    F2C_PROCEDURE_DECLARATION_ERROR_ENTITY,
    F2C_PROCEDURE_DECLARATION_ERROR_DUPLICATE_ENTITY
} F2cProcedureDeclarationError;

typedef struct F2cProcedureDeclarationSyntax {
    F2cSourceSpan span;
    const F2cToken *interface_name;
    const F2cToken *optional_attribute;
    const F2cToken *pointer_attribute;
    const F2cToken *intent_attribute;
    const F2cToken *nopass_attribute;
    const F2cToken *pass_attribute;
    F2cProcedureIntentSyntax intent;
    const F2cToken **entities;
    size_t entity_count;
    F2cProcedureDeclarationError error;
    const F2cToken *error_token;
} F2cProcedureDeclarationSyntax;

F2cProcedureDeclarationStatus
f2c_parse_procedure_declaration_syntax(const Line *line, F2cProcedureDeclarationSyntax *syntax);
void f2c_procedure_declaration_syntax_discard(F2cProcedureDeclarationSyntax *syntax);

#endif
