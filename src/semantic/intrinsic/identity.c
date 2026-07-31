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

const F2cIntrinsicDescriptor *f2c_intrinsic_descriptor(F2cIntrinsicId intrinsic) {
    const F2cIntrinsicDescriptor *descriptor;
    if (intrinsic <= F2C_INTRINSIC_NONE || intrinsic >= F2C_INTRINSIC_ID_COUNT)
        return NULL;
    descriptor = &descriptors[intrinsic];
    return descriptor->id == intrinsic && descriptor->canonical_name != NULL ? descriptor : NULL;
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
