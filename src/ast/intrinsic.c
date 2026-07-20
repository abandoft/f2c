#include "ast/internal.h"

#include <stdlib.h>
#include <string.h>

static int matmul_result_kind(const F2cExpr *expression) {
    int result = 0;
    size_t argument;
    if (expression == NULL)
        return f2c_default_kind(TYPE_UNKNOWN);
    for (argument = 0U; argument < expression->child_count; ++argument) {
        const F2cExpr *value = f2c_ast_intrinsic_argument_value(expression->children[argument]);
        int contributes = 0;
        if (value == NULL)
            continue;
        if (expression->type == TYPE_INTEGER)
            contributes = value->type == TYPE_INTEGER;
        else if (expression->type == TYPE_LOGICAL)
            contributes = value->type == TYPE_LOGICAL;
        else if (expression->type == TYPE_REAL || expression->type == TYPE_DOUBLE)
            contributes = value->type == TYPE_REAL || value->type == TYPE_DOUBLE;
        else if (expression->type == TYPE_COMPLEX || expression->type == TYPE_DOUBLE_COMPLEX)
            contributes = value->type == TYPE_REAL || value->type == TYPE_DOUBLE ||
                          value->type == TYPE_COMPLEX || value->type == TYPE_DOUBLE_COMPLEX;
        if (contributes && value->type_kind > result)
            result = value->type_kind;
    }
    return result != 0 ? result : f2c_default_kind(expression->type);
}

static void resolve_intrinsic_type(F2cExpr *expression, const F2cIntrinsicSignature *signature) {
    Type *argument_types = expression->child_count != 0U
                               ? (Type *)malloc(expression->child_count * sizeof(*argument_types))
                               : NULL;
    size_t argument;
    for (argument = 0U; argument < expression->child_count && argument_types != NULL; ++argument)
        argument_types[argument] = expression->children[argument]->type;
    expression->type =
        argument_types != NULL || expression->child_count == 0U
            ? f2c_resolve_intrinsic_type(expression->text, argument_types, expression->child_count)
            : TYPE_UNKNOWN;
    free(argument_types);

    if (signature != NULL && f2c_intrinsic_is_real_representation(signature->id)) {
        const F2cExpr *model =
            f2c_intrinsic_argument(expression->children, expression->child_count, "x", 0U);
        if (model != NULL)
            expression->type = signature->id == F2C_INTRINSIC_EXPONENT ? TYPE_INTEGER : model->type;
    }
    if (strcmp(expression->text, "real") == 0 && expression->child_count >= 2U) {
        const Type kind_type = f2c_ast_kind_type_from_argument(expression->children[1]);
        if (kind_type == TYPE_REAL || kind_type == TYPE_DOUBLE)
            expression->type = kind_type;
    } else if (strcmp(expression->text, "cmplx") == 0 && expression->child_count >= 3U) {
        const Type kind_type = f2c_ast_kind_type_from_argument(expression->children[2]);
        if (kind_type == TYPE_REAL)
            expression->type = TYPE_COMPLEX;
        else if (kind_type == TYPE_DOUBLE)
            expression->type = TYPE_DOUBLE_COMPLEX;
    }
}

static void resolve_intrinsic_shape(AstParser *parser, F2cExpr *expression) {
    size_t argument;
    expression->rank = f2c_is_intrinsic_name(expression->text)
                           ? f2c_resolve_intrinsic_rank(expression->text, expression->children,
                                                        expression->child_count)
                           : 0U;
    if (!f2c_is_intrinsic_name(expression->text)) {
        for (argument = 0U; argument < expression->child_count; ++argument) {
            if (expression->children[argument]->rank > expression->rank)
                expression->rank = expression->children[argument]->rank;
        }
    }
    f2c_ast_set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
    if (expression->rank != 0U) {
        for (argument = 0U; argument < expression->child_count; ++argument) {
            if (expression->children[argument]->rank == expression->rank) {
                f2c_ast_copy_expression_shape(expression, &expression->children[argument]->shape);
                break;
            }
        }
    }
    if (strcmp(expression->text, "transpose") == 0 && expression->child_count == 1U &&
        expression->children[0]->shape.rank == 2U) {
        const F2cShape source_shape = expression->children[0]->shape;
        expression->shape.rank = 2U;
        expression->shape.kind = F2C_SHAPE_EXPRESSION;
        expression->shape.dimensions[0] = source_shape.dimensions[1];
        expression->shape.dimensions[1] = source_shape.dimensions[0];
    } else if (strcmp(expression->text, "matmul") == 0 && expression->child_count == 2U) {
        const F2cExpr *left = f2c_ast_intrinsic_argument_value(expression->children[0]);
        const F2cExpr *right = f2c_ast_intrinsic_argument_value(expression->children[1]);
        expression->shape.kind = F2C_SHAPE_EXPRESSION;
        expression->shape.rank = expression->rank;
        if (expression->rank == 2U) {
            if (left != NULL && left->rank == 2U)
                expression->shape.dimensions[0] = left->shape.dimensions[0];
            if (right != NULL && right->rank == 2U)
                expression->shape.dimensions[1] = right->shape.dimensions[1];
        } else if (expression->rank == 1U) {
            if (left != NULL && left->rank == 2U)
                expression->shape.dimensions[0] = left->shape.dimensions[0];
            else if (right != NULL && right->rank == 2U)
                expression->shape.dimensions[0] = right->shape.dimensions[1];
        }
    }
    if (strcmp(expression->text, "pack") == 0 || strcmp(expression->text, "unpack") == 0 ||
        strcmp(expression->text, "reshape") == 0 || strcmp(expression->text, "spread") == 0 ||
        strcmp(expression->text, "findloc") == 0 || strcmp(expression->text, "shape") == 0 ||
        strcmp(expression->text, "lbound") == 0 || strcmp(expression->text, "ubound") == 0)
        f2c_ast_set_transform_intrinsic_shape(parser, expression);
}

static void resolve_intrinsic_kind(F2cExpr *expression, const F2cIntrinsicSignature *signature) {
    expression->type_kind = f2c_default_kind(expression->type);
    if (expression->child_count != 0U) {
        const F2cExpr *first_argument = f2c_ast_intrinsic_argument_value(expression->children[0]);
        if (first_argument != NULL && first_argument->type == expression->type) {
            expression->type_kind = first_argument->type_kind;
            if (expression->type == TYPE_DERIVED)
                expression->derived_type = first_argument->derived_type;
        }
    }
    if (strcmp(expression->text, "matmul") == 0)
        expression->type_kind = matmul_result_kind(expression);
    if (strcmp(expression->text, "real") == 0 && expression->child_count >= 2U) {
        const int selected_kind = f2c_ast_kind_value_from_argument(expression->children[1]);
        if (selected_kind != 0)
            expression->type_kind = selected_kind;
    } else if (strcmp(expression->text, "cmplx") == 0 && expression->child_count >= 3U) {
        const int selected_kind = f2c_ast_kind_value_from_argument(expression->children[2]);
        if (selected_kind != 0)
            expression->type_kind = selected_kind;
    } else if (strcmp(expression->text, "size") == 0 || strcmp(expression->text, "lbound") == 0 ||
               strcmp(expression->text, "ubound") == 0 || strcmp(expression->text, "shape") == 0) {
        const F2cExpr *kind_argument = f2c_ast_intrinsic_argument(
            expression, "kind", strcmp(expression->text, "shape") == 0 ? 1U : 2U);
        const int selected_kind = f2c_ast_kind_value_from_argument(kind_argument);
        if (selected_kind != 0)
            expression->type_kind = selected_kind;
    }
    if (signature != NULL && signature->kind_rule != F2C_INTRINSIC_KIND_DEFAULT)
        expression->type_kind = f2c_resolve_intrinsic_kind(expression->text, expression->children,
                                                           expression->child_count);
    if (signature != NULL && signature->kind_rule == F2C_INTRINSIC_KIND_OPTIONAL) {
        const F2cExpr *kind_argument =
            f2c_ast_intrinsic_argument(expression, "kind", signature->maximum_arguments - 1U);
        const int selected_kind = f2c_ast_kind_value_from_argument(kind_argument);
        if (selected_kind != 0)
            expression->type_kind = selected_kind;
    }
}

void f2c_ast_resolve_intrinsic_call(AstParser *parser, F2cExpr *expression) {
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(expression->text);
    resolve_intrinsic_type(expression, signature);
    expression->intrinsic = signature != NULL ? signature->id : F2C_INTRINSIC_NONE;
    resolve_intrinsic_shape(parser, expression);
    resolve_intrinsic_kind(expression, signature);
}
