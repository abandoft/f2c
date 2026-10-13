#include "semantic/intrinsic/result.h"

#include "internal/f2c.h"
#include "semantic/constant/array.h"

#include <stdlib.h>
#include <string.h>

static void set_shape(F2cExpr *expression, size_t rank, F2cShapeKind kind) {
    memset(&expression->shape, 0, sizeof(expression->shape));
    expression->rank = rank;
    expression->shape.rank = rank;
    expression->shape.kind = rank == 0U ? F2C_SHAPE_SCALAR : kind;
}

static void copy_shape(F2cExpr *expression, const F2cShape *shape) {
    expression->shape = *shape;
    expression->rank = shape->rank;
    expression->shape.kind = shape->rank == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_EXPRESSION;
}

static int multiply_extent(uint64_t left, uint64_t right, uint64_t *result) {
    if (right != 0U && left > UINT64_MAX / right)
        return 0;
    *result = left * right;
    return 1;
}

static int transfer_element_bytes(Unit *unit, const F2cExpr *expression, uint64_t *bytes) {
    int kind;
    int64_t length;
    if (expression == NULL || bytes == NULL)
        return 0;
    kind = expression->type_kind != 0 ? expression->type_kind : f2c_default_kind(expression->type);
    switch (expression->type) {
    case TYPE_INTEGER:
    case TYPE_LOGICAL:
    case TYPE_REAL:
    case TYPE_DOUBLE:
        if (kind != 1 && kind != 2 && kind != 4 && kind != 8 && kind != 16)
            return 0;
        *bytes = (uint64_t)kind;
        return 1;
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX:
        if (kind != 4 && kind != 8 && kind != 16)
            return 0;
        *bytes = (uint64_t)kind * UINT64_C(2);
        return 1;
    case TYPE_CHARACTER:
        if (kind != 1)
            return 0;
        if (expression->symbol != NULL &&
            f2c_character_declaration_length(unit, expression->symbol, &length) && length >= 0) {
            *bytes = (uint64_t)length;
            return 1;
        }
        char *characters = NULL;
        size_t character_count = 0U;
        const int known =
            f2c_evaluate_character_constant(unit, expression, &characters, &character_count);
        free(characters);
        if (!known)
            return 0;
        *bytes = (uint64_t)character_count;
        return 1;
    case TYPE_DERIVED:
    case TYPE_UNKNOWN:
    default:
        return 0;
    }
}

static int transfer_storage_bytes(Unit *unit, const F2cExpr *expression, uint64_t *bytes) {
    uint64_t elements = UINT64_C(1);
    uint64_t element_bytes;
    size_t dimension;
    if (!transfer_element_bytes(unit, expression, &element_bytes))
        return 0;
    for (dimension = 0U; dimension < expression->rank; ++dimension) {
        const F2cShapeDimension *shape = &expression->shape.dimensions[dimension];
        if (!shape->extent_known || !multiply_extent(elements, shape->extent, &elements))
            return 0;
    }
    return multiply_extent(elements, element_bytes, bytes);
}

static const F2cExpr *argument_at(const F2cIntrinsicSignature *signature, const F2cExpr *expression,
                                  size_t slot) {
    return f2c_intrinsic_argument_at(signature, expression->children, expression->child_count,
                                     slot);
}

static void infer_transform_shape(Unit *unit, F2cExpr *expression,
                                  const F2cIntrinsicSignature *signature) {
    const F2cExpr *source;
    const F2cExpr *shape;
    const F2cExpr *dimension;
    int64_t dimension_value;
    size_t result_dimension;
    size_t source_dimension;
    if (f2c_intrinsic_is_reduction(expression->intrinsic)) {
        const int location = expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
                             expression->intrinsic == F2C_INTRINSIC_MINLOC;
        source = argument_at(signature, expression, 0U);
        dimension = expression->intrinsic == F2C_INTRINSIC_DOT_PRODUCT
                        ? NULL
                        : argument_at(signature, expression, 1U);
        set_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        if (source == NULL || expression->intrinsic == F2C_INTRINSIC_DOT_PRODUCT)
            return;
        if (dimension == NULL) {
            if (location && expression->rank == 1U) {
                expression->shape.dimensions[0].extent_known = 1;
                expression->shape.dimensions[0].extent = source->rank;
            }
            return;
        }
        if (!f2c_evaluate_integer_constant(unit, dimension, &dimension_value) ||
            dimension_value < 1 || (uint64_t)dimension_value > source->rank)
            return;
        result_dimension = 0U;
        for (source_dimension = 0U; source_dimension < source->rank; ++source_dimension) {
            if (source_dimension + 1U != (size_t)dimension_value)
                expression->shape.dimensions[result_dimension++] =
                    source->shape.dimensions[source_dimension];
        }
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_SHAPE ||
        expression->intrinsic == F2C_INTRINSIC_LBOUND ||
        expression->intrinsic == F2C_INTRINSIC_UBOUND) {
        const int shape_inquiry = expression->intrinsic == F2C_INTRINSIC_SHAPE;
        source = argument_at(signature, expression, 0U);
        dimension = shape_inquiry ? NULL : argument_at(signature, expression, 1U);
        if (dimension != NULL) {
            set_shape(expression, 0U, F2C_SHAPE_SCALAR);
        } else {
            set_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
            if (source != NULL) {
                expression->shape.dimensions[0].extent_known = 1;
                expression->shape.dimensions[0].extent = source->rank;
            }
        }
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_TRANSFER) {
        const F2cExpr *mold = argument_at(signature, expression, 1U);
        const F2cExpr *size = argument_at(signature, expression, 2U);
        source = argument_at(signature, expression, 0U);
        set_shape(expression, expression->rank,
                  expression->rank == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_EXPRESSION);
        if (expression->rank == 1U) {
            F2cShapeDimension *result = &expression->shape.dimensions[0];
            result->lower_known = 1;
            result->lower = 1;
            if (size != NULL && f2c_evaluate_integer_constant(unit, size, &dimension_value) &&
                dimension_value >= 0) {
                result->extent_known = 1;
                result->extent = (uint64_t)dimension_value;
            } else if (size == NULL && mold != NULL && mold->rank != 0U && source != NULL) {
                uint64_t source_bytes;
                uint64_t mold_bytes;
                if (transfer_storage_bytes(unit, source, &source_bytes) &&
                    transfer_element_bytes(unit, mold, &mold_bytes) && mold_bytes != 0U) {
                    result->extent_known = 1;
                    result->extent = source_bytes / mold_bytes +
                                     (source_bytes % mold_bytes != 0U ? UINT64_C(1) : UINT64_C(0));
                }
            }
        }
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_PACK) {
        const F2cExpr *vector = argument_at(signature, expression, 2U);
        set_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
        if (vector != NULL && vector->rank == 1U)
            expression->shape.dimensions[0] = vector->shape.dimensions[0];
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_UNPACK) {
        const F2cExpr *mask = argument_at(signature, expression, 1U);
        if (mask != NULL)
            copy_shape(expression, &mask->shape);
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_RESHAPE) {
        shape = argument_at(signature, expression, 1U);
        set_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        F2cConstantArray values = {0};
        if (shape != NULL && expression->rank != 0U &&
            f2c_evaluate_constant_array(unit, shape, &values) && values.type.type == TYPE_INTEGER &&
            values.count == expression->rank) {
            for (result_dimension = 0U; result_dimension < expression->rank; ++result_dimension) {
                const int64_t extent = values.values[result_dimension].payload.integer;
                if (extent >= 0) {
                    expression->shape.dimensions[result_dimension].extent_known = 1;
                    expression->shape.dimensions[result_dimension].extent = (uint64_t)extent;
                }
            }
        }
        f2c_constant_array_free(&values);
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_SPREAD) {
        source = argument_at(signature, expression, 0U);
        dimension = argument_at(signature, expression, 1U);
        set_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        if (source == NULL || dimension == NULL ||
            !f2c_evaluate_integer_constant(unit, dimension, &dimension_value) ||
            dimension_value < 1 || (uint64_t)dimension_value > expression->rank)
            return;
        source_dimension = 0U;
        for (result_dimension = 0U; result_dimension < expression->rank; ++result_dimension) {
            if (result_dimension + 1U == (size_t)dimension_value) {
                const F2cExpr *copies = argument_at(signature, expression, 2U);
                int64_t copies_value;
                F2cShapeDimension *target = &expression->shape.dimensions[result_dimension];
                target->lower_known = 1;
                target->lower = 1;
                if (copies != NULL && f2c_evaluate_integer_constant(unit, copies, &copies_value)) {
                    target->extent_known = 1;
                    target->extent = copies_value > 0 ? (uint64_t)copies_value : 0U;
                }
                continue;
            }
            if (source_dimension < source->rank)
                expression->shape.dimensions[result_dimension] =
                    source->shape.dimensions[source_dimension++];
        }
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_FINDLOC) {
        source = argument_at(signature, expression, 0U);
        dimension = argument_at(signature, expression, 2U);
        set_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        if (source == NULL)
            return;
        if (dimension == NULL) {
            if (expression->rank == 1U) {
                expression->shape.dimensions[0].extent_known = 1;
                expression->shape.dimensions[0].extent = source->rank;
            }
            return;
        }
        if (!f2c_evaluate_integer_constant(unit, dimension, &dimension_value) ||
            dimension_value < 1 || (uint64_t)dimension_value > source->rank)
            return;
        result_dimension = 0U;
        for (source_dimension = 0U; source_dimension < source->rank; ++source_dimension) {
            if (source_dimension + 1U != (size_t)dimension_value)
                expression->shape.dimensions[result_dimension++] =
                    source->shape.dimensions[source_dimension];
        }
    }
}

void f2c_intrinsic_refresh_shape(Unit *unit, F2cExpr *expression,
                                 const F2cIntrinsicSignature *signature) {
    const size_t rank = expression->rank;
    set_shape(expression, rank, F2C_SHAPE_EXPRESSION);
    for (size_t index = 0U; index < expression->child_count && rank != 0U; ++index) {
        const F2cExpr *argument = expression->children[index];
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
            argument->child_count == 1U)
            argument = argument->children[0];
        if (argument != NULL && argument->rank == rank && argument->shape.rank == rank) {
            copy_shape(expression, &argument->shape);
            break;
        }
    }
    if (signature->id == F2C_INTRINSIC_TRANSPOSE) {
        const F2cExpr *matrix =
            f2c_intrinsic_argument_at(signature, expression->children, expression->child_count, 0U);
        if (matrix != NULL && matrix->shape.rank == 2U) {
            expression->shape.dimensions[0] = matrix->shape.dimensions[1];
            expression->shape.dimensions[1] = matrix->shape.dimensions[0];
        }
    } else if (signature->id == F2C_INTRINSIC_MATMUL) {
        const F2cExpr *left =
            f2c_intrinsic_argument_at(signature, expression->children, expression->child_count, 0U);
        const F2cExpr *right =
            f2c_intrinsic_argument_at(signature, expression->children, expression->child_count, 1U);
        set_shape(expression, rank, F2C_SHAPE_EXPRESSION);
        if (left != NULL && right != NULL) {
            if (rank == 2U) {
                expression->shape.dimensions[0] = left->shape.dimensions[0];
                expression->shape.dimensions[1] = right->shape.dimensions[1];
            } else if (rank == 1U) {
                expression->shape.dimensions[0] =
                    left->rank == 2U ? left->shape.dimensions[0] : right->shape.dimensions[1];
            }
        }
    }
    if (f2c_intrinsic_is_transformational(signature->id) ||
        f2c_intrinsic_is_reduction(signature->id) || signature->id == F2C_INTRINSIC_SHAPE ||
        signature->id == F2C_INTRINSIC_LBOUND || signature->id == F2C_INTRINSIC_UBOUND)
        infer_transform_shape(unit, expression, signature);
    for (size_t dimension = 0U; dimension < expression->shape.rank; ++dimension) {
        expression->shape.dimensions[dimension].kind = F2C_DIMENSION_EXPLICIT;
        expression->shape.dimensions[dimension].lower_known = 1;
        expression->shape.dimensions[dimension].lower = 1;
    }
}
