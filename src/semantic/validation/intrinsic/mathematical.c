#include "semantic/validation/private.h"

#include "semantic/numeric_model.h"
#include "semantic/validation/intrinsic/arguments.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static int is_real(Type type) { return type == TYPE_REAL || type == TYPE_DOUBLE; }

static int is_complex(Type type) { return type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX; }

static int is_supported_numeric(const F2cExpr *expression) {
    return expression != NULL &&
           f2c_numeric_model(expression->type, expression_kind(expression)) != NULL;
}

static const char *display_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_ABS:
        return "ABS";
    case F2C_INTRINSIC_ACOS:
        return "ACOS";
    case F2C_INTRINSIC_ASIN:
        return "ASIN";
    case F2C_INTRINSIC_ATAN:
        return "ATAN";
    case F2C_INTRINSIC_ATAN2:
        return "ATAN2";
    case F2C_INTRINSIC_COS:
        return "COS";
    case F2C_INTRINSIC_COSH:
        return "COSH";
    case F2C_INTRINSIC_DPROD:
        return "DPROD";
    case F2C_INTRINSIC_EXP:
        return "EXP";
    case F2C_INTRINSIC_LOG:
        return "LOG";
    case F2C_INTRINSIC_LOG10:
        return "LOG10";
    case F2C_INTRINSIC_MAX:
        return "MAX";
    case F2C_INTRINSIC_MIN:
        return "MIN";
    case F2C_INTRINSIC_SIN:
        return "SIN";
    case F2C_INTRINSIC_SINH:
        return "SINH";
    case F2C_INTRINSIC_SQRT:
        return "SQRT";
    case F2C_INTRINSIC_TAN:
        return "TAN";
    case F2C_INTRINSIC_TANH:
        return "TANH";
    case F2C_INTRINSIC_NONE:
    default:
        return "mathematical intrinsic";
    }
}

static void diagnose_type(Context *context, size_t line, const char *statement_text,
                          const char *intrinsic, const char *argument_name, const F2cExpr *argument,
                          const char *expected) {
    f2c_diagnostic_at(context, line,
                      f2c_validation_expression_start_column(statement_text, argument), 1,
                      "%s argument %s must be %s", intrinsic, argument_name, expected);
}

static void validate_unary_type(Context *context, size_t line, const char *statement_text,
                                F2cIntrinsicId intrinsic, const F2cExpr *argument) {
    const char *name = display_name(intrinsic);
    int valid = 0;
    const char *expected = "REAL";
    if (argument == NULL)
        return;
    switch (intrinsic) {
    case F2C_INTRINSIC_ABS:
        valid =
            argument->type == TYPE_INTEGER || is_real(argument->type) || is_complex(argument->type);
        expected = "INTEGER, REAL, or COMPLEX";
        break;
    case F2C_INTRINSIC_COS:
    case F2C_INTRINSIC_COSH:
    case F2C_INTRINSIC_ACOS:
    case F2C_INTRINSIC_ASIN:
    case F2C_INTRINSIC_ATAN:
    case F2C_INTRINSIC_EXP:
    case F2C_INTRINSIC_LOG:
    case F2C_INTRINSIC_SIN:
    case F2C_INTRINSIC_SINH:
    case F2C_INTRINSIC_SQRT:
    case F2C_INTRINSIC_TAN:
    case F2C_INTRINSIC_TANH:
        valid = is_real(argument->type) || is_complex(argument->type);
        expected = "REAL or COMPLEX";
        break;
    case F2C_INTRINSIC_LOG10:
        valid = is_real(argument->type);
        break;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_ATAN2:
    case F2C_INTRINSIC_DPROD:
    case F2C_INTRINSIC_MAX:
    case F2C_INTRINSIC_MIN:
    default:
        return;
    }
    if (!valid) {
        diagnose_type(context, line, statement_text, name,
                      intrinsic == F2C_INTRINSIC_ABS ? "A" : "X", argument, expected);
    } else if (!is_supported_numeric(argument)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument uses unsupported %s kind %d", name,
                          f2c_validation_type_name(argument->type), expression_kind(argument));
    }
}

static void validate_constant_domain(Context *context, Unit *unit, size_t line,
                                     const char *statement_text, F2cIntrinsicId intrinsic,
                                     const F2cExpr *argument) {
    double value;
    double imaginary;
    const char *requirement = NULL;
    if (argument == NULL || argument->rank != 0U ||
        !f2c_expression_is_initialization_constant(argument))
        return;
    if (is_complex(argument->type)) {
        if (intrinsic != F2C_INTRINSIC_LOG ||
            !f2c_evaluate_complex_constant(unit, argument, &value, &imaginary) || value != 0.0 ||
            imaginary != 0.0)
            return;
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "LOG argument X must be nonzero");
        return;
    }
    if (!is_real(argument->type) || !f2c_evaluate_real_constant(unit, argument, &value) ||
        isnan(value))
        return;
    if ((intrinsic == F2C_INTRINSIC_ACOS || intrinsic == F2C_INTRINSIC_ASIN) && fabs(value) > 1.0)
        requirement = "magnitude no greater than one";
    else if ((intrinsic == F2C_INTRINSIC_LOG || intrinsic == F2C_INTRINSIC_LOG10) && value <= 0.0)
        requirement = "greater than zero";
    else if (intrinsic == F2C_INTRINSIC_SQRT && value < 0.0)
        requirement = "greater than or equal to zero";
    if (requirement != NULL)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument X must be %s", display_name(intrinsic), requirement);
}

static void validate_specific(Context *context, size_t line, const char *statement_text,
                              const F2cExpr *expression, const F2cExpr *first,
                              const F2cExpr *second) {
    Type expected = TYPE_UNKNOWN;
    int kind = 0;
    if (expression->text == NULL)
        return;
    if (strcmp(expression->text, "iabs") == 0) {
        expected = TYPE_INTEGER;
        kind = 4;
    } else if (strcmp(expression->text, "alog") == 0 || strcmp(expression->text, "alog10") == 0) {
        expected = TYPE_REAL;
        kind = 4;
    } else if (strcmp(expression->text, "cabs") == 0 || strcmp(expression->text, "ccos") == 0 ||
               strcmp(expression->text, "cexp") == 0 || strcmp(expression->text, "clog") == 0 ||
               strcmp(expression->text, "csin") == 0 || strcmp(expression->text, "csqrt") == 0) {
        expected = TYPE_COMPLEX;
        kind = 4;
    } else if (expression->text[0] == 'd' && strcmp(expression->text, "dconjg") != 0) {
        expected = TYPE_DOUBLE;
        kind = 8;
    } else if (strcmp(expression->text, "cdabs") == 0) {
        expected = TYPE_DOUBLE_COMPLEX;
        kind = 8;
    }
    if (expected == TYPE_UNKNOWN)
        return;
    if (first != NULL && (first->type != expected || expression_kind(first) != kind))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, first), 1,
                          "%s requires a %s(kind=%d) first argument", expression->text,
                          f2c_validation_type_name(expected), kind);
    if (second != NULL && (second->type != expected || expression_kind(second) != kind))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, second), 1,
                          "%s requires a %s(kind=%d) second argument", expression->text,
                          f2c_validation_type_name(expected), kind);
}

static void validate_dprod(Context *context, size_t line, const char *statement_text,
                           F2cExpr *expression) {
    static const char *const arguments[] = {"x", "y"};
    const F2cBoundIntrinsicArguments bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, "DPROD", expression->children, expression->child_count,
        arguments, 2U, 2U);
    size_t argument;
    for (argument = 0U; argument < 2U; ++argument)
        if (bound.values[argument] != NULL && (bound.values[argument]->type != TYPE_REAL ||
                                               expression_kind(bound.values[argument]) != 4))
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[argument]), 1,
                "DPROD argument %s must be REAL(kind=4)", arguments[argument]);
}

static void validate_unary(Context *context, Unit *unit, size_t line, const char *statement_text,
                           F2cExpr *expression) {
    static const char *const abs_arguments[] = {"a"};
    static const char *const arguments[] = {"x"};
    const char *const *names =
        expression->intrinsic == F2C_INTRINSIC_ABS ? abs_arguments : arguments;
    const F2cBoundIntrinsicArguments bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, display_name(expression->intrinsic), expression->children,
        expression->child_count, names, 1U, 1U);
    validate_unary_type(context, line, statement_text, expression->intrinsic, bound.values[0]);
    validate_constant_domain(context, unit, line, statement_text, expression->intrinsic,
                             bound.values[0]);
    validate_specific(context, line, statement_text, expression, bound.values[0], NULL);
}

static void validate_atan2(Context *context, size_t line, const char *statement_text,
                           F2cExpr *expression) {
    static const char *const arguments[] = {"y", "x"};
    const F2cBoundIntrinsicArguments bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, "ATAN2", expression->children, expression->child_count,
        arguments, 2U, 2U);
    size_t argument;
    for (argument = 0U; argument < 2U; ++argument) {
        if (bound.values[argument] != NULL && !is_real(bound.values[argument]->type))
            diagnose_type(context, line, statement_text, "ATAN2", arguments[argument],
                          bound.values[argument], "REAL");
        else if (bound.values[argument] != NULL && !is_supported_numeric(bound.values[argument]))
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[argument]), 1,
                "ATAN2 argument %s uses unsupported REAL kind %d", arguments[argument],
                expression_kind(bound.values[argument]));
    }
    if (bound.values[0] != NULL && bound.values[1] != NULL &&
        (bound.values[0]->type != bound.values[1]->type ||
         expression_kind(bound.values[0]) != expression_kind(bound.values[1])))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, bound.values[1]),
                          1, "ATAN2 arguments Y and X must have the same type and kind");
    validate_specific(context, line, statement_text, expression, bound.values[0], bound.values[1]);
}

static size_t maximum_argument_index(const F2cExpr *argument) {
    char *end;
    unsigned long value;
    if (argument == NULL || argument->kind != F2C_EXPR_KEYWORD_ARGUMENT || argument->text == NULL ||
        argument->text[0] != 'a' || argument->text[1] == '\0')
        return SIZE_MAX;
    value = strtoul(argument->text + 1, &end, 10);
    return *end == '\0' && value >= 1UL && value <= 64UL ? (size_t)value - 1U : SIZE_MAX;
}

static void validate_extremum(Context *context, size_t line, const char *statement_text,
                              F2cExpr *expression) {
    const char *name = display_name(expression->intrinsic);
    const F2cExpr *values[64] = {0};
    size_t positional = 0U;
    size_t argument;
    int saw_keyword = 0;
    for (argument = 0U; argument < expression->child_count; ++argument) {
        F2cExpr *actual = expression->children[argument];
        size_t index;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            saw_keyword = 1;
            index = maximum_argument_index(actual);
            if (index == SIZE_MAX) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "%s has no argument named '%s'", name,
                                  actual->text != NULL ? actual->text : "");
                continue;
            }
        } else {
            if (saw_keyword)
                f2c_diagnostic_at(
                    context, line, f2c_validation_expression_start_column(statement_text, actual),
                    1, "positional argument in %s cannot follow a keyword argument", name);
            index = positional++;
        }
        if (index >= 64U)
            continue;
        if (values[index] != NULL) {
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, actual), 1,
                              "%s argument 'a%zu' is specified more than once", name, index + 1U);
            continue;
        }
        values[index] = f2c_validation_actual_value(actual);
    }
    if (values[0] == NULL || values[1] == NULL)
        return;
    if (values[0]->type != TYPE_INTEGER && !is_real(values[0]->type)) {
        diagnose_type(context, line, statement_text, name, "A1", values[0], "INTEGER or REAL");
        return;
    }
    if (!is_supported_numeric(values[0])) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, values[0]), 1,
            "%s argument A1 uses unsupported kind %d", name, expression_kind(values[0]));
        return;
    }
    for (argument = 1U; argument < 64U; ++argument) {
        if (values[argument] == NULL)
            continue;
        if (values[argument]->type != values[0]->type ||
            expression_kind(values[argument]) != expression_kind(values[0]))
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, values[argument]), 1,
                "%s argument A%zu must have the same type and kind as A1", name, argument + 1U);
    }
}

void f2c_validation_mathematical_intrinsic(Context *context, Unit *unit, size_t line,
                                           const char *statement_text, F2cExpr *expression) {
    const F2cIntrinsicSignature *signature;
    if (expression == NULL || !f2c_intrinsic_is_mathematical(expression->intrinsic))
        return;
    signature = f2c_find_intrinsic(expression->text);
    if (signature == NULL || expression->child_count < signature->minimum_arguments)
        return;
    if (expression->intrinsic == F2C_INTRINSIC_ATAN2)
        validate_atan2(context, line, statement_text, expression);
    else if (expression->intrinsic == F2C_INTRINSIC_DPROD)
        validate_dprod(context, line, statement_text, expression);
    else if (expression->intrinsic == F2C_INTRINSIC_MAX ||
             expression->intrinsic == F2C_INTRINSIC_MIN)
        validate_extremum(context, line, statement_text, expression);
    else
        validate_unary(context, unit, line, statement_text, expression);
}
