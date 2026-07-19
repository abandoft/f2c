#include "semantic/validation/private.h"

#include <stdint.h>
#include <string.h>

typedef struct F2cBoundIntrinsicArguments {
    const F2cExpr *values[5];
} F2cBoundIntrinsicArguments;

static const char *intrinsic_display_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_BIT_SIZE:
        return "BIT_SIZE";
    case F2C_INTRINSIC_BTEST:
        return "BTEST";
    case F2C_INTRINSIC_IAND:
        return "IAND";
    case F2C_INTRINSIC_IBCLR:
        return "IBCLR";
    case F2C_INTRINSIC_IBITS:
        return "IBITS";
    case F2C_INTRINSIC_IBSET:
        return "IBSET";
    case F2C_INTRINSIC_IEOR:
        return "IEOR";
    case F2C_INTRINSIC_IOR:
        return "IOR";
    case F2C_INTRINSIC_ISHFT:
        return "ISHFT";
    case F2C_INTRINSIC_ISHFTC:
        return "ISHFTC";
    case F2C_INTRINSIC_NOT:
        return "NOT";
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_MVBITS:
    default:
        return "intrinsic";
    }
}

static const F2cExpr *argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static size_t argument_index(const char *const *names, size_t count, const char *name) {
    size_t index;
    for (index = 0U; index < count; ++index)
        if (strcmp(names[index], name) == 0)
            return index;
    return SIZE_MAX;
}

static F2cBoundIntrinsicArguments
bind_arguments(Context *context, size_t line, const char *statement_text,
               const char *intrinsic_name, F2cExpr *const *arguments, size_t argument_count,
               const char *const *names, size_t name_count, size_t required_count) {
    F2cBoundIntrinsicArguments bound = {{0}};
    size_t positional = 0U;
    size_t argument;
    int saw_keyword = 0;
    for (argument = 0U; argument < argument_count; ++argument) {
        const F2cExpr *actual = arguments[argument];
        size_t index;
        if (actual != NULL && actual->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            saw_keyword = 1;
            index =
                actual->text != NULL ? argument_index(names, name_count, actual->text) : SIZE_MAX;
            if (index == SIZE_MAX) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "%s has no argument named '%s'", intrinsic_name,
                                  actual->text != NULL ? actual->text : "");
                continue;
            }
        } else {
            if (saw_keyword) {
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text, actual), 1,
                                  "positional argument in %s cannot follow a keyword argument",
                                  intrinsic_name);
            }
            index = positional++;
            if (index >= name_count)
                continue;
        }
        if (bound.values[index] != NULL) {
            f2c_diagnostic_at(
                context, line, f2c_validation_expression_start_column(statement_text, actual), 1,
                "%s argument '%s' is specified more than once", intrinsic_name, names[index]);
            continue;
        }
        bound.values[index] = argument_value(actual);
    }
    for (argument = 0U; argument < required_count; ++argument) {
        if (bound.values[argument] == NULL)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, NULL), 1,
                              "%s requires argument %s", intrinsic_name, names[argument]);
    }
    return bound;
}

static void require_integer(Context *context, size_t line, const char *statement_text,
                            const char *intrinsic_name, const char *argument_name,
                            const F2cExpr *argument) {
    if (argument != NULL && argument->type != TYPE_INTEGER)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument %s must be INTEGER", intrinsic_name, argument_name);
}

static int integer_bit_size(const F2cExpr *expression) {
    const int kind =
        expression != NULL && expression->type == TYPE_INTEGER
            ? (expression->type_kind != 0 ? expression->type_kind : f2c_default_kind(TYPE_INTEGER))
            : 0;
    return kind == 1 || kind == 2 || kind == 4 || kind == 8 ? kind * 8 : 0;
}

static int integer_kind(const F2cExpr *expression) {
    if (expression == NULL || expression->type != TYPE_INTEGER)
        return 0;
    return expression->type_kind != 0 ? expression->type_kind : f2c_default_kind(TYPE_INTEGER);
}

static int constant_integer(Unit *unit, const F2cExpr *expression, int64_t *value) {
    return expression != NULL && expression->type == TYPE_INTEGER && expression->rank == 0U &&
           f2c_evaluate_integer_constant(unit, expression, value);
}

static void validate_position(Context *context, Unit *unit, size_t line, const char *statement_text,
                              const char *intrinsic_name, const char *argument_name,
                              const F2cExpr *argument, int width) {
    int64_t value;
    if (width != 0 && constant_integer(unit, argument, &value) &&
        (value < 0 || value >= (int64_t)width))
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, argument), 1,
            "%s argument %s must be between 0 and %d", intrinsic_name, argument_name, width - 1);
}

static void validate_shift(Context *context, Unit *unit, size_t line, const char *statement_text,
                           const char *intrinsic_name, const F2cExpr *argument, int64_t limit) {
    int64_t value;
    if (constant_integer(unit, argument, &value) && (value < -limit || value > limit))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, argument), 1,
                          "%s argument SHIFT must be between %lld and %lld", intrinsic_name,
                          (long long)-limit, (long long)limit);
}

void f2c_validation_bit_intrinsic(Context *context, Unit *unit, size_t line,
                                  const char *statement_text, F2cExpr *expression) {
    static const char *const one[] = {"i"};
    static const char *const binary[] = {"i", "j"};
    static const char *const position[] = {"i", "pos"};
    static const char *const bits[] = {"i", "pos", "len"};
    static const char *const shift[] = {"i", "shift"};
    static const char *const circular[] = {"i", "shift", "size"};
    F2cBoundIntrinsicArguments bound;
    const char *const *names = one;
    size_t name_count = 1U;
    const F2cExpr *model;
    const char *display_name;
    int width;
    int64_t first;
    int64_t second;
    size_t index;
    if (expression == NULL || expression->intrinsic == F2C_INTRINSIC_NONE)
        return;
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_IAND:
    case F2C_INTRINSIC_IEOR:
    case F2C_INTRINSIC_IOR:
        names = binary;
        name_count = 2U;
        break;
    case F2C_INTRINSIC_BTEST:
    case F2C_INTRINSIC_IBCLR:
    case F2C_INTRINSIC_IBSET:
        names = position;
        name_count = 2U;
        break;
    case F2C_INTRINSIC_IBITS:
        names = bits;
        name_count = 3U;
        break;
    case F2C_INTRINSIC_ISHFT:
        names = shift;
        name_count = 2U;
        break;
    case F2C_INTRINSIC_ISHFTC:
        names = circular;
        name_count = 3U;
        break;
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_NOT:
        break;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_MVBITS:
    default:
        return;
    }
    display_name = intrinsic_display_name(expression->intrinsic);
    bound = bind_arguments(context, line, statement_text, display_name, expression->children,
                           expression->child_count, names, name_count,
                           expression->intrinsic == F2C_INTRINSIC_ISHFTC ? 2U : name_count);
    for (index = 0U; index < name_count; ++index)
        require_integer(context, line, statement_text, display_name, names[index],
                        bound.values[index]);
    model = bound.values[0];
    width = integer_bit_size(model);
    if ((expression->intrinsic == F2C_INTRINSIC_IAND ||
         expression->intrinsic == F2C_INTRINSIC_IEOR ||
         expression->intrinsic == F2C_INTRINSIC_IOR) &&
        bound.values[0] != NULL && bound.values[1] != NULL &&
        bound.values[0]->type == TYPE_INTEGER && bound.values[1]->type == TYPE_INTEGER &&
        integer_kind(bound.values[0]) != integer_kind(bound.values[1]))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, bound.values[1]),
                          1, "%s arguments I and J must have the same INTEGER kind", display_name);
    if (expression->intrinsic == F2C_INTRINSIC_BTEST ||
        expression->intrinsic == F2C_INTRINSIC_IBCLR ||
        expression->intrinsic == F2C_INTRINSIC_IBSET)
        validate_position(context, unit, line, statement_text, display_name, "POS", bound.values[1],
                          width);
    if (expression->intrinsic == F2C_INTRINSIC_IBITS && width != 0) {
        const int position_known = constant_integer(unit, bound.values[1], &first);
        const int length_known = constant_integer(unit, bound.values[2], &second);
        if (position_known && first < 0)
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[1]), 1,
                "IBITS argument POS must be nonnegative");
        if (length_known && second < 0)
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[2]), 1,
                "IBITS argument LEN must be nonnegative");
        if (position_known && length_known && first >= 0 && second >= 0 &&
            (first > (int64_t)width || second > (int64_t)width - first))
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, expression), 1,
                              "IBITS requires POS + LEN to be at most BIT_SIZE(I) (%d)", width);
    }
    if (expression->intrinsic == F2C_INTRINSIC_ISHFT && width != 0)
        validate_shift(context, unit, line, statement_text, "ISHFT", bound.values[1], width);
    if (expression->intrinsic == F2C_INTRINSIC_ISHFTC && width != 0) {
        int64_t size = width;
        const int size_known =
            bound.values[2] == NULL || constant_integer(unit, bound.values[2], &size);
        if (size_known && (size <= 0 || size > width)) {
            f2c_diagnostic_at(
                context, line,
                f2c_validation_expression_start_column(statement_text, bound.values[2]), 1,
                "ISHFTC argument SIZE must be between 1 and BIT_SIZE(I) (%d)", width);
        } else if (size_known) {
            validate_shift(context, unit, line, statement_text, "ISHFTC", bound.values[1], size);
        } else {
            validate_shift(context, unit, line, statement_text, "ISHFTC", bound.values[1], width);
        }
    }
}
