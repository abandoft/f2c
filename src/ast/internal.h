#ifndef F2C_EXPRESSION_INTERNAL_H
#define F2C_EXPRESSION_INTERNAL_H

#include "internal/f2c.h"

typedef struct AstParser {
    Unit *unit;
    const char *source;
    const char *cursor;
    const char *error_at;
    const F2cToken *tokens;
    size_t token_count;
    size_t token_index;
    size_t depth;
    F2cToken token;
    const char *double_colon_begin;
    int double_colon_pending;
    int depth_limit_exceeded;
} AstParser;

F2cExpr *f2c_expr_new(F2cExprKind kind, Type type, const char *text, size_t length);
int f2c_expr_push(F2cExpr *parent, F2cExpr *child);
int f2c_ast_push_expression(AstParser *parser, F2cExpr *parent, F2cExpr *child);
int f2c_ast_reserve_expression_nodes(Context *context, const F2cExpr *expression);
void f2c_ast_report_resource_limit(AstParser *parser, const char *message, size_t limit,
                                   int *reported);

void f2c_ast_parser_error(AstParser *parser, const char *at);
void f2c_ast_next_token(AstParser *parser);

void f2c_ast_set_expression_shape(F2cExpr *expression, size_t rank, F2cShapeKind kind);
void f2c_ast_copy_expression_shape(F2cExpr *expression, const F2cShape *source);
int f2c_ast_constructor_extent(Unit *unit, const F2cExpr *expression, uint64_t *extent);
void f2c_ast_set_elemental_shape(F2cExpr *expression, const F2cExpr *left, const F2cExpr *right);
void f2c_ast_set_array_reference_shape(AstParser *parser, F2cExpr *expression, Symbol *symbol);
Type f2c_ast_common_constructor_type(Type left, Type right);
int f2c_ast_precedence(const F2cToken *token);
int f2c_ast_is_comparison(const F2cToken *token);
int f2c_ast_is_defined_operator(const F2cToken *token);
int f2c_ast_token_has_suffix(const F2cToken *token, const char *suffix);
Type f2c_ast_literal_kind_type(AstParser *parser, const F2cToken *token);
int f2c_ast_literal_kind_value(AstParser *parser, const F2cToken *token, Type literal_type);
Type f2c_ast_kind_type_from_argument(const F2cExpr *argument);
int f2c_ast_kind_value_from_argument(const F2cExpr *argument);
int f2c_ast_is_generated_c_intrinsic(const char *name);
const F2cExpr *f2c_ast_intrinsic_argument_value(const F2cExpr *argument);
const F2cExpr *f2c_ast_intrinsic_argument(const F2cExpr *call, const char *keyword,
                                          size_t position);
void f2c_ast_resolve_intrinsic_call(AstParser *parser, F2cExpr *expression);
void f2c_ast_set_transform_intrinsic_shape(AstParser *parser, F2cExpr *expression);
int f2c_ast_common_expression_kind(Type result_type, const F2cExpr *left, const F2cExpr *right);

#endif
