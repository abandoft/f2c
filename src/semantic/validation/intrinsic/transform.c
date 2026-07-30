#include "semantic/validation/private.h"

#include "semantic/validation/intrinsic/arguments.h"

#include <stdint.h>
#include <string.h>

static const char *transform_name(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_CSHIFT:
        return "CSHIFT";
    case F2C_INTRINSIC_EOSHIFT:
        return "EOSHIFT";
    case F2C_INTRINSIC_FINDLOC:
        return "FINDLOC";
    case F2C_INTRINSIC_MATMUL:
        return "MATMUL";
    case F2C_INTRINSIC_PACK:
        return "PACK";
    case F2C_INTRINSIC_RESHAPE:
        return "RESHAPE";
    case F2C_INTRINSIC_SPREAD:
        return "SPREAD";
    case F2C_INTRINSIC_TRANSPOSE:
        return "TRANSPOSE";
    case F2C_INTRINSIC_UNPACK:
        return "UNPACK";
    case F2C_INTRINSIC_NONE:
    default:
        return "transformational intrinsic";
    }
}

static void diagnose_argument(Context *context, size_t line, const char *statement_text,
                              const char *intrinsic, const char *argument_name,
                              const F2cExpr *argument, const char *requirement) {
    f2c_diagnostic_at(context, line,
                      f2c_validation_expression_start_column(statement_text, argument), 1,
                      "%s argument %s must be %s", intrinsic, argument_name, requirement);
}

static int same_element_type(const F2cExpr *left, const F2cExpr *right) {
    if (left == NULL || right == NULL || left->type != right->type ||
        left->type_kind != right->type_kind)
        return 0;
    return left->type != TYPE_DERIVED || left->derived_type == right->derived_type;
}

static void validate_scalar_integer(Context *context, size_t line, const char *statement_text,
                                    const char *intrinsic, const char *argument_name,
                                    const F2cExpr *argument) {
    if (argument != NULL && (argument->type != TYPE_INTEGER || argument->rank != 0U))
        diagnose_argument(context, line, statement_text, intrinsic, argument_name, argument,
                          "a scalar INTEGER expression");
}

static int constant_dimension(Context *context, Unit *unit, size_t line, const char *statement_text,
                              const char *intrinsic, const F2cExpr *dimension, size_t rank,
                              int64_t *value_out) {
    int64_t value;
    if (dimension == NULL || dimension->type != TYPE_INTEGER || dimension->rank != 0U ||
        !f2c_evaluate_integer_constant(unit, dimension, &value))
        return 0;
    if (value < 1 || (uint64_t)value > rank) {
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, dimension), 1,
                          "DIM in %s must be between 1 and %zu", intrinsic, rank);
        return 0;
    }
    *value_out = value;
    return 1;
}

static F2cBoundIntrinsicArguments bind_arguments(Context *context, size_t line,
                                                 const char *statement_text, F2cExpr *expression) {
    static const char *const shift_names[] = {"array", "shift", "dim"};
    static const char *const end_shift_names[] = {"array", "shift", "boundary", "dim"};
    static const char *const findloc_names[] = {"array", "value", "dim", "mask", "kind", "back"};
    static const char *const matmul_names[] = {"matrix_a", "matrix_b"};
    static const char *const pack_names[] = {"array", "mask", "vector"};
    static const char *const reshape_names[] = {"source", "shape", "pad", "order"};
    static const char *const spread_names[] = {"source", "dim", "ncopies"};
    static const char *const transpose_names[] = {"matrix"};
    static const char *const unpack_names[] = {"vector", "mask", "field"};
    const char *const *names;
    size_t count;
    size_t required;
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_CSHIFT:
        names = shift_names;
        count = sizeof(shift_names) / sizeof(shift_names[0]);
        required = 2U;
        break;
    case F2C_INTRINSIC_EOSHIFT:
        names = end_shift_names;
        count = sizeof(end_shift_names) / sizeof(end_shift_names[0]);
        required = 2U;
        break;
    case F2C_INTRINSIC_FINDLOC:
        names = findloc_names;
        count = sizeof(findloc_names) / sizeof(findloc_names[0]);
        required = 2U;
        break;
    case F2C_INTRINSIC_MATMUL:
        names = matmul_names;
        count = sizeof(matmul_names) / sizeof(matmul_names[0]);
        required = 2U;
        break;
    case F2C_INTRINSIC_PACK:
        names = pack_names;
        count = sizeof(pack_names) / sizeof(pack_names[0]);
        required = 2U;
        break;
    case F2C_INTRINSIC_RESHAPE:
        names = reshape_names;
        count = sizeof(reshape_names) / sizeof(reshape_names[0]);
        required = 2U;
        break;
    case F2C_INTRINSIC_SPREAD:
        names = spread_names;
        count = sizeof(spread_names) / sizeof(spread_names[0]);
        required = 3U;
        break;
    case F2C_INTRINSIC_TRANSPOSE:
        names = transpose_names;
        count = sizeof(transpose_names) / sizeof(transpose_names[0]);
        required = 1U;
        break;
    case F2C_INTRINSIC_UNPACK:
        names = unpack_names;
        count = sizeof(unpack_names) / sizeof(unpack_names[0]);
        required = 3U;
        break;
    case F2C_INTRINSIC_NONE:
    default: {
        F2cBoundIntrinsicArguments empty = {{0}};
        return empty;
    }
    }
    return f2c_validation_bind_intrinsic_arguments(
        context, line, statement_text, transform_name(expression->intrinsic), expression->children,
        expression->child_count, names, count, required);
}

static void validate_mask(Context *context, size_t line, const char *statement_text,
                          const char *intrinsic, const F2cExpr *array, const F2cExpr *mask,
                          int scalar_allowed) {
    size_t dimension;
    if (mask == NULL)
        return;
    if (mask->type != TYPE_LOGICAL || (!scalar_allowed && mask->rank == 0U)) {
        diagnose_argument(context, line, statement_text, intrinsic, "MASK", mask,
                          scalar_allowed ? "a LOGICAL scalar or an array conformable with ARRAY"
                                         : "a LOGICAL array");
        return;
    }
    if (mask->rank == 0U || array == NULL)
        return;
    if (mask->rank != array->rank) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, mask), 1,
            "MASK in %s has rank %zu but ARRAY has rank %zu", intrinsic, mask->rank, array->rank);
    } else if (f2c_validation_shapes_mismatch(array, mask, &dimension)) {
        f2c_diagnostic_at(
            context, line, f2c_validation_expression_start_column(statement_text, mask), 1,
            "MASK in %s is not conformable with ARRAY in dimension %zu", intrinsic, dimension + 1U);
    }
}

static void validate_matrix(Context *context, size_t line, const char *statement_text,
                            F2cIntrinsicId intrinsic, const F2cBoundIntrinsicArguments *arguments) {
    const F2cExpr *left = arguments->values[0];
    const F2cExpr *right = intrinsic == F2C_INTRINSIC_TRANSPOSE ? NULL : arguments->values[1];
    if (intrinsic == F2C_INTRINSIC_TRANSPOSE) {
        if (left != NULL && left->rank != 2U)
            diagnose_argument(context, line, statement_text, "TRANSPOSE", "MATRIX", left,
                              "a rank-two array");
        return;
    }
    if (left != NULL && (left->rank < 1U || left->rank > 2U))
        diagnose_argument(context, line, statement_text, "MATMUL", "MATRIX_A", left,
                          "a rank-one or rank-two array");
    if (right != NULL && (right->rank < 1U || right->rank > 2U))
        diagnose_argument(context, line, statement_text, "MATMUL", "MATRIX_B", right,
                          "a rank-one or rank-two array");
    if (left == NULL || right == NULL)
        return;
    if (left->rank == 1U && right->rank == 1U)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, right), 1,
                          "MATMUL does not accept two rank-one operands; use DOT_PRODUCT");
    if (!((left->type == TYPE_LOGICAL && right->type == TYPE_LOGICAL) ||
          (f2c_type_is_numeric(left->type) && f2c_type_is_numeric(right->type))))
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, right), 1,
                          "MATMUL operands must both be numeric or both be LOGICAL");
    if (left->rank >= 1U && left->rank <= 2U && right->rank >= 1U && right->rank <= 2U &&
        left->shape.dimensions[left->rank - 1U].extent_known &&
        right->shape.dimensions[0].extent_known &&
        left->shape.dimensions[left->rank - 1U].extent != right->shape.dimensions[0].extent)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, right), 1,
                          "MATMUL inner extents are not conformable (%llu and %llu)",
                          (unsigned long long)left->shape.dimensions[left->rank - 1U].extent,
                          (unsigned long long)right->shape.dimensions[0].extent);
}

static void validate_reshape(Context *context, Unit *unit, size_t line, const char *statement_text,
                             const F2cBoundIntrinsicArguments *arguments) {
    const F2cExpr *source = arguments->values[0];
    const F2cExpr *shape = arguments->values[1];
    const F2cExpr *pad = arguments->values[2];
    const F2cExpr *order = arguments->values[3];
    size_t element;
    if (source != NULL && source->rank == 0U)
        diagnose_argument(context, line, statement_text, "RESHAPE", "SOURCE", source, "an array");
    if (shape != NULL && (shape->type != TYPE_INTEGER || shape->rank != 1U))
        diagnose_argument(context, line, statement_text, "RESHAPE", "SHAPE", shape,
                          "a rank-one INTEGER array");
    if (pad != NULL && (pad->rank != 1U || !same_element_type(source, pad)))
        diagnose_argument(context, line, statement_text, "RESHAPE", "PAD", pad,
                          "a rank-one array with SOURCE element type and kind");
    if (order != NULL && (order->type != TYPE_INTEGER || order->rank != 1U))
        diagnose_argument(context, line, statement_text, "RESHAPE", "ORDER", order,
                          "a rank-one INTEGER array");
    if (shape != NULL && shape->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        unsigned char seen[F2C_MAX_RANK] = {0};
        if (shape->child_count == 0U || shape->child_count > F2C_MAX_RANK)
            f2c_diagnostic_at(
                context, line, f2c_validation_expression_start_column(statement_text, shape), 1,
                "RESHAPE SHAPE must contain between 1 and %u extents", (unsigned int)F2C_MAX_RANK);
        for (element = 0U; element < shape->child_count; ++element) {
            int64_t extent;
            if (f2c_evaluate_integer_constant(unit, shape->children[element], &extent) &&
                extent < 0)
                f2c_diagnostic_at(context, line,
                                  f2c_validation_expression_start_column(statement_text,
                                                                         shape->children[element]),
                                  1, "RESHAPE SHAPE extents must not be negative");
        }
        if (order != NULL && order->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
            if (order->child_count != shape->child_count)
                f2c_diagnostic_at(
                    context, line, f2c_validation_expression_start_column(statement_text, order), 1,
                    "RESHAPE ORDER must contain exactly one value for each SHAPE extent");
            for (element = 0U; element < order->child_count; ++element) {
                int64_t value;
                if (!f2c_evaluate_integer_constant(unit, order->children[element], &value))
                    continue;
                if (value < 1 || (uint64_t)value > shape->child_count ||
                    seen[(size_t)(value > 0 ? value - 1 : 0)] != 0U)
                    f2c_diagnostic_at(context, line,
                                      f2c_validation_expression_start_column(
                                          statement_text, order->children[element]),
                                      1,
                                      "RESHAPE ORDER must be a permutation of result dimensions");
                else
                    seen[(size_t)value - 1U] = 1U;
            }
        }
    }
}

static void validate_pack(Context *context, size_t line, const char *statement_text,
                          const F2cBoundIntrinsicArguments *arguments) {
    const F2cExpr *array = arguments->values[0];
    const F2cExpr *mask = arguments->values[1];
    const F2cExpr *vector = arguments->values[2];
    if (array != NULL && array->rank == 0U)
        diagnose_argument(context, line, statement_text, "PACK", "ARRAY", array, "an array");
    validate_mask(context, line, statement_text, "PACK", array, mask, 1);
    if (vector != NULL && (vector->rank != 1U || !same_element_type(array, vector)))
        diagnose_argument(context, line, statement_text, "PACK", "VECTOR", vector,
                          "a rank-one array with ARRAY element type and kind");
}

static void validate_unpack(Context *context, size_t line, const char *statement_text,
                            const F2cBoundIntrinsicArguments *arguments) {
    const F2cExpr *vector = arguments->values[0];
    const F2cExpr *mask = arguments->values[1];
    const F2cExpr *field = arguments->values[2];
    size_t dimension;
    if (vector != NULL && vector->rank != 1U)
        diagnose_argument(context, line, statement_text, "UNPACK", "VECTOR", vector,
                          "a rank-one array");
    validate_mask(context, line, statement_text, "UNPACK", mask, mask, 0);
    if (field != NULL &&
        (!same_element_type(vector, field) ||
         (field->rank != 0U && (mask == NULL || field->rank != mask->rank ||
                                f2c_validation_shapes_mismatch(mask, field, &dimension)))))
        diagnose_argument(context, line, statement_text, "UNPACK", "FIELD", field,
                          "a conformable scalar or array with VECTOR element type and kind");
}

static void validate_spread(Context *context, Unit *unit, size_t line, const char *statement_text,
                            const F2cBoundIntrinsicArguments *arguments) {
    const F2cExpr *source = arguments->values[0];
    const F2cExpr *dimension = arguments->values[1];
    const F2cExpr *copies = arguments->values[2];
    int64_t value;
    validate_scalar_integer(context, line, statement_text, "SPREAD", "DIM", dimension);
    validate_scalar_integer(context, line, statement_text, "SPREAD", "NCOPIES", copies);
    if (source != NULL)
        (void)constant_dimension(context, unit, line, statement_text, "SPREAD", dimension,
                                 source->rank + 1U, &value);
    if (copies != NULL && copies->type == TYPE_INTEGER && copies->rank == 0U &&
        f2c_evaluate_integer_constant(unit, copies, &value) && value < 0)
        f2c_diagnostic_at(context, line,
                          f2c_validation_expression_start_column(statement_text, copies), 1,
                          "SPREAD NCOPIES must not be negative");
}

static void validate_shift(Context *context, Unit *unit, size_t line, const char *statement_text,
                           F2cIntrinsicId intrinsic, const F2cBoundIntrinsicArguments *arguments) {
    const char *name = transform_name(intrinsic);
    const F2cExpr *array = arguments->values[0];
    const F2cExpr *shift = arguments->values[1];
    const F2cExpr *boundary = intrinsic == F2C_INTRINSIC_EOSHIFT ? arguments->values[2] : NULL;
    const F2cExpr *dimension = arguments->values[intrinsic == F2C_INTRINSIC_EOSHIFT ? 3U : 2U];
    int64_t dimension_value = 1;
    size_t omitted;
    size_t source_dimension;
    size_t value_dimension;
    if (array != NULL && array->rank == 0U)
        diagnose_argument(context, line, statement_text, name, "ARRAY", array, "an array");
    if (shift != NULL &&
        (shift->type != TYPE_INTEGER ||
         (shift->rank != 0U && (array == NULL || shift->rank + 1U != array->rank))))
        diagnose_argument(context, line, statement_text, name, "SHIFT", shift,
                          "an INTEGER scalar or a rank-(ARRAY rank - 1) array");
    validate_scalar_integer(context, line, statement_text, name, "DIM", dimension);
    if (array != NULL && dimension != NULL)
        (void)constant_dimension(context, unit, line, statement_text, name, dimension, array->rank,
                                 &dimension_value);
    if (boundary != NULL &&
        (!same_element_type(array, boundary) ||
         (boundary->rank != 0U && (array == NULL || boundary->rank + 1U != array->rank))))
        diagnose_argument(
            context, line, statement_text, name, "BOUNDARY", boundary,
            "a scalar or rank-(ARRAY rank - 1) value with ARRAY element type and kind");
    if (array == NULL || dimension_value < 1 || (uint64_t)dimension_value > array->rank)
        return;
    omitted = (size_t)dimension_value - 1U;
    for (source_dimension = 0U, value_dimension = 0U; source_dimension < array->rank;
         ++source_dimension) {
        const F2cShapeDimension *source_shape;
        const F2cShapeDimension *shift_shape;
        const F2cShapeDimension *boundary_shape;
        if (source_dimension == omitted)
            continue;
        source_shape = &array->shape.dimensions[source_dimension];
        shift_shape =
            shift != NULL && shift->rank != 0U ? &shift->shape.dimensions[value_dimension] : NULL;
        boundary_shape = boundary != NULL && boundary->rank != 0U
                             ? &boundary->shape.dimensions[value_dimension]
                             : NULL;
        if (shift_shape != NULL && source_shape->extent_known && shift_shape->extent_known &&
            source_shape->extent != shift_shape->extent)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, shift), 1,
                              "%s SHIFT is not conformable with ARRAY", name);
        if (boundary_shape != NULL && source_shape->extent_known && boundary_shape->extent_known &&
            source_shape->extent != boundary_shape->extent)
            f2c_diagnostic_at(context, line,
                              f2c_validation_expression_start_column(statement_text, boundary), 1,
                              "EOSHIFT BOUNDARY is not conformable with ARRAY");
        ++value_dimension;
    }
}

static void validate_findloc(Context *context, Unit *unit, size_t line, const char *statement_text,
                             const F2cBoundIntrinsicArguments *arguments) {
    const F2cExpr *array = arguments->values[0];
    const F2cExpr *value = arguments->values[1];
    const F2cExpr *dimension = arguments->values[2];
    const F2cExpr *mask = arguments->values[3];
    const F2cExpr *kind = arguments->values[4];
    const F2cExpr *back = arguments->values[5];
    int64_t constant;
    if (array != NULL && array->rank == 0U)
        diagnose_argument(context, line, statement_text, "FINDLOC", "ARRAY", array, "an array");
    if (value != NULL && (value->rank != 0U || !same_element_type(array, value)))
        diagnose_argument(context, line, statement_text, "FINDLOC", "VALUE", value,
                          "a scalar with ARRAY element type and kind");
    validate_scalar_integer(context, line, statement_text, "FINDLOC", "DIM", dimension);
    if (array != NULL && dimension != NULL)
        (void)constant_dimension(context, unit, line, statement_text, "FINDLOC", dimension,
                                 array->rank, &constant);
    validate_mask(context, line, statement_text, "FINDLOC", array, mask, 1);
    if (kind != NULL && (kind->type != TYPE_INTEGER || kind->rank != 0U ||
                         !f2c_evaluate_integer_constant(unit, kind, &constant) ||
                         (constant != 1 && constant != 2 && constant != 4 && constant != 8)))
        diagnose_argument(context, line, statement_text, "FINDLOC", "KIND", kind,
                          "a supported scalar INTEGER constant (1, 2, 4, or 8)");
    if (back != NULL && (back->type != TYPE_LOGICAL || back->rank != 0U))
        diagnose_argument(context, line, statement_text, "FINDLOC", "BACK", back,
                          "a scalar LOGICAL expression");
}

void f2c_validation_transform_intrinsic(Context *context, Unit *unit, size_t line,
                                        const char *statement_text, F2cExpr *expression) {
    F2cBoundIntrinsicArguments arguments;
    if (expression == NULL || (expression->intrinsic != F2C_INTRINSIC_CSHIFT &&
                               expression->intrinsic != F2C_INTRINSIC_EOSHIFT &&
                               expression->intrinsic != F2C_INTRINSIC_FINDLOC &&
                               expression->intrinsic != F2C_INTRINSIC_MATMUL &&
                               expression->intrinsic != F2C_INTRINSIC_PACK &&
                               expression->intrinsic != F2C_INTRINSIC_RESHAPE &&
                               expression->intrinsic != F2C_INTRINSIC_SPREAD &&
                               expression->intrinsic != F2C_INTRINSIC_TRANSPOSE &&
                               expression->intrinsic != F2C_INTRINSIC_UNPACK))
        return;
    arguments = bind_arguments(context, line, statement_text, expression);
    switch (expression->intrinsic) {
    case F2C_INTRINSIC_TRANSPOSE:
    case F2C_INTRINSIC_MATMUL:
        validate_matrix(context, line, statement_text, expression->intrinsic, &arguments);
        break;
    case F2C_INTRINSIC_RESHAPE:
        validate_reshape(context, unit, line, statement_text, &arguments);
        break;
    case F2C_INTRINSIC_PACK:
        validate_pack(context, line, statement_text, &arguments);
        break;
    case F2C_INTRINSIC_UNPACK:
        validate_unpack(context, line, statement_text, &arguments);
        break;
    case F2C_INTRINSIC_SPREAD:
        validate_spread(context, unit, line, statement_text, &arguments);
        break;
    case F2C_INTRINSIC_CSHIFT:
    case F2C_INTRINSIC_EOSHIFT:
        validate_shift(context, unit, line, statement_text, expression->intrinsic, &arguments);
        break;
    case F2C_INTRINSIC_FINDLOC:
        validate_findloc(context, unit, line, statement_text, &arguments);
        break;
    case F2C_INTRINSIC_NONE:
    default:
        break;
    }
}
