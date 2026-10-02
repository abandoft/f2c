#ifndef F2C_AST_DECLARATION_BINDINGS_H
#define F2C_AST_DECLARATION_BINDINGS_H

#include "frontend/token.h"

typedef struct F2cDeclarationBindingsSyntax {
    const F2cToken **names;
    size_t count;
    size_t capacity;
} F2cDeclarationBindingsSyntax;

/* Discover declaration bindings without resolving types or specification expressions.
 * Full declaration validation remains in the declaration's syntax/lowering path. */
int f2c_parse_declaration_bindings_syntax(const Line *line, F2cDeclarationBindingsSyntax *syntax);
void f2c_declaration_bindings_syntax_discard(F2cDeclarationBindingsSyntax *syntax);

#endif
