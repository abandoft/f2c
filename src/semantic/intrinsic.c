#include "internal/f2c.h"

#include <string.h>

#define ELEMENTAL(name, minimum, maximum, type)                                                    \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_NONE,                \
     F2C_INTRINSIC_KIND_DEFAULT}
#define SCALAR(name, minimum, maximum, type)                                                       \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_NONE,                   \
     F2C_INTRINSIC_KIND_DEFAULT}
#define FIRST_RANK(name, minimum, maximum, type)                                                   \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_FIRST, F2C_INTRINSIC_NONE,                    \
     F2C_INTRINSIC_KIND_DEFAULT}
#define BIT_ELEMENTAL(name, minimum, maximum, type, id, kind)                                      \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_ELEMENTAL, id, kind}
#define BIT_SCALAR(name, minimum, maximum, type, id, kind)                                         \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_SCALAR, id, kind}

static const F2cIntrinsicSignature intrinsic_signatures[] = {
    ELEMENTAL("abs", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("abs1", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("abssq", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("acos", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("alog", 1U, 1U, F2C_INTRINSIC_TYPE_REAL),
    SCALAR("allocated", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL),
    SCALAR("associated", 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL),
    ELEMENTAL("aimag", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("asin", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("atan", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("atan2", 2U, 2U, F2C_INTRINSIC_TYPE_COMMON),
    BIT_SCALAR("bit_size", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_BIT_SIZE,
               F2C_INTRINSIC_KIND_FIRST),
    BIT_ELEMENTAL("btest", 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_BTEST,
                  F2C_INTRINSIC_KIND_DEFAULT),
    SCALAR("all", 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL),
    SCALAR("any", 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL),
    ELEMENTAL("cabs", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("cabs1", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("cabs2", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("cdabs", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    ELEMENTAL("ceiling", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER),
    ELEMENTAL("char", 1U, 2U, F2C_INTRINSIC_TYPE_CHARACTER),
    ELEMENTAL("cmplx", 1U, 3U, F2C_INTRINSIC_TYPE_COMPLEX),
    SCALAR("count", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER),
    FIRST_RANK("cshift", 2U, 3U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("conjg", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("cos", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("dabs", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    ELEMENTAL("dble", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    ELEMENTAL("dcmplx", 1U, 2U, F2C_INTRINSIC_TYPE_DOUBLE_COMPLEX),
    ELEMENTAL("dconjg", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("dcos", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    ELEMENTAL("dexp", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    SCALAR("digits", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER),
    ELEMENTAL("dimag", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    ELEMENTAL("dlog", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    ELEMENTAL("dreal", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    SCALAR("dot_product", 2U, 2U, F2C_INTRINSIC_TYPE_COMMON),
    ELEMENTAL("dsign", 2U, 2U, F2C_INTRINSIC_TYPE_DOUBLE),
    ELEMENTAL("dsin", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    ELEMENTAL("dsqrt", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE),
    SCALAR("epsilon", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("exp", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("exponent", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER),
    ELEMENTAL("float", 1U, 1U, F2C_INTRINSIC_TYPE_REAL),
    ELEMENTAL("floor", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("findloc", 2U, 6U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("huge", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    BIT_ELEMENTAL("iand", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IAND,
                  F2C_INTRINSIC_KIND_FIRST),
    BIT_ELEMENTAL("ibclr", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IBCLR,
                  F2C_INTRINSIC_KIND_FIRST),
    BIT_ELEMENTAL("ibits", 3U, 3U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IBITS,
                  F2C_INTRINSIC_KIND_FIRST),
    BIT_ELEMENTAL("ibset", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IBSET,
                  F2C_INTRINSIC_KIND_FIRST),
    ELEMENTAL("ichar", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER),
    ELEMENTAL("idnint", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER),
    BIT_ELEMENTAL("ieor", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IEOR,
                  F2C_INTRINSIC_KIND_FIRST),
    ELEMENTAL("int", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER),
    BIT_ELEMENTAL("ior", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IOR,
                  F2C_INTRINSIC_KIND_FIRST),
    BIT_ELEMENTAL("ishft", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_ISHFT,
                  F2C_INTRINSIC_KIND_FIRST),
    BIT_ELEMENTAL("ishftc", 2U, 3U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_ISHFTC,
                  F2C_INTRINSIC_KIND_FIRST),
    ELEMENTAL("isnan", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL),
    SCALAR("kind", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("lbound", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER),
    ELEMENTAL("la_isnan", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL),
    FIRST_RANK("len", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER),
    ELEMENTAL("len_trim", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER),
    ELEMENTAL("log", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("log10", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("matmul", 2U, 2U, F2C_INTRINSIC_TYPE_COMMON),
    ELEMENTAL("max", 2U, 64U, F2C_INTRINSIC_TYPE_COMMON),
    SCALAR("maxexponent", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("maxloc", 1U, 5U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("maxval", 1U, 3U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("min", 2U, 64U, F2C_INTRINSIC_TYPE_COMMON),
    SCALAR("minexponent", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("minloc", 1U, 5U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("minval", 1U, 3U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("mod", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("nint", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER),
    BIT_ELEMENTAL("not", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_NOT,
                  F2C_INTRINSIC_KIND_FIRST),
    SCALAR("null", 0U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("omp_get_num_threads", 0U, 0U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("omp_get_thread_num", 0U, 0U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("present", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL),
    SCALAR("product", 1U, 3U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("pack", 2U, 3U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("radix", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("random_number", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("real", 1U, 2U, F2C_INTRINSIC_TYPE_REAL),
    SCALAR("reshape", 2U, 4U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("shape", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER),
    ELEMENTAL("sign", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("sin", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("sqrt", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("spread", 3U, 3U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("size", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("sum", 1U, 3U, F2C_INTRINSIC_TYPE_FIRST),
    ELEMENTAL("tan", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("tiny", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("transpose", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST),
    {"transfer", 2U, 3U, F2C_INTRINSIC_TYPE_MOLD, F2C_INTRINSIC_RANK_MOLD, F2C_INTRINSIC_NONE,
     F2C_INTRINSIC_KIND_DEFAULT},
    SCALAR("unpack", 3U, 3U, F2C_INTRINSIC_TYPE_FIRST),
    SCALAR("ubound", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER),
    FIRST_RANK("eoshift", 2U, 4U, F2C_INTRINSIC_TYPE_FIRST),
};

#undef ELEMENTAL
#undef SCALAR
#undef FIRST_RANK
#undef BIT_ELEMENTAL
#undef BIT_SCALAR

static const F2cExpr *argument_value(const F2cExpr *argument) {
    if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
        argument->child_count == 1U)
        return argument->children[0];
    return argument;
}

const F2cExpr *f2c_intrinsic_argument(F2cExpr *const *arguments, size_t count, const char *keyword,
                                      size_t position) {
    size_t positional = 0U;
    size_t i;
    for (i = 0U; i < count; ++i) {
        const F2cExpr *argument = arguments[i];
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (argument->text != NULL && strcmp(argument->text, keyword) == 0)
                return argument_value(argument);
        } else if (positional++ == position) {
            return argument;
        }
    }
    return NULL;
}

const F2cIntrinsicSignature *f2c_find_intrinsic(const char *name) {
    size_t i;
    if (name == NULL)
        return NULL;
    for (i = 0U; i < sizeof(intrinsic_signatures) / sizeof(intrinsic_signatures[0]); ++i) {
        if (strcmp(name, intrinsic_signatures[i].name) == 0)
            return &intrinsic_signatures[i];
    }
    return NULL;
}

int f2c_is_intrinsic_name(const char *name) { return f2c_find_intrinsic(name) != NULL; }

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
    size_t i;
    for (i = 0U; i < sizeof(single_real) / sizeof(single_real[0]); ++i)
        if (strcmp(name, single_real[i]) == 0)
            return TYPE_REAL;
    for (i = 0U; i < sizeof(double_real) / sizeof(double_real[0]); ++i)
        if (strcmp(name, double_real[i]) == 0)
            return TYPE_DOUBLE;
    for (i = 0U; i < sizeof(single_complex) / sizeof(single_complex[0]); ++i)
        if (strcmp(name, single_complex[i]) == 0)
            return TYPE_COMPLEX;
    for (i = 0U; i < sizeof(double_complex) / sizeof(double_complex[0]); ++i)
        if (strcmp(name, double_complex[i]) == 0)
            return TYPE_DOUBLE_COMPLEX;
    if (strcmp(name, "F2C_ABS") == 0)
        return absolute_result(first);
    return TYPE_UNKNOWN;
}

Type f2c_resolve_intrinsic_type(const char *name, const Type *arguments, size_t count) {
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(name);
    Type result = count != 0U && arguments != NULL ? arguments[0] : TYPE_UNKNOWN;
    size_t i;
    if (signature == NULL) {
        Type generated = generated_intrinsic_type(name, result);
        if (generated != TYPE_UNKNOWN)
            return generated;
        if (strcmp(name, "F2C_TRANSFER") == 0)
            return count >= 2U && arguments != NULL ? arguments[1] : TYPE_UNKNOWN;
        if (strcmp(name, "F2C_FORTRAN_MAX") == 0 || strcmp(name, "F2C_FORTRAN_MIN") == 0) {
            for (i = 1U; i < count; ++i)
                result = f2c_common_numeric_type(result, arguments[i]);
        }
        return result;
    }
    switch (signature->type_rule) {
    case F2C_INTRINSIC_TYPE_COMMON:
        for (i = 1U; i < count; ++i)
            result = f2c_common_numeric_type(result, arguments[i]);
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
    size_t i;
    if (signature == NULL)
        return 0U;
    if (strcmp(name, "transpose") == 0)
        return 2U;
    if (strcmp(name, "matmul") == 0) {
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
    if (strcmp(name, "pack") == 0)
        return 1U;
    if (strcmp(name, "shape") == 0)
        return 1U;
    if (strcmp(name, "lbound") == 0 || strcmp(name, "ubound") == 0)
        return f2c_intrinsic_argument(arguments, count, "dim", 1U) != NULL ? 0U : 1U;
    if (strcmp(name, "unpack") == 0) {
        const F2cExpr *mask = f2c_intrinsic_argument(arguments, count, "mask", 1U);
        return mask != NULL ? mask->rank : 0U;
    }
    if (strcmp(name, "spread") == 0) {
        const F2cExpr *source = f2c_intrinsic_argument(arguments, count, "source", 0U);
        return source != NULL && source->rank < F2C_MAX_RANK ? source->rank + 1U : 0U;
    }
    if (strcmp(name, "reshape") == 0) {
        const F2cExpr *shape = f2c_intrinsic_argument(arguments, count, "shape", 1U);
        if (shape != NULL && shape->kind == F2C_EXPR_ARRAY_CONSTRUCTOR)
            return shape->child_count <= F2C_MAX_RANK ? shape->child_count : 0U;
        return shape != NULL && shape->shape.rank == 1U && shape->shape.dimensions[0].extent_known
                   ? (size_t)shape->shape.dimensions[0].extent
                   : 0U;
    }
    if (strcmp(name, "findloc") == 0 || strcmp(name, "maxloc") == 0 ||
        strcmp(name, "minloc") == 0) {
        const F2cExpr *array = count != 0U ? argument_value(arguments[0]) : NULL;
        const size_t dim_position = strcmp(name, "findloc") == 0 ? 2U : 1U;
        const int has_dimension =
            f2c_intrinsic_argument(arguments, count, "dim", dim_position) != NULL;
        return has_dimension ? (array != NULL && array->rank != 0U ? array->rank - 1U : 0U) : 1U;
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
    for (i = 0U; i < count; ++i) {
        const F2cExpr *argument = argument_value(arguments[i]);
        if (argument != NULL && argument->rank > rank)
            rank = argument->rank;
    }
    return rank;
}

int f2c_resolve_intrinsic_kind(const char *name, F2cExpr *const *arguments, size_t count) {
    const F2cIntrinsicSignature *signature = f2c_find_intrinsic(name);
    const F2cExpr *first;
    if (signature == NULL)
        return 0;
    if (signature->kind_rule == F2C_INTRINSIC_KIND_DEFAULT)
        return f2c_default_kind(f2c_resolve_intrinsic_type(name, NULL, 0U));
    first = f2c_intrinsic_argument(arguments, count, "i", 0U);
    return first != NULL && first->type_kind != 0 ? first->type_kind
                                                  : f2c_default_kind(TYPE_INTEGER);
}
