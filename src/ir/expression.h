#ifndef F2C_IR_EXPRESSION_H
#define F2C_IR_EXPRESSION_H

#include "frontend/token.h"
#include "semantic/model.h"

typedef enum F2cExprKind {
    F2C_EXPR_INVALID,
    F2C_EXPR_INTEGER_LITERAL,
    F2C_EXPR_REAL_LITERAL,
    F2C_EXPR_STRING_LITERAL,
    F2C_EXPR_LOGICAL_LITERAL,
    F2C_EXPR_NAME,
    F2C_EXPR_UNARY,
    F2C_EXPR_BINARY,
    F2C_EXPR_CALL,
    F2C_EXPR_ARRAY_REFERENCE,
    F2C_EXPR_ARRAY_SECTION,
    F2C_EXPR_ARRAY_CONSTRUCTOR,
    F2C_EXPR_IMPLIED_DO,
    F2C_EXPR_KEYWORD_ARGUMENT,
    F2C_EXPR_ABSENT_ARGUMENT,
    F2C_EXPR_SUBSTRING,
    F2C_EXPR_COMPLEX_LITERAL,
    F2C_EXPR_COMPONENT,
    F2C_EXPR_STRUCTURE_CONSTRUCTOR
} F2cExprKind;

struct F2cExpr {
    F2cExprKind kind;
    Type type;
    int type_kind;
    size_t rank;
    int definable;
    F2cValueCategory value_category;
    F2cShape shape;
    F2cSourceSpan span;
    F2cSourceSpan parse_error_span;
    char *text;
    char *source;
    char *lowered_c;
    size_t source_offset;
    size_t source_length;
    size_t parse_error_offset;
    Symbol *symbol;
    F2cDerivedType *derived_type;
    size_t temporary_index;
    size_t statement_temporary_index;
    size_t statement_nested_temporary_begin;
    F2cExpr **children;
    size_t child_count;
    size_t child_capacity;
    size_t tree_depth;
};

typedef void (*F2cExpressionVisitor)(F2cExpr *expression, void *state);

F2cExpr *f2c_parse_expression_ast(Unit *unit, const char *expression, const char **error_at);
F2cExpr *f2c_parse_expression_tokens(Unit *unit, const F2cToken *tokens, size_t token_count,
                                     const char *source, const char **error_at);
F2cExpr *f2c_expr_new_absent(Type type, size_t rank);
void f2c_expr_free(F2cExpr *expression);
Type f2c_expression_type(Unit *unit, const char *expression);
int f2c_expression_is_designator(Unit *unit, const char *expression);
void f2c_visit_expression(F2cExpr *expression, F2cExpressionVisitor visitor, void *state);

#endif
