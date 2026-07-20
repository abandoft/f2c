#include "semantic/validation/private.h"

#include "semantic/numeric_model.h"
#include "semantic/validation/intrinsic/arguments.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *display_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_AINT:
        return "AINT";
    case F2C_INTRINSIC_ANINT:
        return "ANINT";
    case F2C_INTRINSIC_CEILING:
        return "CEILING";
    case F2C_INTRINSIC_DIM:
        return "DIM";
    case F2C_INTRINSIC_FLOOR:
        return "FLOOR";
    case F2C_INTRINSIC_MERGE:
        return "MERGE";
    case F2C_INTRINSIC_MOD:
        return "MOD";
    case F2C_INTRINSIC_MODULO:
        return "MODULO";
    case F2C_INTRINSIC_NINT:
        return "NINT";
    case F2C_INTRINSIC_SIGN:
        return "SIGN";
    case F2C_INTRINSIC_NONE:
    default:
        return "numeric intrinsic";
    }
}

static int resolved_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static int supported_numeric_type(const F2cExpr *argument, int integer_allowed) {
    if (argument == NULL)
        return 0;
    if (argument->type != TYPE_REAL && argument->type != TYPE_DOUBLE &&
        (!integer_allowed || argument->type != TYPE_INTEGER))
        return 0;
    return f2c_numeric_model(argument->type, resolved_kind(argument)) != NULL;
}

static void require_real(Context *context, size_t line, const char *statement_text,
                         const char *intrinsic, const F2cExpr *argument) {
    if (argument == NULL)
        return;
    if (argument->type != TYPE_REAL && argument->type != TYPE_DOUBLE) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument A must be REAL", intrinsic);
    } else if (!supported_numeric_type(argument, 0)) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, argument), 1,
            "%s argument A uses unsupported REAL kind %d", intrinsic, resolved_kind(argument));
    }
}

static void validate_kind(Context *context, Unit *unit, size_t line, const char *statement_text,
                          const char *intrinsic, F2cIntrinsicId id, const F2cExpr *kind,
                          F2cExpr *result) {
    const int real_result = id == F2C_INTRINSIC_AINT || id == F2C_INTRINSIC_ANINT;
    int64_t value;
    if (kind == NULL)
        return;
    if (kind->type != TYPE_INTEGER || kind->rank != 0U ||
        !f2c_expression_is_initialization_constant(kind) ||
        !f2c_evaluate_integer_constant(unit, kind, &value) ||
        (real_result ? (value != 4 && value != 8)
                     : (value != 1 && value != 2 && value != 4 && value != 8))) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, kind), 1,
            real_result
                ? "%s argument KIND must be a supported scalar INTEGER initialization constant "
                  "(4 or 8)"
                : "%s argument KIND must be a supported scalar INTEGER initialization constant "
                  "(1, 2, 4, or 8)",
            intrinsic);
        return;
    }
    result->type_kind = (int)value;
    if (real_result)
        result->type = value == 8 ? TYPE_DOUBLE : TYPE_REAL;
}

static int known_character_length(Unit *unit, const F2cExpr *expression, int64_t *length) {
    char *constant = NULL;
    size_t constant_length = 0U;
    if (expression == NULL || expression->type != TYPE_CHARACTER || length == NULL)
        return 0;
    if (f2c_evaluate_character_constant(unit, expression, &constant, &constant_length)) {
        free(constant);
        if (constant_length > (size_t)INT64_MAX)
            return 0;
        *length = (int64_t)constant_length;
        return 1;
    }
    free(constant);
    if (expression->symbol == NULL)
        return 0;
    if (expression->symbol->character_length_expression != NULL)
        return f2c_evaluate_integer_constant(unit, expression->symbol->character_length_expression,
                                             length);
    if (expression->symbol->character_length_syntax.count != 0U)
        return f2c_evaluate_integer_syntax(unit, expression->symbol->character_length_syntax,
                                           length);
    if (expression->symbol->character_length == NULL ||
        strcmp(expression->symbol->character_length, "1") == 0) {
        *length = 1;
        return 1;
    }
    return 0;
}

static void require_same_type(Context *context, Unit *unit, size_t line, const char *statement_text,
                              const char *intrinsic, const char *argument_name,
                              const F2cExpr *first, const F2cExpr *second, int merge) {
    int64_t first_length;
    int64_t second_length;
    if (first == NULL || second == NULL)
        return;
    if (first->type != second->type || resolved_kind(first) != resolved_kind(second) ||
        (first->type == TYPE_DERIVED && first->derived_type != second->derived_type)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, second), 1,
                          "%s argument %s must have the same type and kind as %s", intrinsic,
                          argument_name, merge ? "TSOURCE" : "the first argument");
        return;
    }
    if (merge && first->type == TYPE_CHARACTER &&
        known_character_length(unit, first, &first_length) &&
        known_character_length(unit, second, &second_length) && first_length != second_length)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, second), 1,
                          "MERGE argument FSOURCE must have the same CHARACTER length as "
                          "TSOURCE");
}

static void validate_legacy_specific(Context *context, size_t line, const char *statement_text,
                                     const F2cExpr *expression, const F2cExpr *first,
                                     const F2cExpr *second) {
    Type expected = TYPE_UNKNOWN;
    int kind = 0;
    if (expression->text == NULL)
        return;
    if (strcmp(expression->text, "dint") == 0 || strcmp(expression->text, "dnint") == 0 ||
        strcmp(expression->text, "idnint") == 0 || strcmp(expression->text, "ddim") == 0 ||
        strcmp(expression->text, "dmod") == 0 || strcmp(expression->text, "dsign") == 0) {
        expected = TYPE_DOUBLE;
        kind = 8;
    } else if (strcmp(expression->text, "amod") == 0) {
        expected = TYPE_REAL;
        kind = 4;
    } else if (strcmp(expression->text, "idim") == 0 || strcmp(expression->text, "isign") == 0) {
        expected = TYPE_INTEGER;
        kind = 4;
    }
    if (expected == TYPE_UNKNOWN)
        return;
    if (first != NULL && (first->type != expected || resolved_kind(first) != kind))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, first), 1,
                          "%s requires a %s(kind=%d) first argument", expression->text,
                          f2c_validation_type_name(expected), kind);
    if (second != NULL && (second->type != expected || resolved_kind(second) != kind))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, second), 1,
                          "%s requires a %s(kind=%d) second argument", expression->text,
                          f2c_validation_type_name(expected), kind);
}

static void validate_rounding(Context *context, Unit *unit, size_t line, const char *statement_text,
                              F2cExpr *expression) {
    static const char *const arguments[] = {"a", "kind"};
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(expression->text);
    const size_t argument_count = signature != NULL && signature->maximum_arguments == 2U ? 2U : 1U;
    const F2cBoundIntrinsicArguments bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, display_name(expression->intrinsic), expression->children,
        expression->child_count, arguments, argument_count, 1U);
    require_real(context, line, statement_text, display_name(expression->intrinsic),
                 bound.values[0]);
    if (argument_count == 2U)
        validate_kind(context, unit, line, statement_text, display_name(expression->intrinsic),
                      expression->intrinsic, bound.values[1], expression);
    validate_legacy_specific(context, line, statement_text, expression, bound.values[0], NULL);
}

static void validate_binary_numeric(Context *context, Unit *unit, size_t line,
                                    const char *statement_text, F2cExpr *expression) {
    static const char *const dim_arguments[] = {"x", "y"};
    static const char *const mod_arguments[] = {"a", "p"};
    static const char *const sign_arguments[] = {"a", "b"};
    const char *const *arguments = expression->intrinsic == F2C_INTRINSIC_DIM    ? dim_arguments
                                   : expression->intrinsic == F2C_INTRINSIC_SIGN ? sign_arguments
                                                                                 : mod_arguments;
    const char *name = display_name(expression->intrinsic);
    const F2cBoundIntrinsicArguments bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, name, expression->children, expression->child_count,
        arguments, 2U, 2U);
    int64_t integer_zero;
    double real_zero;
    if (bound.values[0] != NULL && !supported_numeric_type(bound.values[0], 1))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, bound.values[0]),
                          1, "%s arguments must be INTEGER or REAL with a supported kind", name);
    require_same_type(context, unit, line, statement_text, name, arguments[1], bound.values[0],
                      bound.values[1], 0);
    if ((expression->intrinsic == F2C_INTRINSIC_MOD ||
         expression->intrinsic == F2C_INTRINSIC_MODULO) &&
        bound.values[1] != NULL &&
        ((bound.values[1]->type == TYPE_INTEGER &&
          f2c_evaluate_integer_constant(unit, bound.values[1], &integer_zero) &&
          integer_zero == 0) ||
         ((bound.values[1]->type == TYPE_REAL || bound.values[1]->type == TYPE_DOUBLE) &&
          f2c_evaluate_real_constant(unit, bound.values[1], &real_zero) && real_zero == 0.0)))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, bound.values[1]),
                          1, "%s argument P must not be zero", name);
    validate_legacy_specific(context, line, statement_text, expression, bound.values[0],
                             bound.values[1]);
}

static void validate_merge(Context *context, Unit *unit, size_t line, const char *statement_text,
                           F2cExpr *expression) {
    static const char *const arguments[] = {"tsource", "fsource", "mask"};
    const F2cBoundIntrinsicArguments bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, "MERGE", expression->children, expression->child_count,
        arguments, 3U, 3U);
    require_same_type(context, unit, line, statement_text, "MERGE", "FSOURCE", bound.values[0],
                      bound.values[1], 1);
    if (bound.values[2] != NULL && bound.values[2]->type != TYPE_LOGICAL)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, bound.values[2]),
                          1, "MERGE argument MASK must be LOGICAL");
}

void f2c_validation_numeric_operation_intrinsic(Context *context, Unit *unit, size_t line,
                                                const char *statement_text, F2cExpr *expression) {
    const F2cIntrinsicSignature *signature;
    if (expression == NULL || !f2c_intrinsic_is_numeric_operation(expression->intrinsic))
        return;
    signature = f2c_find_intrinsic(expression->text);
    if (signature == NULL || expression->child_count < signature->minimum_arguments)
        return;
    if (expression->intrinsic == F2C_INTRINSIC_AINT ||
        expression->intrinsic == F2C_INTRINSIC_ANINT ||
        expression->intrinsic == F2C_INTRINSIC_CEILING ||
        expression->intrinsic == F2C_INTRINSIC_FLOOR ||
        expression->intrinsic == F2C_INTRINSIC_NINT) {
        validate_rounding(context, unit, line, statement_text, expression);
    } else if (expression->intrinsic == F2C_INTRINSIC_MERGE) {
        validate_merge(context, unit, line, statement_text, expression);
    } else {
        validate_binary_numeric(context, unit, line, statement_text, expression);
    }
}
