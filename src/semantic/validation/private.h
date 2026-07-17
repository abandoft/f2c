#ifndef F2C_SEMANTIC_VALIDATION_PRIVATE_H
#define F2C_SEMANTIC_VALIDATION_PRIVATE_H

#include "internal/f2c.h"

const char *f2c_validation_unit_line(const Context *context, const Unit *unit, size_t line);
size_t f2c_validation_expression_column(const char *statement_text, const F2cExpr *expression);
size_t f2c_validation_expression_start_column(const char *statement_text,
                                              const F2cExpr *expression);
void f2c_validation_report_parse_error(Context *context, size_t line, const char *statement_text,
                                       const F2cExpr *expression, const char *role);
void f2c_validation_constructor(Context *context, Unit *unit, size_t line,
                                const char *statement_text, const F2cExpr *expression);
int f2c_validation_type_compatible(Type target, Type source);
int f2c_validation_shapes_mismatch(const F2cExpr *left, const F2cExpr *right,
                                   size_t *dimension_out);
int f2c_validation_symbol_shape_mismatch(const Symbol *left, const F2cExpr *right,
                                         size_t *dimension_out);
void f2c_validation_intrinsic_assignment(Context *context, const F2cStatement *statement);
void f2c_validation_constructor_assignment(Context *context, Unit *unit,
                                           const F2cStatement *statement);
void f2c_validation_io_item(Context *context, size_t line, const char *statement_text,
                            const F2cIoItem *item);
void f2c_validation_io_statement(Context *context, Unit *unit, F2cStatement *statement);
const char *f2c_validation_type_name(Type type);
Unit *f2c_validation_find_procedure(Context *context, Unit *caller, const char *name);
const F2cExpr *f2c_validation_actual_value(const F2cExpr *actual);
size_t f2c_validation_call_column(const char *statement_text, const char *name);
size_t f2c_validation_keyword_column(const char *statement_text, const F2cExpr *argument);
int f2c_validation_procedure_signatures_compatible(const Symbol *expected, const Symbol *actual,
                                                   unsigned int depth);
Unit *f2c_validation_procedure_call(Context *context, Unit *caller, size_t line,
                                    const char *statement_text, const char *name,
                                    F2cExpr ***arguments, char ***argument_texts,
                                    size_t *argument_count, int subroutine);
void f2c_validation_expression_calls(Context *context, Unit *unit, size_t line,
                                     const char *statement_text, F2cExpr *expression);
void f2c_validation_io_item_calls(Context *context, Unit *unit, size_t line,
                                  const char *statement_text, F2cIoItem *item);
void f2c_validation_allocation(Context *context, Unit *unit, F2cStatement *statement);
void f2c_validation_move_alloc(Context *context, Unit *unit, F2cStatement *statement);

#endif
