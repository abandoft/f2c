#include "internal/f2c.h"

#include <string.h>

/* Intrinsic source names are registered once here before typed semantic resolution. */

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
#define FIRST_RANK(name, minimum, maximum, type)                                                   \
    {name,                                                                                         \
     minimum,                                                                                      \
     maximum,                                                                                      \
     type,                                                                                         \
     F2C_INTRINSIC_RANK_FIRST,                                                                     \
     F2C_INTRINSIC_NONE,                                                                           \
     F2C_INTRINSIC_KIND_DEFAULT}
#define TYPED_ELEMENTAL(name, minimum, maximum, type, id, kind)                                    \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_ELEMENTAL, id, kind}
#define TYPED_SCALAR(name, minimum, maximum, type, id, kind)                                       \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_SCALAR, id, kind}
#define TYPED_FIRST_RANK(name, minimum, maximum, type, id, kind)                                   \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_FIRST, id, kind}
#define TYPED_REDUCTION(name, minimum, maximum, type, id, kind)                                    \
    {name, minimum, maximum, type, F2C_INTRINSIC_RANK_REDUCTION, id, kind}
#define TYPED_LOCATION(name, minimum, maximum, id)                                                 \
    {name,                                                                                         \
     minimum,                                                                                      \
     maximum,                                                                                      \
     F2C_INTRINSIC_TYPE_INTEGER,                                                                   \
     F2C_INTRINSIC_RANK_LOCATION,                                                                  \
     id,                                                                                           \
     F2C_INTRINSIC_KIND_OPTIONAL}

static const F2cIntrinsicSignature intrinsic_signatures[] = {
    TYPED_ELEMENTAL("abs", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE, F2C_INTRINSIC_ABS,
                    F2C_INTRINSIC_KIND_FIRST),
    ELEMENTAL("abs1", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("abssq", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    TYPED_ELEMENTAL("achar", 1U, 2U, F2C_INTRINSIC_TYPE_CHARACTER, F2C_INTRINSIC_ACHAR,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("acos", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_ACOS,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("adjustl", 1U, 1U, F2C_INTRINSIC_TYPE_CHARACTER, F2C_INTRINSIC_ADJUSTL,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("adjustr", 1U, 1U, F2C_INTRINSIC_TYPE_CHARACTER, F2C_INTRINSIC_ADJUSTR,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("aint", 1U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_AINT,
                    F2C_INTRINSIC_KIND_FIRST_OPTIONAL),
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
    TYPED_SCALAR("allocated", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_ALLOCATED,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("anint", 1U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_ANINT,
                    F2C_INTRINSIC_KIND_FIRST_OPTIONAL),
    TYPED_SCALAR("associated", 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_ASSOCIATED,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("aimag", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE, F2C_INTRINSIC_AIMAG,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("asin", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_ASIN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("atan", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_ATAN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("atan2", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_ATAN2,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("bit_size", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_BIT_SIZE,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("btest", 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_BTEST,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_REDUCTION("all", 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_ALL,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_REDUCTION("any", 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_ANY,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("cabs", 1U, 1U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_ABS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    ELEMENTAL("cabs1", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    ELEMENTAL("cabs2", 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE),
    TYPED_ELEMENTAL("cdabs", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_ABS,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("ceiling", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_CEILING,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("char", 1U, 2U, F2C_INTRINSIC_TYPE_CHARACTER, F2C_INTRINSIC_CHAR,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("cmplx", 1U, 3U, F2C_INTRINSIC_TYPE_COMPLEX, F2C_INTRINSIC_CMPLX,
                    F2C_INTRINSIC_KIND_OPTIONAL),
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
    TYPED_REDUCTION("count", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_COUNT,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_FIRST_RANK("cshift", 2U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_CSHIFT,
                     F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("conjg", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_CONJG,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("cos", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_COS,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("cosh", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_COSH,
                    F2C_INTRINSIC_KIND_FIRST),
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
    TYPED_ELEMENTAL("dble", 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_DBLE,
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
    TYPED_SCALAR("digits", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_DIGITS,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("dim", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_DIM,
                    F2C_INTRINSIC_KIND_FIRST),
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
    TYPED_ELEMENTAL("dprod", 2U, 2U, F2C_INTRINSIC_TYPE_DOUBLE, F2C_INTRINSIC_DPROD,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("dot_product", 2U, 2U, F2C_INTRINSIC_TYPE_COMMON, F2C_INTRINSIC_DOT_PRODUCT,
                 F2C_INTRINSIC_KIND_COMMON),
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
    TYPED_SCALAR("epsilon", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_EPSILON,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("exp", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_EXP,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("exponent", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_EXPONENT,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("float", 1U, 1U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_REAL,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("floor", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_FLOOR,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_SCALAR("findloc", 2U, 6U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_FINDLOC,
                 F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("fraction", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_FRACTION,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("huge", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_HUGE,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("iachar", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IACHAR,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("iand", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IAND,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("ibclr", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IBCLR,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("ibits", 3U, 3U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IBITS,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("ibset", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IBSET,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("ichar", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_ICHAR,
                    F2C_INTRINSIC_KIND_OPTIONAL),
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
    TYPED_ELEMENTAL("ieor", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IEOR,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("index", 2U, 4U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_INDEX,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("int", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_INT,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("ior", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_IOR,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("ishft", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_ISHFT,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("ishftc", 2U, 3U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_ISHFTC,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("isign", 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_SIGN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("isnan", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_ISNAN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("kind", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_KIND,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("lbound", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_LBOUND,
                 F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("la_isnan", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_ISNAN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("len", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_LEN,
                 F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("len_trim", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_LEN_TRIM,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("lge", 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_LGE,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("lgt", 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_LGT,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("lle", 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_LLE,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("llt", 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_LLT,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("log", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_LOG,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("log10", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_LOG10,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("logical", 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_LOGICAL,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_SCALAR("matmul", 2U, 2U, F2C_INTRINSIC_TYPE_COMMON, F2C_INTRINSIC_MATMUL,
                 F2C_INTRINSIC_KIND_COMMON),
    TYPED_ELEMENTAL("max", 2U, 64U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_MAX,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("max0", 2U, 64U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MAX,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("max1", 2U, 64U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MAX,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("maxexponent", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MAXEXPONENT,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_LOCATION("maxloc", 1U, 5U, F2C_INTRINSIC_MAXLOC),
    TYPED_REDUCTION("maxval", 1U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_MAXVAL,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("merge", 3U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_MERGE,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("min", 2U, 64U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_MIN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("min0", 2U, 64U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("min1", 2U, 64U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MIN,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("minexponent", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_MINEXPONENT,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_LOCATION("minloc", 1U, 5U, F2C_INTRINSIC_MINLOC),
    TYPED_REDUCTION("minval", 1U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_MINVAL,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("mod", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_MOD,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("modulo", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_MODULO,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("nearest", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_NEAREST,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("nint", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_NINT,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("not", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_NOT,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("null", 0U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_NULL,
                 F2C_INTRINSIC_KIND_FIRST),
    SCALAR("omp_get_num_threads", 0U, 0U, F2C_INTRINSIC_TYPE_INTEGER),
    SCALAR("omp_get_thread_num", 0U, 0U, F2C_INTRINSIC_TYPE_INTEGER),
    TYPED_SCALAR("present", 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL, F2C_INTRINSIC_PRESENT,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("precision", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_PRECISION,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_REDUCTION("product", 1U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_PRODUCT,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("pack", 2U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_PACK,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("radix", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RADIX,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("range", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANGE,
                 F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("real", 1U, 2U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_REAL,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_SCALAR("reshape", 2U, 4U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RESHAPE,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("repeat", 2U, 2U, F2C_INTRINSIC_TYPE_CHARACTER, F2C_INTRINSIC_REPEAT,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("rrspacing", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RRSPACING,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("scale", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SCALE,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("scan", 2U, 4U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_SCAN,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_SCALAR("selected_int_kind", 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER,
                 F2C_INTRINSIC_SELECTED_INT_KIND, F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_SCALAR("selected_real_kind", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER,
                 F2C_INTRINSIC_SELECTED_REAL_KIND, F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("set_exponent", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SET_EXPONENT,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("shape", 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_SHAPE,
                 F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("sign", 2U, 2U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SIGN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("sin", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SIN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("sinh", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SINH,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("sqrt", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SQRT,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("sngl", 1U, 1U, F2C_INTRINSIC_TYPE_REAL, F2C_INTRINSIC_REAL,
                    F2C_INTRINSIC_KIND_DEFAULT),
    TYPED_ELEMENTAL("spacing", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SPACING,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("spread", 3U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SPREAD,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("size", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_SIZE,
                 F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_REDUCTION("sum", 1U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_SUM,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("tan", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_TAN,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_ELEMENTAL("tanh", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_TANH,
                    F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("tiny", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_TINY,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("transpose", 1U, 1U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_TRANSPOSE,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("trim", 1U, 1U, F2C_INTRINSIC_TYPE_CHARACTER, F2C_INTRINSIC_TRIM,
                 F2C_INTRINSIC_KIND_FIRST),
    {"transfer", 2U, 3U, F2C_INTRINSIC_TYPE_MOLD, F2C_INTRINSIC_RANK_MOLD,
     F2C_INTRINSIC_TRANSFER, F2C_INTRINSIC_KIND_DEFAULT},
    TYPED_SCALAR("unpack", 3U, 3U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_UNPACK,
                 F2C_INTRINSIC_KIND_FIRST),
    TYPED_SCALAR("ubound", 1U, 3U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_UBOUND,
                 F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_ELEMENTAL("verify", 2U, 4U, F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_VERIFY,
                    F2C_INTRINSIC_KIND_OPTIONAL),
    TYPED_FIRST_RANK("eoshift", 2U, 4U, F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_EOSHIFT,
                     F2C_INTRINSIC_KIND_FIRST),
};

#undef ELEMENTAL
#undef SCALAR
#undef FIRST_RANK
#undef TYPED_ELEMENTAL
#undef TYPED_LOCATION
#undef TYPED_REDUCTION
#undef TYPED_SCALAR

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

int f2c_is_intrinsic_subroutine(const char *name) {
    return name != NULL && (strcmp(name, "cpu_time") == 0 || strcmp(name, "date_and_time") == 0 ||
                            strcmp(name, "mvbits") == 0 || strcmp(name, "random_number") == 0 ||
                            strcmp(name, "random_seed") == 0 || strcmp(name, "system_clock") == 0);
}

int f2c_intrinsic_is_bit(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_BTEST:
    case F2C_INTRINSIC_IAND:
    case F2C_INTRINSIC_IBCLR:
    case F2C_INTRINSIC_IBITS:
    case F2C_INTRINSIC_IBSET:
    case F2C_INTRINSIC_IEOR:
    case F2C_INTRINSIC_IOR:
    case F2C_INTRINSIC_ISHFT:
    case F2C_INTRINSIC_ISHFTC:
    case F2C_INTRINSIC_NOT:
    case F2C_INTRINSIC_MVBITS:
        return 1;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_ACHAR:
    case F2C_INTRINSIC_ADJUSTL:
    case F2C_INTRINSIC_ADJUSTR:
    case F2C_INTRINSIC_CHAR:
    case F2C_INTRINSIC_IACHAR:
    case F2C_INTRINSIC_ICHAR:
    case F2C_INTRINSIC_INDEX:
    case F2C_INTRINSIC_LEN:
    case F2C_INTRINSIC_LEN_TRIM:
    case F2C_INTRINSIC_LGE:
    case F2C_INTRINSIC_LGT:
    case F2C_INTRINSIC_LLE:
    case F2C_INTRINSIC_LLT:
    case F2C_INTRINSIC_REPEAT:
    case F2C_INTRINSIC_SCAN:
    case F2C_INTRINSIC_TRIM:
    case F2C_INTRINSIC_VERIFY:
    default:
        return 0;
    }
}

int f2c_intrinsic_is_character(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_ACHAR:
    case F2C_INTRINSIC_ADJUSTL:
    case F2C_INTRINSIC_ADJUSTR:
    case F2C_INTRINSIC_CHAR:
    case F2C_INTRINSIC_IACHAR:
    case F2C_INTRINSIC_ICHAR:
    case F2C_INTRINSIC_INDEX:
    case F2C_INTRINSIC_LEN:
    case F2C_INTRINSIC_LEN_TRIM:
    case F2C_INTRINSIC_LGE:
    case F2C_INTRINSIC_LGT:
    case F2C_INTRINSIC_LLE:
    case F2C_INTRINSIC_LLT:
    case F2C_INTRINSIC_REPEAT:
    case F2C_INTRINSIC_SCAN:
    case F2C_INTRINSIC_TRIM:
    case F2C_INTRINSIC_VERIFY:
        return 1;
    case F2C_INTRINSIC_NONE:
    case F2C_INTRINSIC_BIT_SIZE:
    case F2C_INTRINSIC_BTEST:
    case F2C_INTRINSIC_IAND:
    case F2C_INTRINSIC_IBCLR:
    case F2C_INTRINSIC_IBITS:
    case F2C_INTRINSIC_IBSET:
    case F2C_INTRINSIC_IEOR:
    case F2C_INTRINSIC_IOR:
    case F2C_INTRINSIC_ISHFT:
    case F2C_INTRINSIC_ISHFTC:
    case F2C_INTRINSIC_NOT:
    case F2C_INTRINSIC_MVBITS:
    default:
        return 0;
    }
}

int f2c_intrinsic_is_conversion(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_AIMAG:
    case F2C_INTRINSIC_CMPLX:
    case F2C_INTRINSIC_CONJG:
    case F2C_INTRINSIC_DBLE:
    case F2C_INTRINSIC_INT:
    case F2C_INTRINSIC_LOGICAL:
    case F2C_INTRINSIC_REAL:
        return 1;
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
}

int f2c_intrinsic_is_mathematical(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_ABS:
    case F2C_INTRINSIC_ACOS:
    case F2C_INTRINSIC_ASIN:
    case F2C_INTRINSIC_ATAN:
    case F2C_INTRINSIC_ATAN2:
    case F2C_INTRINSIC_COS:
    case F2C_INTRINSIC_COSH:
    case F2C_INTRINSIC_DPROD:
    case F2C_INTRINSIC_EXP:
    case F2C_INTRINSIC_LOG:
    case F2C_INTRINSIC_LOG10:
    case F2C_INTRINSIC_MAX:
    case F2C_INTRINSIC_MIN:
    case F2C_INTRINSIC_SIN:
    case F2C_INTRINSIC_SINH:
    case F2C_INTRINSIC_SQRT:
    case F2C_INTRINSIC_TAN:
    case F2C_INTRINSIC_TANH:
        return 1;
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
}

int f2c_intrinsic_is_numeric_model(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_DIGITS:
    case F2C_INTRINSIC_EPSILON:
    case F2C_INTRINSIC_HUGE:
    case F2C_INTRINSIC_KIND:
    case F2C_INTRINSIC_MAXEXPONENT:
    case F2C_INTRINSIC_MINEXPONENT:
    case F2C_INTRINSIC_PRECISION:
    case F2C_INTRINSIC_RADIX:
    case F2C_INTRINSIC_RANGE:
    case F2C_INTRINSIC_SELECTED_INT_KIND:
    case F2C_INTRINSIC_SELECTED_REAL_KIND:
    case F2C_INTRINSIC_TINY:
        return 1;
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
}

int f2c_intrinsic_is_numeric_operation(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_AINT:
    case F2C_INTRINSIC_ANINT:
    case F2C_INTRINSIC_CEILING:
    case F2C_INTRINSIC_DIM:
    case F2C_INTRINSIC_FLOOR:
    case F2C_INTRINSIC_MERGE:
    case F2C_INTRINSIC_MOD:
    case F2C_INTRINSIC_MODULO:
    case F2C_INTRINSIC_NINT:
    case F2C_INTRINSIC_SIGN:
        return 1;
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
}

int f2c_intrinsic_is_real_representation(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_EXPONENT:
    case F2C_INTRINSIC_FRACTION:
    case F2C_INTRINSIC_NEAREST:
    case F2C_INTRINSIC_RRSPACING:
    case F2C_INTRINSIC_SCALE:
    case F2C_INTRINSIC_SET_EXPONENT:
    case F2C_INTRINSIC_SPACING:
        return 1;
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
}

int f2c_intrinsic_is_reduction(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_ALL:
    case F2C_INTRINSIC_ANY:
    case F2C_INTRINSIC_COUNT:
    case F2C_INTRINSIC_DOT_PRODUCT:
    case F2C_INTRINSIC_MAXLOC:
    case F2C_INTRINSIC_MAXVAL:
    case F2C_INTRINSIC_MINLOC:
    case F2C_INTRINSIC_MINVAL:
    case F2C_INTRINSIC_PRODUCT:
    case F2C_INTRINSIC_SUM:
        return 1;
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
}

int f2c_intrinsic_is_transformational(F2cIntrinsicId intrinsic) {
    switch (intrinsic) {
    case F2C_INTRINSIC_ALL:
    case F2C_INTRINSIC_ANY:
    case F2C_INTRINSIC_COUNT:
    case F2C_INTRINSIC_CSHIFT:
    case F2C_INTRINSIC_DOT_PRODUCT:
    case F2C_INTRINSIC_EOSHIFT:
    case F2C_INTRINSIC_FINDLOC:
    case F2C_INTRINSIC_MATMUL:
    case F2C_INTRINSIC_MAXLOC:
    case F2C_INTRINSIC_MAXVAL:
    case F2C_INTRINSIC_MINLOC:
    case F2C_INTRINSIC_MINVAL:
    case F2C_INTRINSIC_PACK:
    case F2C_INTRINSIC_PRODUCT:
    case F2C_INTRINSIC_RESHAPE:
    case F2C_INTRINSIC_SPREAD:
    case F2C_INTRINSIC_SUM:
    case F2C_INTRINSIC_TRANSPOSE:
    case F2C_INTRINSIC_UNPACK:
        return 1;
    case F2C_INTRINSIC_NONE:
    default:
        return 0;
    }
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
    if (signature->id == F2C_INTRINSIC_PACK)
        return 1U;
    if (strcmp(name, "shape") == 0)
        return 1U;
    if (strcmp(name, "lbound") == 0 || strcmp(name, "ubound") == 0)
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
    for (i = 0U; i < count; ++i) {
        const F2cExpr *argument = argument_value(arguments[i]);
        if (argument != NULL && argument->rank > rank)
            rank = argument->rank;
    }
    return rank;
}

static const char *kind_source_name(const F2cIntrinsicSignature *signature) {
    if (signature->id == F2C_INTRINSIC_AINT || signature->id == F2C_INTRINSIC_ANINT)
        return "a";
    if (signature->id == F2C_INTRINSIC_ABS || signature->id == F2C_INTRINSIC_DBLE ||
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
