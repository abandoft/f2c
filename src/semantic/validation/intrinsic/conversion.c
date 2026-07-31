#include "semantic/validation/private.h"

#include "semantic/numeric_model.h"
#include "semantic/validation/intrinsic/arguments.h"

#include <stdint.h>
#include <string.h>

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static int is_numeric(Type type) {
    return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE ||
           type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX;
}

static int is_complex(Type type) { return type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX; }

static const char *display_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_AIMAG:
        return "AIMAG";
    case F2C_INTRINSIC_CMPLX:
        return "CMPLX";
    case F2C_INTRINSIC_CONJG:
        return "CONJG";
    case F2C_INTRINSIC_DBLE:
        return "DBLE";
    case F2C_INTRINSIC_INT:
        return "INT";
    case F2C_INTRINSIC_LOGICAL:
        return "LOGICAL";
    case F2C_INTRINSIC_REAL:
        return "REAL";
    case F2C_INTRINSIC_NONE:
    default:
        return "conversion intrinsic";
    }
}

static void require_supported_numeric(Context *context, size_t line, const char *statement_text,
                                      const char *intrinsic, const char *argument_name,
                                      const F2cExpr *argument) {
    if (argument == NULL)
        return;
    if (!is_numeric(argument->type)) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, argument), 1,
            "%s argument %s must be INTEGER, REAL, or COMPLEX", intrinsic, argument_name);
    } else if (f2c_numeric_model(argument->type, expression_kind(argument)) == NULL) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument %s uses unsupported %s kind %d", intrinsic, argument_name,
                          f2c_validation_type_name(argument->type), expression_kind(argument));
    }
}

static void require_supported_logical(Context *context, size_t line, const char *statement_text,
                                      const char *intrinsic, const F2cExpr *argument) {
    const int kind = expression_kind(argument);
    if (argument == NULL)
        return;
    if (argument->type != TYPE_LOGICAL) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument L must be LOGICAL", intrinsic);
    } else if (kind != 1 && kind != 2 && kind != 4 && kind != 8) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument L uses unsupported LOGICAL kind %d", intrinsic, kind);
    }
}

static int validate_kind(Context *context, Unit *unit, size_t line, const char *statement_text,
                         const char *intrinsic, const F2cExpr *kind, int integer_result,
                         int *value_out) {
    int64_t value;
    if (kind == NULL)
        return 1;
    if (kind->type != TYPE_INTEGER || kind->rank != 0U ||
        !f2c_expression_is_initialization_constant(kind) ||
        !f2c_evaluate_integer_constant(unit, kind, &value) ||
        (integer_result ? (value != 1 && value != 2 && value != 4 && value != 8)
                        : (value != 4 && value != 8))) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, kind), 1,
            integer_result
                ? "%s argument KIND must be a supported scalar INTEGER initialization constant "
                  "(1, 2, 4, or 8)"
                : "%s argument KIND must be a supported scalar INTEGER initialization constant "
                  "(4 or 8)",
            intrinsic);
        return 0;
    }
    *value_out = (int)value;
    return 1;
}

static void validate_specific_source(Context *context, size_t line, const char *statement_text,
                                     const F2cExpr *expression, const F2cExpr *source) {
    Type expected = TYPE_UNKNOWN;
    int kind = 0;
    if (expression->text == NULL || source == NULL)
        return;
    if (strcmp(expression->text, "ifix") == 0 || strcmp(expression->text, "float") == 0) {
        expected = strcmp(expression->text, "ifix") == 0 ? TYPE_REAL : TYPE_INTEGER;
        kind = 4;
    } else if (strcmp(expression->text, "idint") == 0 || strcmp(expression->text, "sngl") == 0) {
        expected = TYPE_DOUBLE;
        kind = 8;
    } else if (strcmp(expression->text, "dimag") == 0 || strcmp(expression->text, "dreal") == 0 ||
               strcmp(expression->text, "dconjg") == 0) {
        expected = TYPE_DOUBLE_COMPLEX;
        kind = 8;
    }
    if (expected != TYPE_UNKNOWN && (source->type != expected || expression_kind(source) != kind))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, source), 1,
                          "%s requires a %s(kind=%d) argument", expression->text,
                          f2c_validation_type_name(expected), kind);
}

static void resolve_result(F2cExpr *expression, const F2cExpr *source, int selected_kind) {
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_AIMAG:
        expression->type = source != NULL && expression_kind(source) == 8 ? TYPE_DOUBLE : TYPE_REAL;
        expression->type_kind = source != NULL ? expression_kind(source) : 4;
        break;
    case F2C_INTRINSIC_CMPLX:
        expression->type_kind = strcmp(expression->text, "dcmplx") == 0 ? 8
                                : selected_kind != 0                    ? selected_kind
                                                                        : 4;
        expression->type = expression->type_kind == 8 ? TYPE_DOUBLE_COMPLEX : TYPE_COMPLEX;
        break;
    case F2C_INTRINSIC_CONJG:
        if (source != NULL) {
            expression->type = source->type;
            expression->type_kind = expression_kind(source);
        }
        break;
    case F2C_INTRINSIC_DBLE:
        expression->type = TYPE_DOUBLE;
        expression->type_kind = 8;
        break;
    case F2C_INTRINSIC_INT:
        expression->type = TYPE_INTEGER;
        expression->type_kind = selected_kind != 0 ? selected_kind : 4;
        break;
    case F2C_INTRINSIC_LOGICAL:
        expression->type = TYPE_LOGICAL;
        expression->type_kind = selected_kind != 0 ? selected_kind : 4;
        break;
    case F2C_INTRINSIC_REAL:
        expression->type_kind = selected_kind != 0 ? selected_kind
                                : source != NULL && is_complex(source->type)
                                    ? expression_kind(source)
                                    : 4;
        expression->type = expression->type_kind == 8 ? TYPE_DOUBLE : TYPE_REAL;
        break;
    case F2C_INTRINSIC_NONE:
    default:
        break;
    }
}

static void validate_single_source(Context *context, Unit *unit, size_t line,
                                   const char *statement_text, F2cExpr *expression) {
    const int complex_operation = expression->intrinsic == F2C_INTRINSIC_AIMAG ||
                                  expression->intrinsic == F2C_INTRINSIC_CONJG;
    const int logical_operation = expression->intrinsic == F2C_INTRINSIC_LOGICAL;
    const int has_kind =
        (expression->intrinsic == F2C_INTRINSIC_INT ||
         expression->intrinsic == F2C_INTRINSIC_REAL ||
         expression->intrinsic == F2C_INTRINSIC_LOGICAL) &&
        (strcmp(expression->text, "int") == 0 || strcmp(expression->text, "real") == 0 ||
         strcmp(expression->text, "logical") == 0);
    const char *intrinsic = display_name(expression->intrinsic);
    const F2cBoundIntrinsicArguments bound =
        f2c_validation_bind_intrinsic_expression(context, line, statement_text, expression);
    int selected_kind = 0;
    if (complex_operation) {
        if (bound.values[0] != NULL && !is_complex(bound.values[0]->type))
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[0]), 1,
                "%s argument Z must be COMPLEX", intrinsic);
        else
            require_supported_numeric(context, line, statement_text, intrinsic, "Z",
                                      bound.values[0]);
    } else if (logical_operation) {
        require_supported_logical(context, line, statement_text, intrinsic, bound.values[0]);
    } else {
        require_supported_numeric(context, line, statement_text, intrinsic, "A", bound.values[0]);
    }
    if (has_kind)
        (void)validate_kind(context, unit, line, statement_text, intrinsic, bound.values[1],
                            expression->intrinsic == F2C_INTRINSIC_INT ||
                                expression->intrinsic == F2C_INTRINSIC_LOGICAL,
                            &selected_kind);
    validate_specific_source(context, line, statement_text, expression, bound.values[0]);
    resolve_result(expression, bound.values[0], selected_kind);
}

static void validate_cmplx(Context *context, Unit *unit, size_t line, const char *statement_text,
                           F2cExpr *expression) {
    const int generic = strcmp(expression->text, "cmplx") == 0;
    const F2cBoundIntrinsicArguments bound =
        f2c_validation_bind_intrinsic_expression(context, line, statement_text, expression);
    int selected_kind = 0;
    require_supported_numeric(context, line, statement_text, generic ? "CMPLX" : "DCMPLX", "X",
                              bound.values[0]);
    if (bound.values[1] != NULL) {
        if (bound.values[0] != NULL && is_complex(bound.values[0]->type))
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[1]), 1,
                "%s argument Y must be absent when X is COMPLEX", generic ? "CMPLX" : "DCMPLX");
        if (bound.values[1]->type != TYPE_INTEGER && bound.values[1]->type != TYPE_REAL &&
            bound.values[1]->type != TYPE_DOUBLE)
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[1]), 1,
                "%s argument Y must be INTEGER or REAL", generic ? "CMPLX" : "DCMPLX");
        else
            require_supported_numeric(context, line, statement_text, generic ? "CMPLX" : "DCMPLX",
                                      "Y", bound.values[1]);
    }
    if (generic)
        (void)validate_kind(context, unit, line, statement_text, "CMPLX", bound.values[2], 0,
                            &selected_kind);
    resolve_result(expression, bound.values[0], selected_kind);
}

void f2c_validation_conversion_intrinsic(Context *context, Unit *unit, size_t line,
                                         const char *statement_text, F2cExpr *expression) {
    const F2cIntrinsicSignature *signature;
    if (expression == NULL || !f2c_intrinsic_is_conversion(expression->intrinsic))
        return;
    signature = f2c_find_intrinsic(expression->text);
    if (signature == NULL || expression->child_count < signature->minimum_arguments)
        return;
    if (expression->intrinsic == F2C_INTRINSIC_CMPLX)
        validate_cmplx(context, unit, line, statement_text, expression);
    else
        validate_single_source(context, unit, line, statement_text, expression);
}
