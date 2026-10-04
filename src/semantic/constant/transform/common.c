#include "semantic/constant/transform/private.h"

#include "semantic/intrinsic.h"

const F2cExpr *f2c_constant_argument(const F2cExpr *call, const char *name, size_t position) {
    return f2c_intrinsic_argument(call->children, call->child_count, name, position);
}

int f2c_constant_integer_argument(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                  int64_t *value, size_t depth) {
    return expression != NULL && expression->rank == 0U && expression->type == TYPE_INTEGER &&
           f2c_constant_evaluate_integer(evaluation, expression, value, depth);
}

static Type element_category(Type type) {
    if (type == TYPE_DOUBLE)
        return TYPE_REAL;
    if (type == TYPE_DOUBLE_COMPLEX)
        return TYPE_COMPLEX;
    return type;
}

int f2c_constant_same_element(const F2cConstantArray *left, const F2cConstantArray *right) {
    return element_category(left->type.type) == element_category(right->type.type) &&
           left->type.kind == right->type.kind && left->derived_type == right->derived_type &&
           (left->type.type != TYPE_CHARACTER || left->character_length == right->character_length);
}

int f2c_constant_conformable(const F2cShape *left, const F2cShape *right) {
    if (left->rank != right->rank)
        return 0;
    for (size_t dimension = 0U; dimension < left->rank; ++dimension)
        if (!left->dimensions[dimension].extent_known ||
            !right->dimensions[dimension].extent_known ||
            left->dimensions[dimension].extent != right->dimensions[dimension].extent)
            return 0;
    return 1;
}

void f2c_constant_result_model(F2cConstantArray *result, const F2cConstantArray *source) {
    result->type = source->type;
    result->derived_type = source->derived_type;
    result->character_length = source->character_length;
}

void f2c_constant_shape_dimension(F2cShape *shape, size_t dimension, uint64_t extent) {
    shape->kind = F2C_SHAPE_EXPRESSION;
    shape->dimensions[dimension] = (F2cShapeDimension){F2C_DIMENSION_EXPLICIT, 1, 1, 1, extent};
}

int f2c_constant_transform_supported(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_RESHAPE:
    case F2C_INTRINSIC_SPREAD:
    case F2C_INTRINSIC_TRANSPOSE:
    case F2C_INTRINSIC_PACK:
    case F2C_INTRINSIC_UNPACK:
    case F2C_INTRINSIC_CSHIFT:
    case F2C_INTRINSIC_EOSHIFT:
    case F2C_INTRINSIC_FINDLOC:
        return 1;
    default:
        return 0;
    }
}

int f2c_constant_evaluate_transform(F2cConstantEvaluation *evaluation, const F2cExpr *call,
                                    F2cConstantArray *result, size_t depth) {
    if (call->resolved_procedure != NULL || !f2c_constant_consume_step(evaluation, depth))
        return 0;
    switch (call->intrinsic) {
    case F2C_INTRINSIC_RESHAPE:
    case F2C_INTRINSIC_SPREAD:
    case F2C_INTRINSIC_TRANSPOSE:
        return f2c_constant_transform_reorder(evaluation, call, result, depth + 1U);
    case F2C_INTRINSIC_PACK:
    case F2C_INTRINSIC_UNPACK:
        return f2c_constant_transform_pack(evaluation, call, result, depth + 1U);
    case F2C_INTRINSIC_CSHIFT:
    case F2C_INTRINSIC_EOSHIFT:
        return f2c_constant_transform_shift(evaluation, call, result, depth + 1U);
    case F2C_INTRINSIC_FINDLOC:
        return f2c_constant_transform_findloc(evaluation, call, result, depth + 1U);
    default:
        return 0;
    }
}
