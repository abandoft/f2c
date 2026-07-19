#include "semantic/validation/private.h"

#include "semantic/validation/intrinsic/arguments.h"

#include <stdint.h>

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
    case F2C_INTRINSIC_MVBITS:
        return "MVBITS";
    case F2C_INTRINSIC_NONE:
    default:
        return "intrinsic";
    }
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
    bound = f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, display_name, expression->children,
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

void f2c_validation_mvbits(Context *context, Unit *unit, F2cStatement *statement) {
    static const char *const names[] = {"from", "frompos", "len", "to", "topos"};
    F2cBoundIntrinsicArguments bound;
    const F2cExpr *shape = NULL;
    const F2cExpr *from;
    const F2cExpr *to;
    size_t rank = 0U;
    size_t argument;
    int width;
    int64_t from_position;
    int64_t length;
    int64_t to_position;
    int from_position_known;
    int length_known;
    int to_position_known;
    if (statement == NULL || statement->kind != F2C_STMT_CALL || statement->name == NULL ||
        !f2c_is_intrinsic_subroutine(statement->name))
        return;
    statement->intrinsic = F2C_INTRINSIC_MVBITS;
    if (statement->item_count != 5U)
        f2c_diagnostic_at(context, statement->line, statement->name_span.begin.column, 1,
                          "MVBITS requires exactly 5 arguments");
    bound = f2c_validation_bind_intrinsic_arguments(
        context, statement->line, statement->text, "MVBITS", statement->arguments,
        statement->item_count, names, 5U, 5U);
    for (argument = 0U; argument < 5U; ++argument)
        require_integer(context, statement->line, statement->text, "MVBITS", names[argument],
                        bound.values[argument]);
    from = bound.values[0];
    to = bound.values[3];
    if (from != NULL && to != NULL && from->type == TYPE_INTEGER && to->type == TYPE_INTEGER &&
        integer_kind(from) != integer_kind(to))
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, to), 1,
                          "MVBITS arguments FROM and TO must have the same INTEGER kind");
    if (to != NULL && !to->definable)
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, to), 1,
                          "MVBITS argument TO must be definable");
    for (argument = 0U; argument < 5U; ++argument) {
        const F2cExpr *value = bound.values[argument];
        size_t dimension;
        if (value == NULL || value->rank == 0U)
            continue;
        if (shape == NULL) {
            shape = value;
            rank = value->rank;
            continue;
        }
        if (value->rank != rank) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, value), 1,
                              "MVBITS has nonconformable argument ranks %zu and %zu", rank,
                              value->rank);
        } else if (f2c_validation_shapes_mismatch(shape, value, &dimension)) {
            f2c_diagnostic_at(context, statement->line,
                              f2c_validation_expression_start_column(statement->text, value), 1,
                              "MVBITS has nonconformable extent in dimension %zu", dimension + 1U);
        }
    }
    if (rank != 0U && (to == NULL || to->rank != rank))
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, to), 1,
                          "MVBITS argument TO must be an array conformable with every array "
                          "input");
    width = integer_bit_size(from);
    from_position_known = constant_integer(unit, bound.values[1], &from_position);
    length_known = constant_integer(unit, bound.values[2], &length);
    to_position_known = constant_integer(unit, bound.values[4], &to_position);
    if (from_position_known && from_position < 0)
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, bound.values[1]),
                          1, "MVBITS argument FROMPOS must be nonnegative");
    if (length_known && length < 0)
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, bound.values[2]),
                          1, "MVBITS argument LEN must be nonnegative");
    if (to_position_known && to_position < 0)
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, bound.values[4]),
                          1, "MVBITS argument TOPOS must be nonnegative");
    if (width != 0 && from_position_known && length_known && from_position >= 0 && length >= 0 &&
        ((uint64_t)from_position > (uint64_t)width ||
         (uint64_t)length > (uint64_t)width - (uint64_t)from_position))
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, bound.values[0]),
                          1, "MVBITS requires FROMPOS + LEN to be at most BIT_SIZE(FROM) (%d)",
                          width);
    if (width != 0 && to_position_known && length_known && to_position >= 0 && length >= 0 &&
        ((uint64_t)to_position > (uint64_t)width ||
         (uint64_t)length > (uint64_t)width - (uint64_t)to_position))
        f2c_diagnostic_at(context, statement->line,
                          f2c_validation_expression_start_column(statement->text, to), 1,
                          "MVBITS requires TOPOS + LEN to be at most BIT_SIZE(TO) (%d)", width);
}
