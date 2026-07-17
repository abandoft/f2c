#include "ast/internal.h"

int f2c_ast_push_expression(AstParser *parser, F2cExpr *parent, F2cExpr *child) {
    Context *context = parser->unit != NULL ? parser->unit->context : NULL;
    if (context != NULL && context->limits.max_parse_depth != 0U && child != NULL &&
        child->tree_depth >= context->limits.max_parse_depth) {
        parser->depth_limit_exceeded = 1;
        f2c_ast_parser_error(parser, parser->token.begin);
        return 0;
    }
    return f2c_expr_push(parent, child);
}

int f2c_ast_reserve_expression_nodes(Context *context, const F2cExpr *expression) {
    size_t index;
    if (expression == NULL)
        return 1;
    if (context->limits.max_ast_nodes != 0U &&
        context->ast_node_count >= context->limits.max_ast_nodes)
        return 0;
    ++context->ast_node_count;
    for (index = 0U; index < expression->child_count; ++index) {
        if (!f2c_ast_reserve_expression_nodes(context, expression->children[index]))
            return 0;
    }
    return 1;
}

void f2c_ast_report_resource_limit(AstParser *parser, const char *message, size_t limit,
                                   int *reported) {
    Context *context = parser->unit != NULL ? parser->unit->context : NULL;
    size_t line;
    size_t column;
    if (context == NULL || *reported)
        return;
    line = parser->token.line != 0U ? parser->token.line : 1U;
    column = parser->token.column != 0U ? parser->token.column : 1U;
    *reported = 1;
    f2c_diagnostic_at_code(context, F2C_DIAGNOSTIC_RESOURCE_LIMIT, line, column, 1, message, limit);
}
