#ifndef F2C_AST_INTERFACE_HEADER_H
#define F2C_AST_INTERFACE_HEADER_H

#include "ast/declaration/designator.h"

typedef enum F2cInterfaceHeaderStatus {
    F2C_INTERFACE_HEADER_NOT_MATCHED,
    F2C_INTERFACE_HEADER_PARSED,
    F2C_INTERFACE_HEADER_INVALID
} F2cInterfaceHeaderStatus;

typedef enum F2cInterfaceHeaderError {
    F2C_INTERFACE_HEADER_ERROR_NONE,
    F2C_INTERFACE_HEADER_ERROR_GENERIC,
    F2C_INTERFACE_HEADER_ERROR_ABSTRACT_GENERIC,
    F2C_INTERFACE_HEADER_ERROR_TRAILING_TOKEN
} F2cInterfaceHeaderError;

typedef struct F2cInterfaceHeaderSyntax {
    const F2cToken *abstract_keyword;
    const F2cToken *interface_keyword;
    F2cGenericDesignatorSyntax generic;
    int has_generic;
    F2cSourceSpan span;
    F2cInterfaceHeaderError error;
    const F2cToken *error_token;
} F2cInterfaceHeaderSyntax;

typedef struct F2cEndInterfaceSyntax {
    const F2cToken *end_keyword;
    const F2cToken *interface_keyword;
    F2cGenericDesignatorSyntax generic;
    int has_generic;
    F2cSourceSpan span;
    F2cInterfaceHeaderError error;
    const F2cToken *error_token;
} F2cEndInterfaceSyntax;

F2cInterfaceHeaderStatus f2c_parse_interface_header_syntax(const Line *line,
                                                           F2cInterfaceHeaderSyntax *syntax);
F2cInterfaceHeaderStatus f2c_parse_end_interface_syntax(const Line *line,
                                                        F2cEndInterfaceSyntax *syntax);

#endif
