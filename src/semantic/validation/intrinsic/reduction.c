#include "semantic/validation/private.h"

#include "semantic/numeric_model.h"
#include "semantic/validation/intrinsic/arguments.h"

#include <stdint.h>

static const char *reduction_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_ALL:
        return "ALL";
    case F2C_INTRINSIC_ANY:
        return "ANY";
    case F2C_INTRINSIC_COUNT:
        return "COUNT";
    case F2C_INTRINSIC_DOT_PRODUCT:
        return "DOT_PRODUCT";
    case F2C_INTRINSIC_MAXLOC:
        return "MAXLOC";
    case F2C_INTRINSIC_MAXVAL:
        return "MAXVAL";
    case F2C_INTRINSIC_MINLOC:
        return "MINLOC";
    case F2C_INTRINSIC_MINVAL:
        return "MINVAL";
    case F2C_INTRINSIC_PRODUCT:
        return "PRODUCT";
    case F2C_INTRINSIC_SUM:
        return "SUM";
    case F2C_INTRINSIC_NONE:
    default:
        return "reduction intrinsic";
    }
}

static int expression_kind(const F2cExpr *expression) {
    return expression != NULL && expression->type_kind != 0
               ? expression->type_kind
               : f2c_default_kind(expression != NULL ? expression->type : TYPE_UNKNOWN);
}

static int supported_numeric(const F2cExpr *expression) {
    return expression != NULL &&
           f2c_numeric_model(expression->type, expression_kind(expression)) != NULL;
}

static void diagnose_argument(Context *context, size_t line, const char *statement_text,
                              const char *intrinsic, const char *argument_name,
                              const F2cExpr *argument, const char *requirement) {
    f2c_diagnostic_at(context, line,
                      f2c_validation_expression_start_column(statement_text, argument), 1,
                      "%s argument %s must be %s", intrinsic, argument_name, requirement);
}

static void validate_dimension(Context *context, Unit *unit, size_t line,
                               const char *statement_text, const char *intrinsic,
                               const F2cExpr *dimension, size_t rank) {
    int64_t value;
    if (dimension == NULL)
        return;
    if (dimension->type != TYPE_INTEGER || dimension->rank != 0U) {
        diagnose_argument(context, line, statement_text, intrinsic, "DIM", dimension,
                          "a scalar INTEGER expression");
    } else if (f2c_evaluate_integer_constant(unit, dimension, &value) &&
               (value < 1 || (uint64_t)value > rank)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, dimension), 1,
                          "DIM in %s must be between 1 and array rank %zu", intrinsic, rank);
    }
}

static void validate_kind(Context *context, Unit *unit, size_t line, const char *statement_text,
                          const char *intrinsic, const F2cExpr *kind) {
    int64_t value;
    if (kind == NULL)
        return;
    if (kind->type != TYPE_INTEGER || kind->rank != 0U ||
        !f2c_evaluate_integer_constant(unit, kind, &value) ||
        (value != 1 && value != 2 && value != 4 && value != 8))
        diagnose_argument(context, line, statement_text, intrinsic, "KIND", kind,
                          "a supported scalar INTEGER constant (1, 2, 4, or 8)");
}

static void validate_mask(Context *context, size_t line, const char *statement_text,
                          const char *intrinsic, const F2cExpr *array, const F2cExpr *mask) {
    size_t dimension;
    if (mask == NULL)
        return;
    if (mask->type != TYPE_LOGICAL) {
        diagnose_argument(context, line, statement_text, intrinsic, "MASK", mask,
                          "a LOGICAL scalar or an array conformable with ARRAY");
        return;
    }
    if (mask->rank == 0U || array == NULL)
        return;
    if (mask->rank != array->rank) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, mask), 1,
                          "MASK in %s has rank %zu but ARRAY has rank %zu", intrinsic, mask->rank,
                          array->rank);
    } else if (f2c_validation_shapes_mismatch(array, mask, &dimension)) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, mask), 1,
                          "MASK in %s is not conformable with ARRAY in dimension %zu", intrinsic,
                          dimension + 1U);
    }
}

static F2cBoundIntrinsicArguments bind_reduction_arguments(
    Context *context, size_t line, const char *statement_text, F2cExpr *expression) {
    static const char *const logical_names[] = {"mask", "dim"};
    static const char *const count_names[] = {"mask", "dim", "kind"};
    static const char *const dot_names[] = {"vector_a", "vector_b"};
    static const char *const value_names[] = {"array", "dim", "mask"};
    static const char *const location_names[] = {"array", "dim", "mask", "kind", "back"};
    const char *const *names = value_names;
    size_t count = sizeof(value_names) / sizeof(value_names[0]);
    if (expression->intrinsic == F2C_INTRINSIC_ALL ||
        expression->intrinsic == F2C_INTRINSIC_ANY) {
        names = logical_names;
        count = sizeof(logical_names) / sizeof(logical_names[0]);
    } else if (expression->intrinsic == F2C_INTRINSIC_COUNT) {
        names = count_names;
        count = sizeof(count_names) / sizeof(count_names[0]);
    } else if (expression->intrinsic == F2C_INTRINSIC_DOT_PRODUCT) {
        names = dot_names;
        count = sizeof(dot_names) / sizeof(dot_names[0]);
    } else if (expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
               expression->intrinsic == F2C_INTRINSIC_MINLOC) {
        names = location_names;
        count = sizeof(location_names) / sizeof(location_names[0]);
    }
    return f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, reduction_name(expression->intrinsic), expression->children,
        expression->child_count, names, count,
        expression->intrinsic == F2C_INTRINSIC_DOT_PRODUCT ? 2U : 1U);
}

static void validate_dot_product(Context *context, size_t line, const char *statement_text,
                                 const F2cBoundIntrinsicArguments *arguments) {
    const F2cExpr *left = arguments->values[0];
    const F2cExpr *right = arguments->values[1];
    size_t dimension;
    if (left != NULL && left->rank != 1U)
        diagnose_argument(context, line, statement_text, "DOT_PRODUCT", "VECTOR_A", left,
                          "a rank-one array");
    if (right != NULL && right->rank != 1U)
        diagnose_argument(context, line, statement_text, "DOT_PRODUCT", "VECTOR_B", right,
                          "a rank-one array");
    if (left == NULL || right == NULL)
        return;
    if (!((left->type == TYPE_LOGICAL && right->type == TYPE_LOGICAL) ||
          (supported_numeric(left) && supported_numeric(right)))) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, right), 1,
                          "DOT_PRODUCT vectors must both be numeric or both be LOGICAL");
    }
    if (left->rank == 1U && right->rank == 1U &&
        f2c_validation_shapes_mismatch(left, right, &dimension))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, right), 1,
                          "DOT_PRODUCT vectors are not conformable");
}

void f2c_validation_reduction_intrinsic(Context *context, Unit *unit, size_t line,
                                        const char *statement_text, F2cExpr *expression) {
    F2cBoundIntrinsicArguments arguments;
    const F2cExpr *array;
    const F2cExpr *dimension;
    const F2cExpr *mask;
    const F2cExpr *kind;
    const F2cExpr *back;
    const char *name;
    if (expression == NULL || !f2c_intrinsic_is_reduction(expression->intrinsic))
        return;
    arguments =
        bind_reduction_arguments(context, line, statement_text, expression);
    if (expression->intrinsic == F2C_INTRINSIC_DOT_PRODUCT) {
        validate_dot_product(context, line, statement_text, &arguments);
        return;
    }
    name = reduction_name(expression->intrinsic);
    array = arguments.values[0];
    dimension = arguments.values[1];
    mask = expression->intrinsic == F2C_INTRINSIC_COUNT ? NULL : arguments.values[2];
    kind = expression->intrinsic == F2C_INTRINSIC_COUNT
               ? arguments.values[2]
               : (expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
                          expression->intrinsic == F2C_INTRINSIC_MINLOC
                      ? arguments.values[3]
                      : NULL);
    back = expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
                   expression->intrinsic == F2C_INTRINSIC_MINLOC
               ? arguments.values[4]
               : NULL;
    if (array != NULL && array->rank == 0U)
        diagnose_argument(context, line, statement_text, name,
                          expression->intrinsic == F2C_INTRINSIC_ALL ||
                                  expression->intrinsic == F2C_INTRINSIC_ANY ||
                                  expression->intrinsic == F2C_INTRINSIC_COUNT
                              ? "MASK"
                              : "ARRAY",
                          array, "an array");
    if (array != NULL) {
        const int logical = expression->intrinsic == F2C_INTRINSIC_ALL ||
                            expression->intrinsic == F2C_INTRINSIC_ANY ||
                            expression->intrinsic == F2C_INTRINSIC_COUNT;
        const int extrema = expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
                            expression->intrinsic == F2C_INTRINSIC_MAXVAL ||
                            expression->intrinsic == F2C_INTRINSIC_MINLOC ||
                            expression->intrinsic == F2C_INTRINSIC_MINVAL;
        if (logical && array->type != TYPE_LOGICAL)
            diagnose_argument(context, line, statement_text, name, "MASK", array,
                              "a LOGICAL array");
        else if (extrema && array->type != TYPE_INTEGER && array->type != TYPE_REAL &&
                 array->type != TYPE_DOUBLE)
            diagnose_argument(context, line, statement_text, name, "ARRAY", array,
                              "an INTEGER or REAL array");
        else if (!logical && !extrema && !supported_numeric(array))
            diagnose_argument(context, line, statement_text, name, "ARRAY", array,
                              "an array of a supported INTEGER, REAL, or COMPLEX kind");
    }
    validate_dimension(context, unit, line, statement_text, name, dimension,
                       array != NULL ? array->rank : 0U);
    validate_mask(context, line, statement_text, name, array, mask);
    validate_kind(context, unit, line, statement_text, name, kind);
    if (back != NULL && (back->type != TYPE_LOGICAL || back->rank != 0U))
        diagnose_argument(context, line, statement_text, name, "BACK", back,
                          "a scalar LOGICAL expression");
}
