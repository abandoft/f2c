#ifndef F2C_AST_STATEMENT_PRIVATE_H
#define F2C_AST_STATEMENT_PRIVATE_H

#include "internal/f2c.h"

int f2c_statement_parse_data(Unit *unit, const Line *line, size_t body_start,
                             F2cStatement *statement);
void f2c_statement_free_data_group(F2cDataGroup *group);
void f2c_statement_parse_label(Unit *unit, const Line *line, F2cStatement *statement);
char *f2c_statement_copy_label_token(const F2cToken *token);
int f2c_statement_parse_control(Unit *unit, const Line *line, size_t body_start,
                                F2cStatement *statement);
int f2c_statement_parse_nested_tokens(Unit *unit, const Line *line, size_t begin,
                                      F2cStatement **statement);
char **f2c_statement_split_arguments(const char *text, size_t *count);
void f2c_statement_free_io_item(F2cIoItem *item);
int f2c_statement_parse_io_item_tokens(Unit *unit, F2cTokenRange range, F2cIoItem *item);
int f2c_statement_parse_io(Unit *unit, const Line *line, size_t body_start,
                           F2cStatement *statement);
int f2c_statement_parse_print(Unit *unit, const Line *line, size_t body_start,
                              F2cStatement *statement);
char *f2c_statement_matching_parenthesis(char *open);
F2cExpr *f2c_statement_parse_parenthesized_tokens(Unit *unit, const Line *line, size_t begin,
                                                  char **tail);
int f2c_statement_parse_case(Unit *unit, const Line *line, F2cStatement *statement);
int f2c_statement_parse_construct_syntax(const Line *line, size_t body_start,
                                         F2cStatement *statement);
int f2c_statement_tokenize_transient(const char *text, size_t line_number, Line *line);
void f2c_statement_release_transient(Line *line);

#endif
