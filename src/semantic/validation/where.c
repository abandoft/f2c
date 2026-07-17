#include "semantic/validation/private.h"

static void report_shape_mismatch(Context *context, const F2cStatement *statement,
                                  const F2cExpr *mask, const F2cExpr *expression,
                                  const char *role) {
    size_t dimension;
    if (mask == NULL || expression == NULL)
        return;
    if (expression->rank != mask->rank) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, expression), 1,
                          "%s rank %zu does not conform to WHERE mask rank %zu", role,
                          expression->rank, mask->rank);
    } else if (f2c_validation_shapes_mismatch(mask, expression, &dimension)) {
        f2c_diagnostic_at(
            context, statement->line, f2c_validation_expression_column(statement->text, expression),
            1, "%s extent does not conform to WHERE mask in dimension %zu", role, dimension + 1U);
    }
}

static void validate_mask(Context *context, const F2cStatement *statement, const F2cExpr *mask,
                          const F2cExpr *owner_mask) {
    if (mask == NULL) {
        f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SYNTAX, &statement->span, 1,
                                 "WHERE requires a mask expression");
        return;
    }
    if (mask->type != TYPE_LOGICAL || mask->rank == 0U) {
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_column(statement->text, mask), 1,
                          "WHERE mask must be a LOGICAL array expression");
    }
    if (owner_mask != NULL)
        report_shape_mismatch(context, statement, owner_mask, mask, "nested WHERE mask");
}

void f2c_validation_where_statement(Context *context, const F2cStatement *statement) {
    const F2cStatement *owner = statement->construct_owner;
    const F2cExpr *owner_mask =
        owner != NULL && owner->kind == F2C_STMT_WHERE ? owner->expression : NULL;
    if (statement->kind == F2C_STMT_WHERE) {
        validate_mask(context, statement, statement->expression, owner_mask);
        if (!statement->block &&
            (statement->nested == NULL || statement->nested->kind != F2C_STMT_ASSIGNMENT)) {
            f2c_diagnostic_span_code(context, F2C_DIAGNOSTIC_SEMANTIC, &statement->span, 1,
                                     "single-line WHERE requires an intrinsic assignment");
        }
        return;
    }
    if (statement->kind == F2C_STMT_ELSEWHERE) {
        if (statement->expression != NULL)
            validate_mask(context, statement, statement->expression, owner_mask);
        return;
    }
    if (statement->kind == F2C_STMT_ASSIGNMENT && owner_mask != NULL) {
        report_shape_mismatch(context, statement, owner_mask, statement->left,
                              "masked assignment target");
        if (statement->right != NULL && statement->right->rank != 0U)
            report_shape_mismatch(context, statement, owner_mask, statement->right,
                                  "masked assignment value");
    }
}
