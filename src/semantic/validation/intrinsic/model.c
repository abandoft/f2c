#include "semantic/validation/private.h"

#include "semantic/numeric_model.h"
#include "semantic/validation/intrinsic/arguments.h"

#include <stddef.h>

static const char *display_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_DIGITS:
        return "DIGITS";
    case F2C_INTRINSIC_EPSILON:
        return "EPSILON";
    case F2C_INTRINSIC_HUGE:
        return "HUGE";
    case F2C_INTRINSIC_KIND:
        return "KIND";
    case F2C_INTRINSIC_MAXEXPONENT:
        return "MAXEXPONENT";
    case F2C_INTRINSIC_MINEXPONENT:
        return "MINEXPONENT";
    case F2C_INTRINSIC_PRECISION:
        return "PRECISION";
    case F2C_INTRINSIC_RADIX:
        return "RADIX";
    case F2C_INTRINSIC_RANGE:
        return "RANGE";
    case F2C_INTRINSIC_SELECTED_INT_KIND:
        return "SELECTED_INT_KIND";
    case F2C_INTRINSIC_SELECTED_REAL_KIND:
        return "SELECTED_REAL_KIND";
    case F2C_INTRINSIC_TINY:
        return "TINY";
    case F2C_INTRINSIC_NONE:
    default:
        return "numeric model intrinsic";
    }
}

static int supports_type(F2cIntrinsicId intrinsic, Type type) {
    switch (intrinsic) {
    case F2C_INTRINSIC_DIGITS:
    case F2C_INTRINSIC_HUGE:
    case F2C_INTRINSIC_RADIX:
        return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE;
    case F2C_INTRINSIC_EPSILON:
    case F2C_INTRINSIC_MAXEXPONENT:
    case F2C_INTRINSIC_MINEXPONENT:
    case F2C_INTRINSIC_TINY:
        return type == TYPE_REAL || type == TYPE_DOUBLE;
    case F2C_INTRINSIC_PRECISION:
        return type == TYPE_REAL || type == TYPE_DOUBLE || type == TYPE_COMPLEX ||
               type == TYPE_DOUBLE_COMPLEX;
    case F2C_INTRINSIC_RANGE:
        return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE ||
               type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX;
    case F2C_INTRINSIC_KIND:
        return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE ||
               type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX || type == TYPE_LOGICAL ||
               type == TYPE_CHARACTER;
    case F2C_INTRINSIC_SELECTED_INT_KIND:
    case F2C_INTRINSIC_SELECTED_REAL_KIND:
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
}

static int supports_kind(const F2cExpr *argument) {
    const int kind = argument->type_kind != 0 ? argument->type_kind
                                              : f2c_default_kind(argument->type);
    if (argument->type == TYPE_CHARACTER)
        return kind == 1;
    if (argument->type == TYPE_LOGICAL)
        return kind == 1 || kind == 2 || kind == 4 || kind == 8;
    return f2c_numeric_model(argument->type, kind) != NULL;
}

static void validate_model_argument(Context *context, size_t line, const char *statement_text,
                                    F2cIntrinsicId intrinsic, const F2cExpr *argument) {
    const char *name = display_name(intrinsic);
    if (argument == NULL)
        return;
    if (!supports_type(intrinsic, argument->type)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument X has an unsupported type", name);
    } else if (!supports_kind(argument)) {
        const int kind = argument->type_kind != 0 ? argument->type_kind
                                                   : f2c_default_kind(argument->type);
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument X uses unsupported kind %d", name, kind);
    }
}

static void validate_integer_scalar(Context *context, size_t line, const char *statement_text,
                                    const char *intrinsic, const char *argument_name,
                                    const F2cExpr *argument) {
    if (argument != NULL && (argument->type != TYPE_INTEGER || argument->rank != 0U))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument %s must be a scalar INTEGER", intrinsic, argument_name);
}

void f2c_validation_numeric_model_intrinsic(Context *context, size_t line,
                                            const char *statement_text, F2cExpr *expression) {
    static const char *const model[] = {"x"};
    static const char *const integer_kind[] = {"r"};
    static const char *const real_kind[] = {"p", "r", "radix"};
    F2cBoundIntrinsicArguments bound;
    const char *name;
    if (expression == NULL || !f2c_intrinsic_is_numeric_model(expression->intrinsic))
        return;
    name = display_name(expression->intrinsic);
    if (expression->intrinsic == F2C_INTRINSIC_SELECTED_INT_KIND) {
        bound = f2c_validation_bind_intrinsic_arguments(
            context, line, statement_text, name, expression->children, expression->child_count,
            integer_kind, 1U, 1U);
        validate_integer_scalar(context, line, statement_text, name, "R", bound.values[0]);
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_SELECTED_REAL_KIND) {
        size_t argument;
        bound = f2c_validation_bind_intrinsic_arguments(
            context, line, statement_text, name, expression->children, expression->child_count,
            real_kind, 3U, 0U);
        if (bound.values[0] == NULL && bound.values[1] == NULL)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, expression),
                              1, "SELECTED_REAL_KIND requires P or R");
        for (argument = 0U; argument < 3U; ++argument)
            validate_integer_scalar(context, line, statement_text, name,
                                    argument == 0U ? "P" : argument == 1U ? "R" : "RADIX",
                                    bound.values[argument]);
        return;
    }
    bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, name, expression->children, expression->child_count, model,
        1U, 1U);
    validate_model_argument(context, line, statement_text, expression->intrinsic,
                            bound.values[0]);
}
