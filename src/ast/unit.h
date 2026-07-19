#ifndef F2C_AST_UNIT_H
#define F2C_AST_UNIT_H

#include "frontend/token.h"

typedef enum F2cUnitSyntaxKind {
    F2C_UNIT_SYNTAX_PROGRAM,
    F2C_UNIT_SYNTAX_SUBROUTINE,
    F2C_UNIT_SYNTAX_FUNCTION
} F2cUnitSyntaxKind;

typedef enum F2cUnitHeaderParseStatus {
    F2C_UNIT_HEADER_NOT_MATCHED,
    F2C_UNIT_HEADER_PARSED,
    F2C_UNIT_HEADER_INVALID,
    F2C_UNIT_HEADER_NO_MEMORY
} F2cUnitHeaderParseStatus;

typedef enum F2cUnitHeaderError {
    F2C_UNIT_HEADER_ERROR_NONE,
    F2C_UNIT_HEADER_ERROR_DUPLICATE_PREFIX,
    F2C_UNIT_HEADER_ERROR_MALFORMED_PREFIX,
    F2C_UNIT_HEADER_ERROR_MISSING_NAME,
    F2C_UNIT_HEADER_ERROR_MALFORMED_ARGUMENT_LIST,
    F2C_UNIT_HEADER_ERROR_MALFORMED_ARGUMENT,
    F2C_UNIT_HEADER_ERROR_DUPLICATE_ARGUMENT,
    F2C_UNIT_HEADER_ERROR_ALTERNATE_RETURN,
    F2C_UNIT_HEADER_ERROR_MALFORMED_RESULT,
    F2C_UNIT_HEADER_ERROR_TRAILING_TOKENS
} F2cUnitHeaderError;

typedef struct F2cUnitDummySyntax {
    const F2cToken *token;
    int alternate_return;
} F2cUnitDummySyntax;

typedef struct F2cUnitHeaderSyntax {
    F2cUnitSyntaxKind kind;
    F2cSourceSpan span;
    F2cTokenRange type_spec;
    const F2cToken *name;
    F2cUnitDummySyntax *arguments;
    size_t argument_count;
    const F2cToken *result_name;
    const F2cToken *recursive_prefix;
    const F2cToken *pure_prefix;
    const F2cToken *elemental_prefix;
    const F2cToken *impure_prefix;
    const F2cToken *module_prefix;
    F2cUnitHeaderError error;
    const F2cToken *error_token;
} F2cUnitHeaderSyntax;

F2cUnitHeaderParseStatus f2c_parse_unit_header_syntax(const Line *line,
                                                      F2cUnitHeaderSyntax *syntax);
void f2c_unit_header_syntax_discard(F2cUnitHeaderSyntax *syntax);

#endif
