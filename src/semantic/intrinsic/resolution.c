#include "semantic/intrinsic.h"

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

static Type generated_intrinsic_type(const char *name, Type first) {
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
    return TYPE_UNKNOWN;
}

Type f2c_resolve_intrinsic_type(const char *name, const Type *arguments, size_t count) {
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(name);
    Type result = count != 0U && arguments != NULL ? arguments[0] : TYPE_UNKNOWN;
    size_t argument;
    if (signature == NULL) {
        Type generated = generated_intrinsic_type(name, result);
        if (generated != TYPE_UNKNOWN)
            return generated;
        if (strcmp(name, "F2C_TRANSFER") == 0)
            return count >= 2U && arguments != NULL ? arguments[1] : TYPE_UNKNOWN;
        if (strcmp(name, "F2C_FORTRAN_MAX") == 0 || strcmp(name, "F2C_FORTRAN_MIN") == 0) {
            for (argument = 1U; argument < count; ++argument)
                result = f2c_common_numeric_type(result, arguments[argument]);
        }
        return result;
    }
    switch (signature->type_rule) {
    case F2C_INTRINSIC_TYPE_COMMON:
        for (argument = 1U; argument < count; ++argument)
            result = f2c_common_numeric_type(result, arguments[argument]);
        return result;
    case F2C_INTRINSIC_TYPE_ABSOLUTE:
        return absolute_result(result);
    case F2C_INTRINSIC_TYPE_DOUBLE:
        return TYPE_DOUBLE;
    case F2C_INTRINSIC_TYPE_REAL:
        return result == TYPE_DOUBLE_COMPLEX ? TYPE_DOUBLE : TYPE_REAL;
    case F2C_INTRINSIC_TYPE_INTEGER:
        return TYPE_INTEGER;
    case F2C_INTRINSIC_TYPE_COMPLEX:
        return TYPE_COMPLEX;
    case F2C_INTRINSIC_TYPE_DOUBLE_COMPLEX:
        return TYPE_DOUBLE_COMPLEX;
    case F2C_INTRINSIC_TYPE_CHARACTER:
        return TYPE_CHARACTER;
    case F2C_INTRINSIC_TYPE_LOGICAL:
        return TYPE_LOGICAL;
    case F2C_INTRINSIC_TYPE_MOLD:
        return count >= 2U && arguments != NULL ? arguments[1] : TYPE_UNKNOWN;
    case F2C_INTRINSIC_TYPE_FIRST:
    default:
        return result;
    }
}

size_t f2c_resolve_intrinsic_rank(const char *name, F2cExpr *const *arguments, size_t count) {
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(name);
    size_t rank = 0U;
    size_t argument;
    if (signature == NULL)
        return 0U;
    if (signature->id == F2C_INTRINSIC_TRANSPOSE)
        return 2U;
    if (signature->id == F2C_INTRINSIC_MATMUL) {
        const F2cExpr *left = count != 0U ? argument_value(arguments[0]) : NULL;
        const F2cExpr *right = count >= 2U ? argument_value(arguments[1]) : NULL;
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
        return f2c_intrinsic_argument(arguments, count, "dim", 1U) != NULL ? 0U : 1U;
    if (signature->id == F2C_INTRINSIC_UNPACK) {
        const F2cExpr *mask = f2c_intrinsic_argument(arguments, count, "mask", 1U);
        return mask != NULL ? mask->rank : 0U;
    }
    if (signature->id == F2C_INTRINSIC_SPREAD) {
        const F2cExpr *source = f2c_intrinsic_argument(arguments, count, "source", 0U);
        return source != NULL && source->rank < F2C_MAX_RANK ? source->rank + 1U : 0U;
    }
    if (signature->id == F2C_INTRINSIC_RESHAPE) {
        const F2cExpr *shape = f2c_intrinsic_argument(arguments, count, "shape", 1U);
        if (shape != NULL && shape->kind == F2C_EXPR_ARRAY_CONSTRUCTOR)
            return shape->child_count <= F2C_MAX_RANK ? shape->child_count : 0U;
        return shape != NULL && shape->shape.rank == 1U && shape->shape.dimensions[0].extent_known
                   ? (size_t)shape->shape.dimensions[0].extent
                   : 0U;
    }
    if (signature->id == F2C_INTRINSIC_FINDLOC) {
        const F2cExpr *array = count != 0U ? argument_value(arguments[0]) : NULL;
        const int has_dimension = f2c_intrinsic_argument(arguments, count, "dim", 2U) != NULL;
        return has_dimension ? (array != NULL && array->rank != 0U ? array->rank - 1U : 0U) : 1U;
    }
    if (signature->rank_rule == F2C_INTRINSIC_RANK_REDUCTION ||
        signature->rank_rule == F2C_INTRINSIC_RANK_LOCATION) {
        const char *source_name = signature->id == F2C_INTRINSIC_ALL ||
                                          signature->id == F2C_INTRINSIC_ANY ||
                                          signature->id == F2C_INTRINSIC_COUNT
                                      ? "mask"
                                      : "array";
        const F2cExpr *array = f2c_intrinsic_argument(arguments, count, source_name, 0U);
        const F2cExpr *dimension = f2c_intrinsic_argument(arguments, count, "dim", 1U);
        if (signature->rank_rule == F2C_INTRINSIC_RANK_LOCATION && dimension == NULL)
            return 1U;
        return dimension != NULL && array != NULL && array->rank != 0U ? array->rank - 1U : 0U;
    }
    if (signature->rank_rule == F2C_INTRINSIC_RANK_SCALAR)
        return 0U;
    if (signature->rank_rule == F2C_INTRINSIC_RANK_FIRST) {
        const F2cExpr *first = count != 0U ? argument_value(arguments[0]) : NULL;
        return first != NULL ? first->rank : 0U;
    }
    if (signature->rank_rule == F2C_INTRINSIC_RANK_MOLD) {
        const F2cExpr *mold = count >= 2U ? argument_value(arguments[1]) : NULL;
        return count >= 3U ? 1U : (mold != NULL ? mold->rank : 0U);
    }
    for (argument = 0U; argument < count; ++argument) {
        const F2cExpr *value = argument_value(arguments[argument]);
        if (value != NULL && value->rank > rank)
            rank = value->rank;
    }
    return rank;
}

static const char *kind_source_name(const F2cIntrinsicSignature *signature) {
    if (signature->id == F2C_INTRINSIC_AINT || signature->id == F2C_INTRINSIC_ANINT ||
        signature->id == F2C_INTRINSIC_ABS || signature->id == F2C_INTRINSIC_DBLE ||
        signature->id == F2C_INTRINSIC_INT || signature->id == F2C_INTRINSIC_REAL)
        return "a";
    if (signature->id == F2C_INTRINSIC_LOGICAL)
        return "l";
    if (signature->id == F2C_INTRINSIC_AIMAG || signature->id == F2C_INTRINSIC_CONJG)
        return "z";
    if (signature->id == F2C_INTRINSIC_CMPLX)
        return "x";
    if (signature->id == F2C_INTRINSIC_MERGE)
        return "tsource";
    if (signature->id == F2C_INTRINSIC_DOT_PRODUCT)
        return "vector_a";
    if (signature->id == F2C_INTRINSIC_MAXVAL || signature->id == F2C_INTRINSIC_MINVAL ||
        signature->id == F2C_INTRINSIC_PRODUCT || signature->id == F2C_INTRINSIC_SUM)
        return "array";
    if (signature->id == F2C_INTRINSIC_ADJUSTL || signature->id == F2C_INTRINSIC_ADJUSTR ||
        signature->id == F2C_INTRINSIC_REPEAT || signature->id == F2C_INTRINSIC_TRIM)
        return "string";
    if (signature->id == F2C_INTRINSIC_EPSILON || signature->id == F2C_INTRINSIC_HUGE ||
        signature->id == F2C_INTRINSIC_TINY || f2c_intrinsic_is_real_representation(signature->id))
        return "x";
    if (f2c_intrinsic_is_mathematical(signature->id))
        return signature->id == F2C_INTRINSIC_MAX || signature->id == F2C_INTRINSIC_MIN ? "a1"
                                                                                        : "x";
    return "i";
}

static int common_intrinsic_kind(const char *name, F2cExpr *const *arguments, size_t count) {
    Type argument_types[2] = {TYPE_UNKNOWN, TYPE_UNKNOWN};
    Type result_type;
    const size_t considered = count < 2U ? count : 2U;
    size_t argument;
    int kind;
    for (argument = 0U; argument < considered; ++argument) {
        const F2cExpr *value = argument_value(arguments[argument]);
        argument_types[argument] = value != NULL ? value->type : TYPE_UNKNOWN;
    }
    result_type = f2c_resolve_intrinsic_type(name, argument_types, considered);
    kind = f2c_default_kind(result_type);
    for (argument = 0U; argument < considered; ++argument) {
        const F2cExpr *value = argument_value(arguments[argument]);
        const int contributes =
            value != NULL &&
            ((result_type == TYPE_INTEGER && value->type == TYPE_INTEGER) ||
             (result_type == TYPE_LOGICAL && value->type == TYPE_LOGICAL) ||
             ((result_type == TYPE_REAL || result_type == TYPE_DOUBLE) &&
              (value->type == TYPE_REAL || value->type == TYPE_DOUBLE)) ||
             ((result_type == TYPE_COMPLEX || result_type == TYPE_DOUBLE_COMPLEX) &&
              (value->type == TYPE_REAL || value->type == TYPE_DOUBLE ||
               value->type == TYPE_COMPLEX || value->type == TYPE_DOUBLE_COMPLEX)));
        if (contributes && value->type_kind > kind)
            kind = value->type_kind;
    }
    return kind;
}

int f2c_resolve_intrinsic_kind(const char *name, F2cExpr *const *arguments, size_t count) {
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(name);
    const F2cExpr *first;
    if (signature == NULL)
        return 0;
    if (signature->kind_rule == F2C_INTRINSIC_KIND_DEFAULT)
        return f2c_default_kind(f2c_resolve_intrinsic_type(name, NULL, 0U));
    if (signature->kind_rule == F2C_INTRINSIC_KIND_OPTIONAL)
        return f2c_default_kind(
            signature->type_rule == F2C_INTRINSIC_TYPE_CHARACTER ? TYPE_CHARACTER : TYPE_INTEGER);
    if (signature->kind_rule == F2C_INTRINSIC_KIND_COMMON)
        return common_intrinsic_kind(name, arguments, count);
    first = f2c_intrinsic_argument(arguments, count, kind_source_name(signature), 0U);
    return first != NULL
               ? (first->type_kind != 0 ? first->type_kind : f2c_default_kind(first->type))
               : f2c_default_kind(signature->type_rule == F2C_INTRINSIC_TYPE_CHARACTER
                                      ? TYPE_CHARACTER
                                      : TYPE_INTEGER);
}
