#ifndef F2C_AST_DECLARATION_BINDINGS_PRIVATE_H
#define F2C_AST_DECLARATION_BINDINGS_PRIVATE_H

#include "ast/declaration/bindings.h"

int f2c_binding_name_append(F2cDeclarationBindingsSyntax *syntax, const F2cToken *token);
int f2c_storage_bindings_syntax(const Line *line, size_t start,
                                F2cDeclarationBindingsSyntax *syntax);

#endif
