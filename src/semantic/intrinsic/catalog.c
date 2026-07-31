#include "internal/f2c.h"

#include <string.h>

/* Specific legacy names and nonstandard compatibility extensions live here.  Every
 * canonical standard intrinsic is defined by semantic/intrinsic/identity.c. */

#define ELEMENTAL(name, minimum, maximum, type)                                                    \
    {name,                                                                                         \
     minimum,                                                                                      \
     maximum,                                                                                      \
     type,                                                                                         \
     F2C_INTRINSIC_RANK_ELEMENTAL,                                                                 \
     F2C_INTRINSIC_NONE,                                                                           \
     F2C_INTRINSIC_KIND_DEFAULT}
#define SCALAR(name, minimum, maximum, type)                                                       \
    {name,                                                                                         \
     minimum,                                                                                      \
     maximum,                                                                                      \
     type,                                                                                         \
     F2C_INTRINSIC_RANK_SCALAR,                                                                    \
     F2C_INTRINSIC_NONE,                                                                           \
     F2C_INTRINSIC_KIND_DEFAULT}
#define TYPED_ELEMENTAL(name, minimum, maximum, type, id, kind)                                    \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_ELEMENTAL, id, kind}

static const F2cIntrinsicSignature intrinsic_aliases[] = {
    ELEMENTAL("abs1", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("abssq", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    TYPED_ELEMENTAL("alog", 1U, 1U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_LOG,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("alog10", 1U, 1U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_LOG10,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("amax0", 2U, 64U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_MAX,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("amax1", 2U, 64U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_MAX,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("amin0", 2U, 64U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_MIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("amin1", 2U, 64U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_MIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("amod", 2U, 2U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_MOD,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("cabs", 1U, 1U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_ABS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    ELEMENTAL("cabs1", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("cabs2", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    TYPED_ELEMENTAL("cdabs", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_ABS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("ccos", 1U, 1U, F2C_INTRINSIC_TYPE_COMPLEX, F2C_INTRINSIC_COS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("cexp", 1U, 1U, F2C_INTRINSIC_TYPE_COMPLEX, F2C_INTRINSIC_EXP,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("clog", 1U, 1U, F2C_INTRINSIC_TYPE_COMPLEX, F2C_INTRINSIC_LOG,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("csin", 1U, 1U, F2C_INTRINSIC_TYPE_COMPLEX, F2C_INTRINSIC_SIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("csqrt", 1U, 1U, F2C_INTRINSIC_TYPE_COMPLEX, F2C_INTRINSIC_SQRT,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dabs", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_ABS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dacos", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_ACOS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dasin", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_ASIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("datan", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_ATAN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("datan2", 2U, 2U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_ATAN2,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dcmplx", 1U, 2U, F2C_INTRINSIC_TYPE_DOUBLE_COMPLEX, F2C_INTRINSIC_CMPLX,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dconjg", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE_COMPLEX, F2C_INTRINSIC_CONJG,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dcos", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_COS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dcosh", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_COSH,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("ddim", 2U, 2U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_DIM,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("dint", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_AINT,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("dmod", 2U, 2U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_MOD,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("dnint", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_ANINT,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("dexp", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_EXP,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dimag", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_AIMAG,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dlog", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_LOG,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dlog10", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_LOG10,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dmax1", 2U, 64U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_MAX,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dmin1", 2U, 64U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_MIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dreal", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_REAL,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dsign", 2U, 2U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_SIGN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("dsin", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_SIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dsinh", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_SINH,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dsqrt", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_SQRT,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dtan", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_TAN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dtanh", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_TANH,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("float", 1U, 1U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_REAL,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("iabs", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_ABS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("idim", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_DIM,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("idnint", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_NINT,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("idint", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_INT,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("ifix", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_INT,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("isign", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_SIGN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("la_isnan", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_ISNAN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("max0", 2U, 64U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MAX,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("max1", 2U, 64U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MAX,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("min0", 2U, 64U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("min1", 2U, 64U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    SCALAR("omp_get_num_threads", 0U, 0U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("omp_get_thread_num", 0U, 0U, F2C_INTRINSIC_TYPE_INTEGER),
    TYPED_ELEMENTAL("sngl", 1U, 1U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_REAL,
                    F2C_INTRINSIC_KIND_DEFAULT),
};

#undef TYPED_ELEMENTAL
#undef SCALAR
#undef ELEMENTAL

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
    const F2cIntrinsicSpecification *specification;
    size_t i;
    if (name == NULL)
        return NULL;

    specification = f2c_find_intrinsic_specification(name);
    if (specification != NULL &&
        specification->descriptor.procedure_kind == F2C_INTRINSIC_PROCEDURE_FUNCTION)
        return &specification->signature;

    for (i = 0U; i < sizeof(intrinsic_aliases) / sizeof(intrinsic_aliases[0]); ++i) {
        if (strcmp(name, intrinsic_aliases[i].name) == 0)
            return &intrinsic_aliases[i];
    }
    return NULL;
}

static size_t canonical_function_count(void) {
    F2cIntrinsicId intrinsic;
    size_t count = 0U;
    for (intrinsic = (F2cIntrinsicId)(F2C_INTRINSIC_NONE + 1); intrinsic < F2C_INTRINSIC_ID_COUNT;
         intrinsic = (F2cIntrinsicId)(intrinsic + 1)) {
        if (f2c_intrinsic_canonical_signature(intrinsic) != NULL)
            ++count;
    }
    return count;
}

size_t f2c_intrinsic_signature_count(void) {
    return canonical_function_count() + sizeof(intrinsic_aliases) / sizeof(intrinsic_aliases[0]);
}

const F2cIntrinsicSignature *f2c_intrinsic_signature_at(size_t index) {
    F2cIntrinsicId intrinsic;
    size_t canonical_index = 0U;
    for (intrinsic = (F2cIntrinsicId)(F2C_INTRINSIC_NONE + 1); intrinsic < F2C_INTRINSIC_ID_COUNT;
         intrinsic = (F2cIntrinsicId)(intrinsic + 1)) {
        const F2cIntrinsicSignature *signature = f2c_intrinsic_canonical_signature(intrinsic);
        if (signature == NULL)
            continue;
        if (canonical_index++ == index)
            return signature;
    }

    index -= canonical_index;
    return index < sizeof(intrinsic_aliases) / sizeof(intrinsic_aliases[0])
               ? &intrinsic_aliases[index]
               : NULL;
}

int f2c_is_intrinsic_name(const char *name) { return f2c_find_intrinsic(name) != NULL; }

int f2c_is_intrinsic_subroutine(const char *name) {
    const F2cIntrinsicSpecification *specification = f2c_find_intrinsic_specification(name);
    return specification != NULL &&
           specification->descriptor.procedure_kind == F2C_INTRINSIC_PROCEDURE_SUBROUTINE;
}

int f2c_intrinsic_is_bit(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_BIT);
}

int f2c_intrinsic_is_character(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_CHARACTER);
}

int f2c_intrinsic_is_conversion(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_CONVERSION);
}

int f2c_intrinsic_is_mathematical(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_MATHEMATICAL);
}

int f2c_intrinsic_is_numeric_model(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_NUMERIC_MODEL);
}

int f2c_intrinsic_is_numeric_operation(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_NUMERIC_OPERATION);
}

int f2c_intrinsic_is_real_representation(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_REAL_REPRESENTATION);
}

int f2c_intrinsic_is_reduction(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_REDUCTION);
}

int f2c_intrinsic_is_transformational(F2cIntrinsicId intrinsic) {
    return f2c_intrinsic_has_family(intrinsic, F2C_INTRINSIC_FAMILY_TRANSFORMATIONAL);
}
