#include "semantic/intrinsic.h"

#include <string.h>

#define FUNCTION(id, name, standard, families)                                                     \
    [F2C_INTRINSIC_##id] = {F2C_INTRINSIC_##id, name, standard,                                   \
                            F2C_INTRINSIC_PROCEDURE_FUNCTION, families}
#define SUBROUTINE(id, name, standard, families)                                                   \
    [F2C_INTRINSIC_##id] = {F2C_INTRINSIC_##id, name, standard,                                   \
                            F2C_INTRINSIC_PROCEDURE_SUBROUTINE, families}

#define F77 F2C_INTRINSIC_STANDARD_FORTRAN_77
#define F90 F2C_INTRINSIC_STANDARD_FORTRAN_90
#define F95 F2C_INTRINSIC_STANDARD_FORTRAN_95
#define F2008 F2C_INTRINSIC_STANDARD_FORTRAN_2008
#define EXTENSION F2C_INTRINSIC_STANDARD_EXTENSION

#define BIT F2C_INTRINSIC_FAMILY_BIT
#define CHARACTER F2C_INTRINSIC_FAMILY_CHARACTER
#define CONVERSION F2C_INTRINSIC_FAMILY_CONVERSION
#define MATHEMATICAL F2C_INTRINSIC_FAMILY_MATHEMATICAL
#define MODEL F2C_INTRINSIC_FAMILY_NUMERIC_MODEL
#define NUMERIC F2C_INTRINSIC_FAMILY_NUMERIC_OPERATION
#define REPRESENTATION F2C_INTRINSIC_FAMILY_REAL_REPRESENTATION
#define REDUCTION F2C_INTRINSIC_FAMILY_REDUCTION
#define TRANSFORM F2C_INTRINSIC_FAMILY_TRANSFORMATIONAL
#define ARRAY_INQUIRY F2C_INTRINSIC_FAMILY_ARRAY_INQUIRY
#define ASSUMED_SIZE F2C_INTRINSIC_FAMILY_ASSUMED_SIZE_INQUIRY
#define NONE F2C_INTRINSIC_FAMILY_NONE

static const F2cIntrinsicDescriptor descriptors[F2C_INTRINSIC_ID_COUNT] = {
    FUNCTION(ABS, "abs", F77, MATHEMATICAL),
    FUNCTION(ACOS, "acos", F77, MATHEMATICAL),
    FUNCTION(ACHAR, "achar", F90, CHARACTER),
    FUNCTION(ADJUSTL, "adjustl", F90, CHARACTER),
    FUNCTION(ADJUSTR, "adjustr", F90, CHARACTER),
    FUNCTION(AINT, "aint", F77, NUMERIC),
    FUNCTION(AIMAG, "aimag", F77, CONVERSION),
    FUNCTION(ALL, "all", F90, REDUCTION | TRANSFORM),
    FUNCTION(ALLOCATED, "allocated", F90, NONE),
    FUNCTION(ANINT, "anint", F77, NUMERIC),
    FUNCTION(ANY, "any", F90, REDUCTION | TRANSFORM),
    FUNCTION(ASSOCIATED, "associated", F90, NONE),
    FUNCTION(ASIN, "asin", F77, MATHEMATICAL),
    FUNCTION(ATAN, "atan", F77, MATHEMATICAL),
    FUNCTION(ATAN2, "atan2", F77, MATHEMATICAL),
    FUNCTION(BIT_SIZE, "bit_size", F90, BIT),
    FUNCTION(BTEST, "btest", F90, BIT),
    FUNCTION(CEILING, "ceiling", F90, NUMERIC),
    FUNCTION(CHAR, "char", F77, CHARACTER),
    FUNCTION(CMPLX, "cmplx", F77, CONVERSION),
    FUNCTION(CONJG, "conjg", F77, CONVERSION),
    FUNCTION(COS, "cos", F77, MATHEMATICAL),
    FUNCTION(COSH, "cosh", F77, MATHEMATICAL),
    FUNCTION(COUNT, "count", F90, REDUCTION | TRANSFORM),
    SUBROUTINE(CPU_TIME, "cpu_time", F95, NONE),
    FUNCTION(CSHIFT, "cshift", F90, TRANSFORM),
    SUBROUTINE(DATE_AND_TIME, "date_and_time", F90, NONE),
    FUNCTION(DBLE, "dble", F77, CONVERSION),
    FUNCTION(DIGITS, "digits", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(DIM, "dim", F77, NUMERIC),
    FUNCTION(DOT_PRODUCT, "dot_product", F90, REDUCTION | TRANSFORM),
    FUNCTION(DPROD, "dprod", F77, MATHEMATICAL),
    FUNCTION(EPSILON, "epsilon", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(EOSHIFT, "eoshift", F90, TRANSFORM),
    FUNCTION(EXP, "exp", F77, MATHEMATICAL),
    FUNCTION(EXPONENT, "exponent", F90, REPRESENTATION),
    FUNCTION(FLOOR, "floor", F90, NUMERIC),
    FUNCTION(FINDLOC, "findloc", F2008, TRANSFORM),
    FUNCTION(FRACTION, "fraction", F90, REPRESENTATION),
    FUNCTION(HUGE, "huge", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(IACHAR, "iachar", F90, CHARACTER),
    FUNCTION(IAND, "iand", F90, BIT),
    FUNCTION(IBCLR, "ibclr", F90, BIT),
    FUNCTION(IBITS, "ibits", F90, BIT),
    FUNCTION(IBSET, "ibset", F90, BIT),
    FUNCTION(ICHAR, "ichar", F77, CHARACTER),
    FUNCTION(IEOR, "ieor", F90, BIT),
    FUNCTION(INDEX, "index", F77, CHARACTER),
    FUNCTION(IOR, "ior", F90, BIT),
    FUNCTION(ISHFT, "ishft", F90, BIT),
    FUNCTION(ISHFTC, "ishftc", F90, BIT),
    FUNCTION(ISNAN, "isnan", EXTENSION, NONE),
    FUNCTION(INT, "int", F77, CONVERSION),
    FUNCTION(KIND, "kind", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(LBOUND, "lbound", F90, ARRAY_INQUIRY | ASSUMED_SIZE),
    FUNCTION(LEN, "len", F77, CHARACTER | ASSUMED_SIZE),
    FUNCTION(LEN_TRIM, "len_trim", F90, CHARACTER),
    FUNCTION(LGE, "lge", F77, CHARACTER),
    FUNCTION(LGT, "lgt", F77, CHARACTER),
    FUNCTION(LLE, "lle", F77, CHARACTER),
    FUNCTION(LLT, "llt", F77, CHARACTER),
    FUNCTION(LOG, "log", F77, MATHEMATICAL),
    FUNCTION(LOG10, "log10", F77, MATHEMATICAL),
    FUNCTION(LOGICAL, "logical", F90, CONVERSION),
    FUNCTION(MATMUL, "matmul", F90, TRANSFORM),
    FUNCTION(MAXLOC, "maxloc", F90, REDUCTION | TRANSFORM),
    FUNCTION(MAXVAL, "maxval", F90, REDUCTION | TRANSFORM),
    FUNCTION(MAXEXPONENT, "maxexponent", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(MAX, "max", F77, MATHEMATICAL),
    FUNCTION(MERGE, "merge", F90, NUMERIC),
    FUNCTION(MINLOC, "minloc", F90, REDUCTION | TRANSFORM),
    FUNCTION(MINVAL, "minval", F90, REDUCTION | TRANSFORM),
    FUNCTION(MIN, "min", F77, MATHEMATICAL),
    FUNCTION(MINEXPONENT, "minexponent", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(MOD, "mod", F77, NUMERIC),
    FUNCTION(MODULO, "modulo", F90, NUMERIC),
    FUNCTION(NEAREST, "nearest", F90, REPRESENTATION),
    FUNCTION(NINT, "nint", F77, NUMERIC),
    FUNCTION(NOT, "not", F90, BIT),
    FUNCTION(NULL, "null", F95, NONE),
    SUBROUTINE(MVBITS, "mvbits", F90, BIT),
    FUNCTION(PRECISION, "precision", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(PACK, "pack", F90, TRANSFORM),
    FUNCTION(PRESENT, "present", F90, ASSUMED_SIZE),
    FUNCTION(PRODUCT, "product", F90, REDUCTION | TRANSFORM),
    SUBROUTINE(RANDOM_NUMBER, "random_number", F90, NONE),
    SUBROUTINE(RANDOM_SEED, "random_seed", F90, NONE),
    FUNCTION(RADIX, "radix", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(RANGE, "range", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(REAL, "real", F77, CONVERSION),
    FUNCTION(REPEAT, "repeat", F90, CHARACTER),
    FUNCTION(RESHAPE, "reshape", F90, TRANSFORM),
    FUNCTION(RRSPACING, "rrspacing", F90, REPRESENTATION),
    FUNCTION(SCALE, "scale", F90, REPRESENTATION),
    FUNCTION(SCAN, "scan", F90, CHARACTER),
    FUNCTION(SELECTED_INT_KIND, "selected_int_kind", F90, MODEL),
    FUNCTION(SELECTED_REAL_KIND, "selected_real_kind", F90, MODEL),
    FUNCTION(SET_EXPONENT, "set_exponent", F90, REPRESENTATION),
    FUNCTION(SIGN, "sign", F77, NUMERIC),
    FUNCTION(SHAPE, "shape", F90, ARRAY_INQUIRY),
    FUNCTION(SIN, "sin", F77, MATHEMATICAL),
    FUNCTION(SINH, "sinh", F77, MATHEMATICAL),
    FUNCTION(SPACING, "spacing", F90, REPRESENTATION),
    FUNCTION(SPREAD, "spread", F90, TRANSFORM),
    FUNCTION(SQRT, "sqrt", F77, MATHEMATICAL),
    FUNCTION(SIZE, "size", F90, ARRAY_INQUIRY | ASSUMED_SIZE),
    FUNCTION(SUM, "sum", F90, REDUCTION | TRANSFORM),
    SUBROUTINE(SYSTEM_CLOCK, "system_clock", F90, NONE),
    FUNCTION(TAN, "tan", F77, MATHEMATICAL),
    FUNCTION(TANH, "tanh", F77, MATHEMATICAL),
    FUNCTION(TINY, "tiny", F90, MODEL | ASSUMED_SIZE),
    FUNCTION(TRANSFER, "transfer", F90, NONE),
    FUNCTION(TRIM, "trim", F90, CHARACTER),
    FUNCTION(TRANSPOSE, "transpose", F90, TRANSFORM),
    FUNCTION(UBOUND, "ubound", F90, ARRAY_INQUIRY | ASSUMED_SIZE),
    FUNCTION(UNPACK, "unpack", F90, TRANSFORM),
    FUNCTION(VERIFY, "verify", F90, CHARACTER),
};

#define ARGS1(id, required, a0)                                                                   \
    [F2C_INTRINSIC_##id] = {{a0}, 1U, required, 0U}
#define ARGS2(id, required, a0, a1)                                                               \
    [F2C_INTRINSIC_##id] = {{a0, a1}, 2U, required, 0U}
#define ARGS3(id, required, a0, a1, a2)                                                           \
    [F2C_INTRINSIC_##id] = {{a0, a1, a2}, 3U, required, 0U}
#define ARGS4(id, required, a0, a1, a2, a3)                                                       \
    [F2C_INTRINSIC_##id] = {{a0, a1, a2, a3}, 4U, required, 0U}
#define ARGS5(id, required, a0, a1, a2, a3, a4)                                                   \
    [F2C_INTRINSIC_##id] = {{a0, a1, a2, a3, a4}, 5U, required, 0U}
#define ARGS6(id, required, a0, a1, a2, a3, a4, a5)                                               \
    [F2C_INTRINSIC_##id] = {{a0, a1, a2, a3, a4, a5}, 6U, required, 0U}
#define VARARGS2(id, a0, a1) [F2C_INTRINSIC_##id] = {{a0, a1}, 2U, 3U, 1U}

static const F2cIntrinsicArgumentSchema argument_schemas[F2C_INTRINSIC_ID_COUNT] = {
    ARGS1(ABS, 1U, "a"),
    ARGS1(ACOS, 1U, "x"),
    ARGS2(ACHAR, 1U, "i", "kind"),
    ARGS1(ADJUSTL, 1U, "string"),
    ARGS1(ADJUSTR, 1U, "string"),
    ARGS2(AINT, 1U, "a", "kind"),
    ARGS1(AIMAG, 1U, "z"),
    ARGS2(ALL, 1U, "mask", "dim"),
    ARGS1(ALLOCATED, 1U, "array"),
    ARGS2(ANINT, 1U, "a", "kind"),
    ARGS2(ANY, 1U, "mask", "dim"),
    ARGS2(ASSOCIATED, 1U, "pointer", "target"),
    ARGS1(ASIN, 1U, "x"),
    ARGS1(ATAN, 1U, "x"),
    ARGS2(ATAN2, 3U, "y", "x"),
    ARGS1(BIT_SIZE, 1U, "i"),
    ARGS2(BTEST, 3U, "i", "pos"),
    ARGS2(CEILING, 1U, "a", "kind"),
    ARGS2(CHAR, 1U, "i", "kind"),
    ARGS3(CMPLX, 1U, "x", "y", "kind"),
    ARGS1(CONJG, 1U, "z"),
    ARGS1(COS, 1U, "x"),
    ARGS1(COSH, 1U, "x"),
    ARGS3(COUNT, 1U, "mask", "dim", "kind"),
    ARGS1(CPU_TIME, 1U, "time"),
    ARGS3(CSHIFT, 3U, "array", "shift", "dim"),
    ARGS4(DATE_AND_TIME, 0U, "date", "time", "zone", "values"),
    ARGS1(DBLE, 1U, "a"),
    ARGS1(DIGITS, 1U, "x"),
    ARGS2(DIM, 3U, "x", "y"),
    ARGS2(DOT_PRODUCT, 3U, "vector_a", "vector_b"),
    ARGS2(DPROD, 3U, "x", "y"),
    ARGS1(EPSILON, 1U, "x"),
    ARGS4(EOSHIFT, 3U, "array", "shift", "boundary", "dim"),
    ARGS1(EXP, 1U, "x"),
    ARGS1(EXPONENT, 1U, "x"),
    ARGS2(FLOOR, 1U, "a", "kind"),
    ARGS6(FINDLOC, 3U, "array", "value", "dim", "mask", "kind", "back"),
    ARGS1(FRACTION, 1U, "x"),
    ARGS1(HUGE, 1U, "x"),
    ARGS2(IACHAR, 1U, "c", "kind"),
    ARGS2(IAND, 3U, "i", "j"),
    ARGS2(IBCLR, 3U, "i", "pos"),
    ARGS3(IBITS, 7U, "i", "pos", "len"),
    ARGS2(IBSET, 3U, "i", "pos"),
    ARGS2(ICHAR, 1U, "c", "kind"),
    ARGS2(IEOR, 3U, "i", "j"),
    ARGS4(INDEX, 3U, "string", "substring", "back", "kind"),
    ARGS2(IOR, 3U, "i", "j"),
    ARGS2(ISHFT, 3U, "i", "shift"),
    ARGS3(ISHFTC, 3U, "i", "shift", "size"),
    ARGS1(ISNAN, 1U, "x"),
    ARGS2(INT, 1U, "a", "kind"),
    ARGS1(KIND, 1U, "x"),
    ARGS3(LBOUND, 1U, "array", "dim", "kind"),
    ARGS2(LEN, 1U, "string", "kind"),
    ARGS2(LEN_TRIM, 1U, "string", "kind"),
    ARGS2(LGE, 3U, "string_a", "string_b"),
    ARGS2(LGT, 3U, "string_a", "string_b"),
    ARGS2(LLE, 3U, "string_a", "string_b"),
    ARGS2(LLT, 3U, "string_a", "string_b"),
    ARGS1(LOG, 1U, "x"),
    ARGS1(LOG10, 1U, "x"),
    ARGS2(LOGICAL, 1U, "l", "kind"),
    ARGS2(MATMUL, 3U, "matrix_a", "matrix_b"),
    ARGS5(MAXLOC, 1U, "array", "dim", "mask", "kind", "back"),
    ARGS3(MAXVAL, 1U, "array", "dim", "mask"),
    ARGS1(MAXEXPONENT, 1U, "x"),
    VARARGS2(MAX, "a1", "a2"),
    ARGS3(MERGE, 7U, "tsource", "fsource", "mask"),
    ARGS5(MINLOC, 1U, "array", "dim", "mask", "kind", "back"),
    ARGS3(MINVAL, 1U, "array", "dim", "mask"),
    VARARGS2(MIN, "a1", "a2"),
    ARGS1(MINEXPONENT, 1U, "x"),
    ARGS2(MOD, 3U, "a", "p"),
    ARGS2(MODULO, 3U, "a", "p"),
    ARGS2(NEAREST, 3U, "x", "s"),
    ARGS2(NINT, 1U, "a", "kind"),
    ARGS1(NOT, 1U, "i"),
    ARGS1(NULL, 0U, "mold"),
    ARGS5(MVBITS, 31U, "from", "frompos", "len", "to", "topos"),
    ARGS1(PRECISION, 1U, "x"),
    ARGS3(PACK, 3U, "array", "mask", "vector"),
    ARGS1(PRESENT, 1U, "a"),
    ARGS3(PRODUCT, 1U, "array", "dim", "mask"),
    ARGS1(RANDOM_NUMBER, 1U, "harvest"),
    ARGS3(RANDOM_SEED, 0U, "size", "put", "get"),
    ARGS1(RADIX, 1U, "x"),
    ARGS1(RANGE, 1U, "x"),
    ARGS2(REAL, 1U, "a", "kind"),
    ARGS2(REPEAT, 3U, "string", "ncopies"),
    ARGS4(RESHAPE, 3U, "source", "shape", "pad", "order"),
    ARGS1(RRSPACING, 1U, "x"),
    ARGS2(SCALE, 3U, "x", "i"),
    ARGS4(SCAN, 3U, "string", "set", "back", "kind"),
    ARGS1(SELECTED_INT_KIND, 1U, "r"),
    ARGS3(SELECTED_REAL_KIND, 0U, "p", "r", "radix"),
    ARGS2(SET_EXPONENT, 3U, "x", "i"),
    ARGS2(SIGN, 3U, "a", "b"),
    ARGS2(SHAPE, 1U, "source", "kind"),
    ARGS1(SIN, 1U, "x"),
    ARGS1(SINH, 1U, "x"),
    ARGS1(SPACING, 1U, "x"),
    ARGS3(SPREAD, 7U, "source", "dim", "ncopies"),
    ARGS1(SQRT, 1U, "x"),
    ARGS3(SIZE, 1U, "array", "dim", "kind"),
    ARGS3(SUM, 1U, "array", "dim", "mask"),
    ARGS3(SYSTEM_CLOCK, 0U, "count", "count_rate", "count_max"),
    ARGS1(TAN, 1U, "x"),
    ARGS1(TANH, 1U, "x"),
    ARGS1(TINY, 1U, "x"),
    ARGS3(TRANSFER, 3U, "source", "mold", "size"),
    ARGS1(TRIM, 1U, "string"),
    ARGS1(TRANSPOSE, 1U, "matrix"),
    ARGS3(UBOUND, 1U, "array", "dim", "kind"),
    ARGS3(UNPACK, 7U, "vector", "mask", "field"),
    ARGS4(VERIFY, 3U, "string", "set", "back", "kind"),
};

const F2cIntrinsicDescriptor *f2c_intrinsic_descriptor(F2cIntrinsicId intrinsic) {
    const F2cIntrinsicDescriptor *descriptor;
    if (intrinsic <= F2C_INTRINSIC_NONE || intrinsic >= F2C_INTRINSIC_ID_COUNT)
        return NULL;
    descriptor = &descriptors[intrinsic];
    return descriptor->id == intrinsic && descriptor->canonical_name != NULL ? descriptor : NULL;
}

const F2cIntrinsicArgumentSchema *f2c_intrinsic_argument_schema(F2cIntrinsicId intrinsic) {
    const F2cIntrinsicArgumentSchema *schema;
    if (f2c_intrinsic_descriptor(intrinsic) == NULL)
        return NULL;
    schema = &argument_schemas[intrinsic];
    return schema->count != 0U ? schema : NULL;
}

const F2cIntrinsicDescriptor *f2c_find_intrinsic_descriptor(const char *name) {
    F2cIntrinsicId intrinsic;
    if (name == NULL)
        return NULL;
    for (intrinsic = (F2cIntrinsicId)(F2C_INTRINSIC_NONE + 1);
         intrinsic < F2C_INTRINSIC_ID_COUNT; intrinsic = (F2cIntrinsicId)(intrinsic + 1)) {
        const F2cIntrinsicDescriptor *descriptor = f2c_intrinsic_descriptor(intrinsic);
        if (descriptor != NULL && strcmp(descriptor->canonical_name, name) == 0)
            return descriptor;
    }
    return NULL;
}

int f2c_intrinsic_has_family(F2cIntrinsicId intrinsic, F2cIntrinsicFamily family) {
    const F2cIntrinsicDescriptor *descriptor = f2c_intrinsic_descriptor(intrinsic);
    return descriptor != NULL && family != F2C_INTRINSIC_FAMILY_NONE &&
           (descriptor->families & (unsigned int)family) != 0U;
}

#undef VARARGS2
#undef ARGS6
#undef ARGS5
#undef ARGS4
#undef ARGS3
#undef ARGS2
#undef ARGS1
#undef NONE
#undef ASSUMED_SIZE
#undef ARRAY_INQUIRY
#undef TRANSFORM
#undef REDUCTION
#undef REPRESENTATION
#undef NUMERIC
#undef MODEL
#undef MATHEMATICAL
#undef CONVERSION
#undef CHARACTER
#undef BIT
#undef EXTENSION
#undef F2008
#undef F95
#undef F90
#undef F77
#undef SUBROUTINE
#undef FUNCTION
