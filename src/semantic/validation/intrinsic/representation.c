#include "semantic/validation/private.h"

#include "semantic/numeric_model.h"
#include "semantic/validation/intrinsic/arguments.h"

#include <ctype.h>
#include <stddef.h>

static const char *display_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_EXPONENT:
        return "EXPONENT";
    case F2C_INTRINSIC_FRACTION:
        return "FRACTION";
    case F2C_INTRINSIC_NEAREST:
        return "NEAREST";
    case F2C_INTRINSIC_RRSPACING:
        return "RRSPACING";
    case F2C_INTRINSIC_SCALE:
        return "SCALE";
    case F2C_INTRINSIC_SET_EXPONENT:
        return "SET_EXPONENT";
    case F2C_INTRINSIC_SPACING:
        return "SPACING";
    case F2C_INTRINSIC_NONE:
    default:
        return "real representation intrinsic";
    }
}

static int real_kind_supported(const F2cExpr *argument) {
    const int kind =
        argument->type_kind != 0 ? argument->type_kind : f2c_default_kind(argument->type);
    return f2c_numeric_model(argument->type, kind) != NULL;
}

static void validate_real(Context *context, size_t line, const char *statement_text,
                          const char *intrinsic, const char *name, const F2cExpr *argument) {
    if (argument == NULL)
        return;
    if (argument->type != TYPE_REAL && argument->type != TYPE_DOUBLE) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument %s must be REAL", intrinsic, name);
    } else if (!real_kind_supported(argument)) {
        const int kind =
            argument->type_kind != 0 ? argument->type_kind : f2c_default_kind(argument->type);
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument %s uses unsupported REAL kind %d", intrinsic, name, kind);
    }
}

static void validate_integer(Context *context, size_t line, const char *statement_text,
                             const char *intrinsic, const F2cExpr *argument) {
    int kind;
    if (argument == NULL)
        return;
    if (argument->type != TYPE_INTEGER) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument I must be INTEGER", intrinsic);
        return;
    }
    kind = argument->type_kind != 0 ? argument->type_kind : f2c_default_kind(TYPE_INTEGER);
    if (f2c_numeric_model(TYPE_INTEGER, kind) == NULL)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument I uses unsupported INTEGER kind %d", intrinsic, kind);
}

static const F2cExpr *strip_unary_sign(const F2cExpr *expression) {
    while (expression != NULL && expression->kind == F2C_EXPR_UNARY &&
           expression->child_count == 1U && expression->text != NULL &&
           (expression->text[0] == '+' || expression->text[0] == '-') &&
           expression->text[1] == '\0')
        expression = expression->children[0];
    return expression;
}

static int is_literal_zero(const F2cExpr *expression) {
    const char *cursor;
    expression = strip_unary_sign(expression);
    if (expression == NULL || expression->kind != F2C_EXPR_REAL_LITERAL || expression->text == NULL)
        return 0;
    for (cursor = expression->text;
         *cursor != '\0' && *cursor != '_' && *cursor != 'e' && *cursor != 'E' && *cursor != 'd' &&
         *cursor != 'D' && *cursor != 'q' && *cursor != 'Q';
         ++cursor)
        if (isdigit((unsigned char)*cursor) && *cursor != '0')
            return 0;
    return 1;
}

void f2c_validation_real_representation_intrinsic(Context *context, size_t line,
                                                  const char *statement_text, F2cExpr *expression) {
    static const char *const unary[] = {"x"};
    static const char *const nearest[] = {"x", "s"};
    static const char *const scaled[] = {"x", "i"};
    const char *const *arguments = unary;
    size_t argument_count = 1U;
    F2cBoundIntrinsicArguments bound;
    const char *name;
    if (expression == NULL || !f2c_intrinsic_is_real_representation(expression->intrinsic))
        return;
    name = display_name(expression->intrinsic);
    if (expression->intrinsic == F2C_INTRINSIC_NEAREST) {
        arguments = nearest;
        argument_count = 2U;
    } else if (expression->intrinsic == F2C_INTRINSIC_SCALE ||
               expression->intrinsic == F2C_INTRINSIC_SET_EXPONENT) {
        arguments = scaled;
        argument_count = 2U;
    }
    bound = f2c_validation_bind_intrinsic_arguments(context, line, statement_text, name,
                                                    expression->children, expression->child_count,
                                                    arguments, argument_count, argument_count);
    validate_real(context, line, statement_text, name, "X", bound.values[0]);
    if (expression->intrinsic == F2C_INTRINSIC_NEAREST) {
        validate_real(context, line, statement_text, name, "S", bound.values[1]);
        if (is_literal_zero(bound.values[1]))
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[1]), 1,
                "NEAREST argument S must not be zero");
    } else if (argument_count == 2U) {
        validate_integer(context, line, statement_text, name, bound.values[1]);
    }
}
