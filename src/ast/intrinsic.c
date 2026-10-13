#include "ast/internal.h"

#include "semantic/intrinsic/result.h"
#include "semantic/operator.h"

#include <string.h>

static void resolve_generated_call(F2cExpr *expression) {
    const F2cExpr *first = expression->child_count != 0U
                               ? f2c_ast_intrinsic_argument_value(expression->children[0])
                               : NULL;
    const Type primary = first != NULL ? first->type : TYPE_UNKNOWN;
    expression->type = f2c_generated_intrinsic_type(expression->text, primary);
    expression->type_kind = first != NULL && first->type == expression->type
                                ? first->type_kind
                                : f2c_default_kind(expression->type);
    if ((strcmp(expression->text, "F2C_FORTRAN_MAX") == 0 ||
         strcmp(expression->text, "F2C_FORTRAN_MIN") == 0) &&
        first != NULL) {
        F2cScalarType common = f2c_expression_scalar_type(first);
        for (size_t index = 1U; index < expression->child_count; ++index) {
            F2cOperatorTyping typing;
            const F2cExpr *argument = f2c_ast_intrinsic_argument_value(expression->children[index]);
            if (f2c_operator_typing(F2C_OPERATOR_ADD, 0, common,
                                    f2c_expression_scalar_type(argument),
                                    &typing) != F2C_OPERATOR_VALID)
                break;
            common = typing.result;
        }
        expression->type = common.type;
        expression->type_kind = common.kind;
    }
    size_t rank = 0U;
    for (size_t index = 0U; index < expression->child_count; ++index) {
        const F2cExpr *argument = f2c_ast_intrinsic_argument_value(expression->children[index]);
        if (argument != NULL && argument->rank > rank)
            rank = argument->rank;
    }
    f2c_ast_set_expression_shape(expression, rank, F2C_SHAPE_EXPRESSION);
    for (size_t index = 0U; index < expression->child_count && rank != 0U; ++index) {
        const F2cExpr *argument = f2c_ast_intrinsic_argument_value(expression->children[index]);
        if (argument != NULL && argument->shape.rank == rank) {
            f2c_ast_copy_expression_shape(expression, &argument->shape);
            break;
        }
    }
}

void f2c_ast_resolve_intrinsic_call(AstParser *parser, F2cExpr *expression) {
    if (f2c_find_intrinsic(expression->text) != NULL)
        f2c_intrinsic_refresh_result(parser != NULL ? parser->unit : NULL, expression);
    else
        resolve_generated_call(expression);
}
