#include "semantic/validation/private.h"

#include "semantic/validation/intrinsic/arguments.h"

#include <stdint.h>
#include <string.h>

static const char *display_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_ACHAR:
        return "ACHAR";
    case F2C_INTRINSIC_ADJUSTL:
        return "ADJUSTL";
    case F2C_INTRINSIC_ADJUSTR:
        return "ADJUSTR";
    case F2C_INTRINSIC_CHAR:
        return "CHAR";
    case F2C_INTRINSIC_IACHAR:
        return "IACHAR";
    case F2C_INTRINSIC_ICHAR:
        return "ICHAR";
    case F2C_INTRINSIC_INDEX:
        return "INDEX";
    case F2C_INTRINSIC_LEN:
        return "LEN";
    case F2C_INTRINSIC_LEN_TRIM:
        return "LEN_TRIM";
    case F2C_INTRINSIC_LGE:
        return "LGE";
    case F2C_INTRINSIC_LGT:
        return "LGT";
    case F2C_INTRINSIC_LLE:
        return "LLE";
    case F2C_INTRINSIC_LLT:
        return "LLT";
    case F2C_INTRINSIC_REPEAT:
        return "REPEAT";
    case F2C_INTRINSIC_SCAN:
        return "SCAN";
    case F2C_INTRINSIC_TRIM:
        return "TRIM";
    case F2C_INTRINSIC_VERIFY:
        return "VERIFY";
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_BTEST:
    case F2C_INTRINSIC_IAND:
    case F2C_INTRINSIC_IBCLR:
    case F2C_INTRINSIC_IBITS:
    case F2C_INTRINSIC_IBSET:
    case F2C_INTRINSIC_IEOR:
    case F2C_INTRINSIC_IOR:
    case F2C_INTRINSIC_ISHFT:
    case F2C_INTRINSIC_ISHFTC:
    case F2C_INTRINSIC_NOT:
    case F2C_INTRINSIC_MVBITS:
    default:
        return "character intrinsic";
    }
}

static void require_type(Context *context, size_t line, const char *statement_text,
                         const char *intrinsic, const char *argument_name, const F2cExpr *argument,
                         Type type) {
    if (argument != NULL && argument->type != type)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument %s must be %s", intrinsic, argument_name,
                          type == TYPE_CHARACTER ? "CHARACTER"
                          : type == TYPE_INTEGER ? "INTEGER"
                                                 : "LOGICAL");
}

static void validate_kind(Context *context, Unit *unit, size_t line, const char *statement_text,
                          const char *intrinsic, const F2cExpr *kind, int character_kind) {
    int64_t value;
    if (kind == NULL)
        return;
    if (kind->type != TYPE_INTEGER || kind->rank != 0U ||
        !f2c_expression_is_initialization_constant(kind) ||
        !f2c_evaluate_integer_constant(unit, kind, &value) ||
        (character_kind ? value != 1 : (value != 1 && value != 2 && value != 4 && value != 8)))
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, kind), 1,
            character_kind
                ? "KIND in %s must be the supported default CHARACTER kind (1)"
                : "KIND in %s must be a supported scalar INTEGER constant (1, 2, 4, or 8)",
            intrinsic);
}

static void validate_character_kind(Context *context, size_t line, const char *statement_text,
                                    const char *intrinsic, const char *argument_name,
                                    const F2cExpr *argument) {
    if (argument != NULL && argument->type == TYPE_CHARACTER &&
        argument->type_kind != f2c_default_kind(TYPE_CHARACTER))
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, argument), 1,
            "%s argument %s uses an unsupported CHARACTER kind", intrinsic, argument_name);
}

void f2c_validation_character_intrinsic(Context *context, Unit *unit, size_t line,
                                        const char *statement_text, F2cExpr *expression) {
    F2cBoundIntrinsicArguments bound;
    const char *intrinsic;
    const F2cExpr *primary;
    const F2cExpr *secondary = NULL;
    const F2cExpr *back = NULL;
    const F2cExpr *kind = NULL;
    int64_t value;
    if (expression == NULL || !f2c_intrinsic_is_character(expression->intrinsic))
        return;
    intrinsic = display_name(expression->intrinsic);
    bound = f2c_validation_bind_intrinsic_expression(context, line, statement_text, expression);
    primary = bound.values[0];
    if (expression->intrinsic == F2C_INTRINSIC_LGE || expression->intrinsic == F2C_INTRINSIC_LGT ||
        expression->intrinsic == F2C_INTRINSIC_LLE || expression->intrinsic == F2C_INTRINSIC_LLT) {
        secondary = bound.values[1];
        require_type(context, line, statement_text, intrinsic, "STRING_A", primary, TYPE_CHARACTER);
        require_type(context, line, statement_text, intrinsic, "STRING_B", secondary,
                     TYPE_CHARACTER);
        validate_character_kind(context, line, statement_text, intrinsic, "STRING_A", primary);
        validate_character_kind(context, line, statement_text, intrinsic, "STRING_B", secondary);
        if (primary != NULL && secondary != NULL && primary->type == TYPE_CHARACTER &&
            secondary->type == TYPE_CHARACTER && primary->type_kind != secondary->type_kind)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, secondary), 1,
                              "%s CHARACTER arguments must have the same kind", intrinsic);
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_ACHAR ||
        expression->intrinsic == F2C_INTRINSIC_CHAR) {
        require_type(context, line, statement_text, intrinsic, "I", primary, TYPE_INTEGER);
        kind = bound.values[1];
        validate_kind(context, unit, line, statement_text, intrinsic, kind, 1);
        if (expression->intrinsic == F2C_INTRINSIC_CHAR && primary != NULL && primary->rank == 0U &&
            f2c_evaluate_integer_constant(unit, primary, &value) && (value < 0 || value > 255))
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, primary), 1,
                              "CHAR argument I must be between 0 and 255 for default CHARACTER");
        return;
    }
    require_type(context, line, statement_text, intrinsic,
                 expression->intrinsic == F2C_INTRINSIC_IACHAR ||
                         expression->intrinsic == F2C_INTRINSIC_ICHAR
                     ? "C"
                     : "STRING",
                 primary, TYPE_CHARACTER);
    validate_character_kind(context, line, statement_text, intrinsic,
                            expression->intrinsic == F2C_INTRINSIC_IACHAR ||
                                    expression->intrinsic == F2C_INTRINSIC_ICHAR
                                ? "C"
                                : "STRING",
                            primary);
    if (expression->intrinsic == F2C_INTRINSIC_IACHAR ||
        expression->intrinsic == F2C_INTRINSIC_ICHAR) {
        int64_t character_length;
        kind = bound.values[1];
        validate_kind(context, unit, line, statement_text, intrinsic, kind, 0);
        if (f2c_character_constant_length(unit, primary, &character_length) &&
            character_length != 1)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, primary), 1,
                              "%s argument C must have CHARACTER length one", intrinsic);
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_INDEX ||
        expression->intrinsic == F2C_INTRINSIC_SCAN ||
        expression->intrinsic == F2C_INTRINSIC_VERIFY) {
        secondary = bound.values[1];
        back = bound.values[2];
        kind = bound.values[3];
        require_type(context, line, statement_text, intrinsic,
                     expression->intrinsic == F2C_INTRINSIC_INDEX ? "SUBSTRING" : "SET", secondary,
                     TYPE_CHARACTER);
        validate_character_kind(context, line, statement_text, intrinsic,
                                expression->intrinsic == F2C_INTRINSIC_INDEX ? "SUBSTRING" : "SET",
                                secondary);
        if (primary != NULL && secondary != NULL && primary->type == TYPE_CHARACTER &&
            secondary->type == TYPE_CHARACTER && primary->type_kind != secondary->type_kind)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, secondary), 1,
                              "%s CHARACTER arguments must have the same kind", intrinsic);
        require_type(context, line, statement_text, intrinsic, "BACK", back, TYPE_LOGICAL);
        validate_kind(context, unit, line, statement_text, intrinsic, kind, 0);
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_LEN ||
        expression->intrinsic == F2C_INTRINSIC_LEN_TRIM) {
        validate_kind(context, unit, line, statement_text, intrinsic, bound.values[1], 0);
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_REPEAT) {
        secondary = bound.values[1];
        require_type(context, line, statement_text, intrinsic, "NCOPIES", secondary, TYPE_INTEGER);
        if (primary != NULL && primary->rank != 0U)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, primary), 1,
                              "REPEAT argument STRING must be scalar");
        if (secondary != NULL && secondary->rank != 0U)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, secondary), 1,
                              "REPEAT argument NCOPIES must be scalar");
        if (secondary != NULL && f2c_evaluate_integer_constant(unit, secondary, &value) &&
            value < 0)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, secondary), 1,
                              "REPEAT argument NCOPIES must be nonnegative");
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_TRIM && primary != NULL && primary->rank != 0U)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, primary), 1,
                          "TRIM argument STRING must be scalar");
}
