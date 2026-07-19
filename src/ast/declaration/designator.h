#ifndef F2C_AST_DECLARATION_DESIGNATOR_H
#define F2C_AST_DECLARATION_DESIGNATOR_H

#include "frontend/token.h"

typedef enum F2cGenericDesignatorKind {
    F2C_GENERIC_DESIGNATOR_NAME,
    F2C_GENERIC_DESIGNATOR_OPERATOR,
    F2C_GENERIC_DESIGNATOR_ASSIGNMENT,
    F2C_GENERIC_DESIGNATOR_DEFINED_IO
} F2cGenericDesignatorKind;

typedef enum F2cGenericDesignatorStatus {
    F2C_GENERIC_DESIGNATOR_NOT_MATCHED,
    F2C_GENERIC_DESIGNATOR_PARSED,
    F2C_GENERIC_DESIGNATOR_INVALID
} F2cGenericDesignatorStatus;

typedef struct F2cGenericDesignatorSyntax {
    F2cGenericDesignatorKind kind;
    F2cTokenRange range;
    F2cSourceSpan span;
    const F2cToken *name;
} F2cGenericDesignatorSyntax;

F2cGenericDesignatorStatus
f2c_parse_generic_designator_syntax(const Line *line, size_t begin, size_t *end,
                                    F2cGenericDesignatorSyntax *designator);
int f2c_generic_designators_equal(const F2cGenericDesignatorSyntax *left,
                                  const F2cGenericDesignatorSyntax *right);
char *f2c_generic_designator_key(const F2cGenericDesignatorSyntax *designator);

#endif
