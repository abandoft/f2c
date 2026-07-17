#ifndef F2C_AST_STATEMENT_PRIVATE_H
#define F2C_AST_STATEMENT_PRIVATE_H

#include "internal/f2c.h"

void f2c_statement_parse_data(Unit *unit, F2cStatement *statement);
void f2c_statement_parse_label(Unit *unit, F2cStatement *statement);
int f2c_statement_parse_arithmetic_labels(F2cStatement *statement);
char **f2c_statement_split_arguments(const char *text, size_t *count);
int f2c_statement_parse_io_item(Unit *unit, const char *text, F2cIoItem *item);
char *f2c_statement_matching_parenthesis(char *open);
F2cExpr *f2c_statement_parse_parenthesized(Unit *unit, char *text, char **tail);

#endif
