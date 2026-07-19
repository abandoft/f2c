#ifndef F2C_AST_INTERFACE_SPECIFIC_H
#define F2C_AST_INTERFACE_SPECIFIC_H

#include "internal/f2c.h"

typedef enum F2cInterfaceSpecificStatus {
    F2C_INTERFACE_SPECIFIC_NOT_MATCHED,
    F2C_INTERFACE_SPECIFIC_PARSED,
    F2C_INTERFACE_SPECIFIC_INVALID,
    F2C_INTERFACE_SPECIFIC_NO_MEMORY
} F2cInterfaceSpecificStatus;

typedef enum F2cInterfaceSpecificError {
    F2C_INTERFACE_SPECIFIC_ERROR_NONE,
    F2C_INTERFACE_SPECIFIC_ERROR_EMPTY_LIST,
    F2C_INTERFACE_SPECIFIC_ERROR_NAME,
    F2C_INTERFACE_SPECIFIC_ERROR_SEPARATOR,
    F2C_INTERFACE_SPECIFIC_ERROR_DUPLICATE_NAME,
    F2C_INTERFACE_SPECIFIC_ERROR_TRAILING_COMMA
} F2cInterfaceSpecificError;

typedef struct F2cInterfaceSpecificSyntax {
    const F2cToken *module_keyword;
    const F2cToken *procedure_keyword;
    const F2cToken *double_colon;
    const F2cToken **names;
    size_t name_count;
    size_t name_capacity;
    F2cSourceSpan span;
    F2cInterfaceSpecificError error;
    const F2cToken *error_token;
} F2cInterfaceSpecificSyntax;

F2cInterfaceSpecificStatus f2c_parse_interface_specific_syntax(const Line *line,
                                                               F2cInterfaceSpecificSyntax *syntax);
void f2c_interface_specific_syntax_discard(F2cInterfaceSpecificSyntax *syntax);

#endif
