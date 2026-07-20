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
                         const char *intrinsic, const char *argument_name,
                         const F2cExpr *argument, Type type) {
    if (argument != NULL && argument->type != type)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument %s must be %s", intrinsic, argument_name,
                          type == TYPE_CHARACTER ? "CHARACTER"
                          : type == TYPE_INTEGER ? "INTEGER"
                                                 : "LOGICAL");
}

static int same_scalar_expression(const F2cExpr *left, const F2cExpr *right) {
    size_t child;
    if (left == right)
        return 1;
    if (left == NULL || right == NULL || left->kind != right->kind || left->type != right->type ||
        left->rank != 0U || right->rank != 0U || left->child_count != right->child_count ||
        left->symbol != right->symbol)
        return 0;
    if ((left->text == NULL) != (right->text == NULL) ||
        (left->text != NULL && strcmp(left->text, right->text) != 0))
        return 0;
    for (child = 0U; child < left->child_count; ++child) {
        if (!same_scalar_expression(left->children[child], right->children[child]))
            return 0;
    }
    return 1;
}

static int declared_character_length(Unit *unit, const Symbol *symbol, int64_t *length) {
    if (symbol == NULL || length == NULL)
        return 0;
    if (symbol->character_length_expression != NULL)
        return f2c_evaluate_integer_constant(unit, symbol->character_length_expression, length);
    if (symbol->character_length_syntax.count != 0U)
        return f2c_evaluate_integer_syntax(unit, symbol->character_length_syntax, length);
    if (symbol->character_length == NULL || strcmp(symbol->character_length, "1") == 0) {
        *length = 1;
        return 1;
    }
    return 0;
}

static int constant_substring_length(Unit *unit, const F2cExpr *expression, int64_t *length) {
    const F2cExpr *selector;
    const F2cExpr *lower = NULL;
    const F2cExpr *upper = NULL;
    int64_t lower_value = 1;
    int64_t upper_value;
    uint64_t distance;
    if (expression == NULL || expression->kind != F2C_EXPR_SUBSTRING ||
        expression->symbol == NULL || expression->child_count != 1U)
        return 0;
    selector = expression->children[0];
    if (selector->kind == F2C_EXPR_ARRAY_SECTION) {
        if (selector->child_count != 3U)
            return 0;
        if (selector->children[0]->kind != F2C_EXPR_INVALID)
            lower = selector->children[0];
        if (selector->children[1]->kind != F2C_EXPR_INVALID)
            upper = selector->children[1];
    } else {
        lower = selector;
        upper = selector;
    }
    if (lower != NULL && upper != NULL && same_scalar_expression(lower, upper)) {
        *length = 1;
        return 1;
    }
    if (lower != NULL && !f2c_evaluate_integer_constant(unit, lower, &lower_value))
        return 0;
    if (upper != NULL) {
        if (!f2c_evaluate_integer_constant(unit, upper, &upper_value))
            return 0;
    } else if (!declared_character_length(unit, expression->symbol, &upper_value)) {
        return 0;
    }
    if (upper_value < lower_value) {
        *length = 0;
        return 1;
    }
    distance = (uint64_t)upper_value - (uint64_t)lower_value;
    if (distance >= (uint64_t)INT64_MAX)
        return 0;
    *length = (int64_t)(distance + 1U);
    return 1;
}

static int constant_character_length(Unit *unit, const F2cExpr *expression, int64_t *length) {
    const F2cExpr *source;
    if (expression == NULL || length == NULL || expression->type != TYPE_CHARACTER)
        return 0;
    if (expression->kind == F2C_EXPR_STRING_LITERAL) {
        const size_t value = f2c_character_literal_length(expression->text);
        if ((uint64_t)value > (uint64_t)INT64_MAX)
            return 0;
        *length = (int64_t)value;
        return 1;
    }
    if (expression->kind == F2C_EXPR_SUBSTRING)
        return constant_substring_length(unit, expression, length);
    if (expression->symbol != NULL)
        return declared_character_length(unit, expression->symbol, length);
    if (expression->kind != F2C_EXPR_CALL)
        return 0;
    if (expression->intrinsic == F2C_INTRINSIC_CHAR ||
        expression->intrinsic == F2C_INTRINSIC_ACHAR) {
        *length = 1;
        return 1;
    }
    if (expression->intrinsic != F2C_INTRINSIC_ADJUSTL &&
        expression->intrinsic != F2C_INTRINSIC_ADJUSTR)
        return 0;
    source = f2c_intrinsic_argument(expression->children, expression->child_count, "string", 0U);
    return constant_character_length(unit, source, length);
}

static void validate_kind(Context *context, Unit *unit, size_t line, const char *statement_text,
                          const char *intrinsic, const F2cExpr *kind, int character_kind) {
    int64_t value;
    if (kind == NULL)
        return;
    if (kind->type != TYPE_INTEGER || kind->rank != 0U ||
        !f2c_expression_is_initialization_constant(kind) ||
        !f2c_evaluate_integer_constant(unit, kind, &value) ||
        (character_kind ? value != 1
                        : (value != 1 && value != 2 && value != 4 && value != 8)))
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
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument %s uses an unsupported CHARACTER kind", intrinsic,
                          argument_name);
}

void f2c_validation_character_intrinsic(Context *context, Unit *unit, size_t line,
                                        const char *statement_text, F2cExpr *expression) {
    static const char *const code[] = {"i", "kind"};
    static const char *const string[] = {"string"};
    static const char *const character_code[] = {"c", "kind"};
    static const char *const search[] = {"string", "substring", "back", "kind"};
    static const char *const length[] = {"string", "kind"};
    static const char *const repetition[] = {"string", "ncopies"};
    static const char *const set_search[] = {"string", "set", "back", "kind"};
    const char *const *names = string;
    size_t name_count = 1U;
    size_t required_count = 1U;
    F2cBoundIntrinsicArguments bound;
    const char *intrinsic;
    const F2cExpr *primary;
    const F2cExpr *secondary = NULL;
    const F2cExpr *back = NULL;
    const F2cExpr *kind = NULL;
    int64_t value;
    if (expression == NULL || !f2c_intrinsic_is_character(expression->intrinsic))
        return;
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_ACHAR:
    case F2C_INTRINSIC_CHAR:
        names = code;
        name_count = 2U;
        break;
    case F2C_INTRINSIC_IACHAR:
    case F2C_INTRINSIC_ICHAR:
        names = character_code;
        name_count = 2U;
        break;
    case F2C_INTRINSIC_INDEX:
        names = search;
        name_count = 4U;
        required_count = 2U;
        break;
    case F2C_INTRINSIC_LEN:
    case F2C_INTRINSIC_LEN_TRIM:
        names = length;
        name_count = 2U;
        break;
    case F2C_INTRINSIC_REPEAT:
        names = repetition;
        name_count = 2U;
        required_count = 2U;
        break;
    case F2C_INTRINSIC_SCAN:
    case F2C_INTRINSIC_VERIFY:
        names = set_search;
        name_count = 4U;
        required_count = 2U;
        break;
    case F2C_INTRINSIC_ADJUSTL:
    case F2C_INTRINSIC_ADJUSTR:
    case F2C_INTRINSIC_TRIM:
        break;
    default:
        return;
    }
    intrinsic = display_name(expression->intrinsic);
    bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, intrinsic, expression->children, expression->child_count,
        names, name_count, required_count);
    primary = bound.values[0];
    if (expression->intrinsic == F2C_INTRINSIC_ACHAR ||
        expression->intrinsic == F2C_INTRINSIC_CHAR) {
        require_type(context, line, statement_text, intrinsic, "I", primary, TYPE_INTEGER);
        kind = bound.values[1];
        validate_kind(context, unit, line, statement_text, intrinsic, kind, 1);
        if (expression->intrinsic == F2C_INTRINSIC_CHAR && primary != NULL &&
            primary->rank == 0U && f2c_evaluate_integer_constant(unit, primary, &value) &&
            (value < 0 || value > 255))
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
        if (constant_character_length(unit, primary, &character_length) && character_length != 1)
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
                     expression->intrinsic == F2C_INTRINSIC_INDEX ? "SUBSTRING" : "SET",
                     secondary, TYPE_CHARACTER);
        validate_character_kind(
            context, line, statement_text, intrinsic,
            expression->intrinsic == F2C_INTRINSIC_INDEX ? "SUBSTRING" : "SET", secondary);
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
        require_type(context, line, statement_text, intrinsic, "NCOPIES", secondary,
                     TYPE_INTEGER);
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
