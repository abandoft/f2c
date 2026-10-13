#include "semantic/intrinsic.h"
#include "semantic/intrinsic/result.h"

#include <string.h>

static const F2cExpr *argument_value(const F2cExpr *argument) {
    if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
        argument->child_count == 1U)
        return argument->children[0];
    return argument;
}

static Type absolute_result(Type type) {
    if (type == TYPE_DOUBLE_COMPLEX)
        return TYPE_DOUBLE;
    if (type == TYPE_COMPLEX)
        return TYPE_REAL;
    return type;
}

Type f2c_generated_intrinsic_type(const char *name, Type first) {
    static const char *const single_real[] = {
        "crealf", "cimagf", "cabsf",  "fabsf", "sqrtf", "sinf",  "cosf",   "tanf",
        "expf",   "logf",   "log10f", "atanf", "asinf", "acosf", "atan2f", "powf"};
    static const char *const double_real[] = {"creal", "cimag", "cabs", "fabs", "pow"};
    static const char *const single_complex[] = {"conjf", "f2c_cdiv", "csqrtf", "cexpf",
                                                 "clogf", "csinf",    "ccosf"};
    static const char *const double_complex[] = {"conj", "f2c_zdiv", "csqrt", "cexp",
                                                 "clog", "csin",     "ccos"};
    size_t index;
    for (index = 0U; index < sizeof(single_real) / sizeof(single_real[0]); ++index)
        if (strcmp(name, single_real[index]) == 0)
            return TYPE_REAL;
    for (index = 0U; index < sizeof(double_real) / sizeof(double_real[0]); ++index)
        if (strcmp(name, double_real[index]) == 0)
            return TYPE_DOUBLE;
    for (index = 0U; index < sizeof(single_complex) / sizeof(single_complex[0]); ++index)
        if (strcmp(name, single_complex[index]) == 0)
            return TYPE_COMPLEX;
    for (index = 0U; index < sizeof(double_complex) / sizeof(double_complex[0]); ++index)
        if (strcmp(name, double_complex[index]) == 0)
            return TYPE_DOUBLE_COMPLEX;
    if (strcmp(name, "F2C_ABS") == 0)
        return absolute_result(first);
    return first;
}

size_t f2c_intrinsic_result_rank(const F2cIntrinsicSignature *signature, F2cExpr *const *arguments,
                                 size_t count) {
    size_t rank = 0U;
    size_t argument;
    if (signature == NULL)
        return 0U;
    if (signature->id == F2C_INTRINSIC_TRANSPOSE)
        return 2U;
    if (signature->id == F2C_INTRINSIC_MATMUL) {
        const F2cExpr *left = f2c_intrinsic_argument_at(signature, arguments, count, 0U);
        const F2cExpr *right = f2c_intrinsic_argument_at(signature, arguments, count, 1U);
        if (left == NULL || right == NULL)
            return 0U;
        if (left->rank == 1U && right->rank == 1U)
            return 0U;
        if (left->rank == 2U && right->rank == 2U)
            return 2U;
        return 1U;
    }
    if (signature->id == F2C_INTRINSIC_PACK || signature->id == F2C_INTRINSIC_SHAPE)
        return 1U;
    if (signature->id == F2C_INTRINSIC_LBOUND || signature->id == F2C_INTRINSIC_UBOUND)
        return f2c_intrinsic_argument_at(signature, arguments, count, 1U) != NULL ? 0U : 1U;
    if (signature->id == F2C_INTRINSIC_UNPACK) {
        const F2cExpr *mask = f2c_intrinsic_argument_at(signature, arguments, count, 1U);
        return mask != NULL ? mask->rank : 0U;
    }
    if (signature->id == F2C_INTRINSIC_SPREAD) {
        const F2cExpr *source = f2c_intrinsic_argument_at(signature, arguments, count, 0U);
        return source != NULL && source->rank < F2C_MAX_RANK ? source->rank + 1U : 0U;
    }
    if (signature->id == F2C_INTRINSIC_RESHAPE) {
        const F2cExpr *shape = f2c_intrinsic_argument_at(signature, arguments, count, 1U);
        return shape != NULL && shape->shape.rank == 1U &&
                       shape->shape.dimensions[0].extent_known &&
                       shape->shape.dimensions[0].extent <= F2C_MAX_RANK
                   ? (size_t)shape->shape.dimensions[0].extent
                   : 0U;
    }
    if (signature->id == F2C_INTRINSIC_FINDLOC) {
        const F2cExpr *array = f2c_intrinsic_argument_at(signature, arguments, count, 0U);
        const int has_dimension =
            f2c_intrinsic_argument_at(signature, arguments, count, 2U) != NULL;
        return has_dimension ? (array != NULL && array->rank != 0U ? array->rank - 1U : 0U) : 1U;
    }
    if (signature->rank_rule == F2C_INTRINSIC_RANK_REDUCTION ||
        signature->rank_rule == F2C_INTRINSIC_RANK_LOCATION) {
        const F2cExpr *array = f2c_intrinsic_argument_at(signature, arguments, count, 0U);
        const F2cExpr *dimension = f2c_intrinsic_argument_at(signature, arguments, count, 1U);
        if (signature->rank_rule == F2C_INTRINSIC_RANK_LOCATION && dimension == NULL)
            return 1U;
        return dimension != NULL && array != NULL && array->rank != 0U ? array->rank - 1U : 0U;
    }
    if (signature->rank_rule == F2C_INTRINSIC_RANK_SCALAR)
        return 0U;
    if (signature->rank_rule == F2C_INTRINSIC_RANK_FIRST) {
        const F2cExpr *first = f2c_intrinsic_argument_at(signature, arguments, count, 0U);
        return first != NULL ? first->rank : 0U;
    }
    if (signature->rank_rule == F2C_INTRINSIC_RANK_MOLD) {
        const F2cExpr *mold = f2c_intrinsic_argument_at(signature, arguments, count, 1U);
        const F2cExpr *size = f2c_intrinsic_argument_at(signature, arguments, count, 2U);
        return size != NULL || (mold != NULL && mold->rank != 0U) ? 1U : 0U;
    }
    for (argument = 0U; argument < count; ++argument) {
        const F2cExpr *value = argument_value(arguments[argument]);
        if (value != NULL && value->rank > rank)
            rank = value->rank;
    }
    return rank;
}
