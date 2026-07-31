#include "semantic/intrinsic.h"

#include <string.h>

#define F77 F2C_INTRINSIC_STANDARD_FORTRAN_77
#define F90 F2C_INTRINSIC_STANDARD_FORTRAN_90
#define F95 F2C_INTRINSIC_STANDARD_FORTRAN_95
#define F2008 F2C_INTRINSIC_STANDARD_FORTRAN_2008
#define EXTENSION F2C_INTRINSIC_STANDARD_EXTENSION
#define F2C_INTRINSIC_NULL_ID F2C_INTRINSIC_NULL

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

#define SPEC(procedure, id, name, standard, families, minimum, maximum, type, rank, kind,          \
             required, variadic, count, ...)                                                       \
    [F2C_INTRINSIC_##id] = {                                                                       \
        {F2C_INTRINSIC_##id, name, standard, F2C_INTRINSIC_PROCEDURE_##procedure, families},       \
        {{__VA_ARGS__}, count, required, variadic},                                                \
        {name, minimum, maximum, type, rank, F2C_INTRINSIC_##id, kind}}
#define SPEC_ARGS1(procedure, id, name, standard, families, minimum, maximum, type, rank, kind,    \
                   required, a0)                                                                   \
    SPEC(procedure, id, name, standard, families, minimum, maximum, type, rank, kind, required,    \
         0U, 1U, a0)
#define SPEC_ARGS2(procedure, id, name, standard, families, minimum, maximum, type, rank, kind,    \
                   required, a0, a1)                                                               \
    SPEC(procedure, id, name, standard, families, minimum, maximum, type, rank, kind, required,    \
         0U, 2U, a0, a1)
#define SPEC_ARGS3(procedure, id, name, standard, families, minimum, maximum, type, rank, kind,    \
                   required, a0, a1, a2)                                                           \
    SPEC(procedure, id, name, standard, families, minimum, maximum, type, rank, kind, required,    \
         0U, 3U, a0, a1, a2)
#define SPEC_ARGS4(procedure, id, name, standard, families, minimum, maximum, type, rank, kind,    \
                   required, a0, a1, a2, a3)                                                       \
    SPEC(procedure, id, name, standard, families, minimum, maximum, type, rank, kind, required,    \
         0U, 4U, a0, a1, a2, a3)
#define SPEC_ARGS5(procedure, id, name, standard, families, minimum, maximum, type, rank, kind,    \
                   required, a0, a1, a2, a3, a4)                                                   \
    SPEC(procedure, id, name, standard, families, minimum, maximum, type, rank, kind, required,    \
         0U, 5U, a0, a1, a2, a3, a4)
#define SPEC_ARGS6(procedure, id, name, standard, families, minimum, maximum, type, rank, kind,    \
                   required, a0, a1, a2, a3, a4, a5)                                               \
    SPEC(procedure, id, name, standard, families, minimum, maximum, type, rank, kind, required,    \
         0U, 6U, a0, a1, a2, a3, a4, a5)
#define SPEC_VARARGS2(procedure, id, name, standard, families, minimum, maximum, type, rank, kind, \
                      a0, a1)                                                                      \
    SPEC(procedure, id, name, standard, families, minimum, maximum, type, rank, kind, 3U, 1U, 2U,  \
         a0, a1)

static const F2cIntrinsicSpecification specifications[F2C_INTRINSIC_ID_COUNT] = {
    SPEC_ARGS1(FUNCTION, ABS, "abs", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "a"),
    SPEC_ARGS1(FUNCTION, ACOS, "acos", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS2(FUNCTION, ACHAR, "achar", F90, CHARACTER, 1U, 2U, F2C_INTRINSIC_TYPE_CHARACTER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "i", "kind"),
    SPEC_ARGS1(FUNCTION, ADJUSTL, "adjustl", F90, CHARACTER, 1U, 1U, F2C_INTRINSIC_TYPE_CHARACTER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "string"),
    SPEC_ARGS1(FUNCTION, ADJUSTR, "adjustr", F90, CHARACTER, 1U, 1U, F2C_INTRINSIC_TYPE_CHARACTER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "string"),
    SPEC_ARGS2(FUNCTION, AINT, "aint", F77, NUMERIC, 1U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST_OPTIONAL, 1U, "a", "kind"),
    SPEC_ARGS1(FUNCTION, AIMAG, "aimag", F77, CONVERSION, 1U, 1U, F2C_INTRINSIC_TYPE_ABSOLUTE,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "z"),
    SPEC_ARGS2(FUNCTION, ALL, "all", F90, REDUCTION | TRANSFORM, 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_REDUCTION, F2C_INTRINSIC_KIND_DEFAULT, 1U, "mask", "dim"),
    SPEC_ARGS1(FUNCTION, ALLOCATED, "allocated", F90, NONE, 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 1U, "array"),
    SPEC_ARGS2(FUNCTION, ANINT, "anint", F77, NUMERIC, 1U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST_OPTIONAL, 1U, "a", "kind"),
    SPEC_ARGS2(FUNCTION, ANY, "any", F90, REDUCTION | TRANSFORM, 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_REDUCTION, F2C_INTRINSIC_KIND_DEFAULT, 1U, "mask", "dim"),
    SPEC_ARGS2(FUNCTION, ASSOCIATED, "associated", F90, NONE, 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 1U, "pointer", "target"),
    SPEC_ARGS1(FUNCTION, ASIN, "asin", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS1(FUNCTION, ATAN, "atan", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS2(FUNCTION, ATAN2, "atan2", F77, MATHEMATICAL, 2U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "y", "x"),
    SPEC_ARGS1(FUNCTION, BIT_SIZE, "bit_size", F90, BIT, 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 1U, "i"),
    SPEC_ARGS2(FUNCTION, BTEST, "btest", F90, BIT, 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT, 3U, "i", "pos"),
    SPEC_ARGS2(FUNCTION, CEILING, "ceiling", F90, NUMERIC, 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "a", "kind"),
    SPEC_ARGS2(FUNCTION, CHAR, "char", F77, CHARACTER, 1U, 2U, F2C_INTRINSIC_TYPE_CHARACTER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "i", "kind"),
    SPEC_ARGS3(FUNCTION, CMPLX, "cmplx", F77, CONVERSION, 1U, 3U, F2C_INTRINSIC_TYPE_COMPLEX,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "x", "y", "kind"),
    SPEC_ARGS1(FUNCTION, CONJG, "conjg", F77, CONVERSION, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "z"),
    SPEC_ARGS1(FUNCTION, COS, "cos", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS1(FUNCTION, COSH, "cosh", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS3(FUNCTION, COUNT, "count", F90, REDUCTION | TRANSFORM, 1U, 3U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_REDUCTION,
               F2C_INTRINSIC_KIND_OPTIONAL, 1U, "mask", "dim", "kind"),
    SPEC_ARGS1(SUBROUTINE, CPU_TIME, "cpu_time", F95, NONE, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 1U, "time"),
    SPEC_ARGS3(FUNCTION, CSHIFT, "cshift", F90, TRANSFORM, 2U, 3U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_FIRST, F2C_INTRINSIC_KIND_FIRST, 3U, "array", "shift", "dim"),
    SPEC_ARGS4(SUBROUTINE, DATE_AND_TIME, "date_and_time", F90, NONE, 0U, 4U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 0U,
               "date", "time", "zone", "values"),
    SPEC_ARGS1(FUNCTION, DBLE, "dble", F77, CONVERSION, 1U, 1U, F2C_INTRINSIC_TYPE_DOUBLE,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT, 1U, "a"),
    SPEC_ARGS1(FUNCTION, DIGITS, "digits", F90, MODEL | ASSUMED_SIZE, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "x"),
    SPEC_ARGS2(FUNCTION, DIM, "dim", F77, NUMERIC, 2U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "x", "y"),
    SPEC_ARGS2(FUNCTION, DOT_PRODUCT, "dot_product", F90, REDUCTION | TRANSFORM, 2U, 2U,
               F2C_INTRINSIC_TYPE_COMMON, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_COMMON, 3U,
               "vector_a", "vector_b"),
    SPEC_ARGS2(FUNCTION, DPROD, "dprod", F77, MATHEMATICAL, 2U, 2U, F2C_INTRINSIC_TYPE_DOUBLE,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT, 3U, "x", "y"),
    SPEC_ARGS1(FUNCTION, EPSILON, "epsilon", F90, MODEL | ASSUMED_SIZE, 1U, 1U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 1U,
               "x"),
    SPEC_ARGS4(FUNCTION, EOSHIFT, "eoshift", F90, TRANSFORM, 2U, 4U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_FIRST, F2C_INTRINSIC_KIND_FIRST, 3U, "array", "shift", "boundary",
               "dim"),
    SPEC_ARGS1(FUNCTION, EXP, "exp", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS1(FUNCTION, EXPONENT, "exponent", F90, REPRESENTATION, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "x"),
    SPEC_ARGS2(FUNCTION, FLOOR, "floor", F90, NUMERIC, 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "a", "kind"),
    SPEC_ARGS6(FUNCTION, FINDLOC, "findloc", F2008, TRANSFORM, 2U, 6U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_OPTIONAL, 3U, "array", "value", "dim",
               "mask", "kind", "back"),
    SPEC_ARGS1(FUNCTION, FRACTION, "fraction", F90, REPRESENTATION, 1U, 1U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U,
               "x"),
    SPEC_ARGS1(FUNCTION, HUGE, "huge", F90, MODEL | ASSUMED_SIZE, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS2(FUNCTION, IACHAR, "iachar", F90, CHARACTER, 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "c", "kind"),
    SPEC_ARGS2(FUNCTION, IAND, "iand", F90, BIT, 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "i", "j"),
    SPEC_ARGS2(FUNCTION, IBCLR, "ibclr", F90, BIT, 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "i", "pos"),
    SPEC_ARGS3(FUNCTION, IBITS, "ibits", F90, BIT, 3U, 3U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 7U, "i", "pos", "len"),
    SPEC_ARGS2(FUNCTION, IBSET, "ibset", F90, BIT, 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "i", "pos"),
    SPEC_ARGS2(FUNCTION, ICHAR, "ichar", F77, CHARACTER, 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "c", "kind"),
    SPEC_ARGS2(FUNCTION, IEOR, "ieor", F90, BIT, 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "i", "j"),
    SPEC_ARGS4(FUNCTION, INDEX, "index", F77, CHARACTER, 2U, 4U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 3U, "string", "substring",
               "back", "kind"),
    SPEC_ARGS2(FUNCTION, IOR, "ior", F90, BIT, 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "i", "j"),
    SPEC_ARGS2(FUNCTION, ISHFT, "ishft", F90, BIT, 2U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "i", "shift"),
    SPEC_ARGS3(FUNCTION, ISHFTC, "ishftc", F90, BIT, 2U, 3U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "i", "shift", "size"),
    SPEC_ARGS1(FUNCTION, ISNAN, "isnan", EXTENSION, NONE, 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT, 1U, "x"),
    SPEC_ARGS2(FUNCTION, INT, "int", F77, CONVERSION, 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "a", "kind"),
    SPEC_ARGS1(FUNCTION, KIND, "kind", F90, MODEL | ASSUMED_SIZE, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "x"),
    SPEC_ARGS3(FUNCTION, LBOUND, "lbound", F90, ARRAY_INQUIRY | ASSUMED_SIZE, 1U, 3U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_OPTIONAL,
               1U, "array", "dim", "kind"),
    SPEC_ARGS2(FUNCTION, LEN, "len", F77, CHARACTER | ASSUMED_SIZE, 1U, 2U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_OPTIONAL,
               1U, "string", "kind"),
    SPEC_ARGS2(FUNCTION, LEN_TRIM, "len_trim", F90, CHARACTER, 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "string", "kind"),
    SPEC_ARGS2(FUNCTION, LGE, "lge", F77, CHARACTER, 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT, 3U, "string_a",
               "string_b"),
    SPEC_ARGS2(FUNCTION, LGT, "lgt", F77, CHARACTER, 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT, 3U, "string_a",
               "string_b"),
    SPEC_ARGS2(FUNCTION, LLE, "lle", F77, CHARACTER, 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT, 3U, "string_a",
               "string_b"),
    SPEC_ARGS2(FUNCTION, LLT, "llt", F77, CHARACTER, 2U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_DEFAULT, 3U, "string_a",
               "string_b"),
    SPEC_ARGS1(FUNCTION, LOG, "log", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS1(FUNCTION, LOG10, "log10", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS2(FUNCTION, LOGICAL, "logical", F90, CONVERSION, 1U, 2U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "l", "kind"),
    SPEC_ARGS2(FUNCTION, MATMUL, "matmul", F90, TRANSFORM, 2U, 2U, F2C_INTRINSIC_TYPE_COMMON,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_COMMON, 3U, "matrix_a", "matrix_b"),
    SPEC_ARGS5(FUNCTION, MAXLOC, "maxloc", F90, REDUCTION | TRANSFORM, 1U, 5U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_LOCATION, F2C_INTRINSIC_KIND_OPTIONAL,
               1U, "array", "dim", "mask", "kind", "back"),
    SPEC_ARGS3(FUNCTION, MAXVAL, "maxval", F90, REDUCTION | TRANSFORM, 1U, 3U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_REDUCTION, F2C_INTRINSIC_KIND_FIRST, 1U,
               "array", "dim", "mask"),
    SPEC_ARGS1(FUNCTION, MAXEXPONENT, "maxexponent", F90, MODEL | ASSUMED_SIZE, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "x"),
    SPEC_VARARGS2(FUNCTION, MAX, "max", F77, MATHEMATICAL, 2U, 64U, F2C_INTRINSIC_TYPE_FIRST,
                  F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, "a1", "a2"),
    SPEC_ARGS3(FUNCTION, MERGE, "merge", F90, NUMERIC, 3U, 3U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 7U, "tsource", "fsource",
               "mask"),
    SPEC_ARGS5(FUNCTION, MINLOC, "minloc", F90, REDUCTION | TRANSFORM, 1U, 5U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_LOCATION, F2C_INTRINSIC_KIND_OPTIONAL,
               1U, "array", "dim", "mask", "kind", "back"),
    SPEC_ARGS3(FUNCTION, MINVAL, "minval", F90, REDUCTION | TRANSFORM, 1U, 3U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_REDUCTION, F2C_INTRINSIC_KIND_FIRST, 1U,
               "array", "dim", "mask"),
    SPEC_VARARGS2(FUNCTION, MIN, "min", F77, MATHEMATICAL, 2U, 64U, F2C_INTRINSIC_TYPE_FIRST,
                  F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, "a1", "a2"),
    SPEC_ARGS1(FUNCTION, MINEXPONENT, "minexponent", F90, MODEL | ASSUMED_SIZE, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "x"),
    SPEC_ARGS2(FUNCTION, MOD, "mod", F77, NUMERIC, 2U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "a", "p"),
    SPEC_ARGS2(FUNCTION, MODULO, "modulo", F90, NUMERIC, 2U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "a", "p"),
    SPEC_ARGS2(FUNCTION, NEAREST, "nearest", F90, REPRESENTATION, 2U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "x", "s"),
    SPEC_ARGS2(FUNCTION, NINT, "nint", F77, NUMERIC, 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "a", "kind"),
    SPEC_ARGS1(FUNCTION, NOT, "not", F90, BIT, 1U, 1U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "i"),
    SPEC_ARGS1(FUNCTION, NULL_ID, "null", F95, NONE, 0U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 0U, "mold"),
    SPEC_ARGS5(SUBROUTINE, MVBITS, "mvbits", F90, BIT, 5U, 5U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 31U, "from", "frompos", "len",
               "to", "topos"),
    SPEC_ARGS1(FUNCTION, PRECISION, "precision", F90, MODEL | ASSUMED_SIZE, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "x"),
    SPEC_ARGS3(FUNCTION, PACK, "pack", F90, TRANSFORM, 2U, 3U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 3U, "array", "mask", "vector"),
    SPEC_ARGS1(FUNCTION, PRESENT, "present", F90, ASSUMED_SIZE, 1U, 1U, F2C_INTRINSIC_TYPE_LOGICAL,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 1U, "a"),
    SPEC_ARGS3(FUNCTION, PRODUCT, "product", F90, REDUCTION | TRANSFORM, 1U, 3U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_REDUCTION, F2C_INTRINSIC_KIND_FIRST, 1U,
               "array", "dim", "mask"),
    SPEC_ARGS1(SUBROUTINE, RANDOM_NUMBER, "random_number", F90, NONE, 1U, 1U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 1U,
               "harvest"),
    SPEC_ARGS3(SUBROUTINE, RANDOM_SEED, "random_seed", F90, NONE, 0U, 3U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 0U, "size", "put", "get"),
    SPEC_ARGS1(FUNCTION, RADIX, "radix", F90, MODEL | ASSUMED_SIZE, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "x"),
    SPEC_ARGS1(FUNCTION, RANGE, "range", F90, MODEL | ASSUMED_SIZE, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "x"),
    SPEC_ARGS2(FUNCTION, REAL, "real", F77, CONVERSION, 1U, 2U, F2C_INTRINSIC_TYPE_REAL,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "a", "kind"),
    SPEC_ARGS2(FUNCTION, REPEAT, "repeat", F90, CHARACTER, 2U, 2U, F2C_INTRINSIC_TYPE_CHARACTER,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 3U, "string", "ncopies"),
    SPEC_ARGS4(FUNCTION, RESHAPE, "reshape", F90, TRANSFORM, 2U, 4U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 3U, "source", "shape", "pad",
               "order"),
    SPEC_ARGS1(FUNCTION, RRSPACING, "rrspacing", F90, REPRESENTATION, 1U, 1U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U,
               "x"),
    SPEC_ARGS2(FUNCTION, SCALE, "scale", F90, REPRESENTATION, 2U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "x", "i"),
    SPEC_ARGS4(FUNCTION, SCAN, "scan", F90, CHARACTER, 2U, 4U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 3U, "string", "set",
               "back", "kind"),
    SPEC_ARGS1(FUNCTION, SELECTED_INT_KIND, "selected_int_kind", F90, MODEL, 1U, 1U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               1U, "r"),
    SPEC_ARGS3(FUNCTION, SELECTED_REAL_KIND, "selected_real_kind", F90, MODEL, 1U, 3U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT,
               0U, "p", "r", "radix"),
    SPEC_ARGS2(FUNCTION, SET_EXPONENT, "set_exponent", F90, REPRESENTATION, 2U, 2U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U,
               "x", "i"),
    SPEC_ARGS2(FUNCTION, SIGN, "sign", F77, NUMERIC, 2U, 2U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 3U, "a", "b"),
    SPEC_ARGS2(FUNCTION, SHAPE, "shape", F90, ARRAY_INQUIRY, 1U, 2U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_OPTIONAL, 1U, "source", "kind"),
    SPEC_ARGS1(FUNCTION, SIN, "sin", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS1(FUNCTION, SINH, "sinh", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS1(FUNCTION, SPACING, "spacing", F90, REPRESENTATION, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS3(FUNCTION, SPREAD, "spread", F90, TRANSFORM, 3U, 3U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 7U, "source", "dim", "ncopies"),
    SPEC_ARGS1(FUNCTION, SQRT, "sqrt", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS3(FUNCTION, SIZE, "size", F90, ARRAY_INQUIRY | ASSUMED_SIZE, 1U, 3U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_OPTIONAL,
               1U, "array", "dim", "kind"),
    SPEC_ARGS3(FUNCTION, SUM, "sum", F90, REDUCTION | TRANSFORM, 1U, 3U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_REDUCTION, F2C_INTRINSIC_KIND_FIRST, 1U, "array", "dim", "mask"),
    SPEC_ARGS3(SUBROUTINE, SYSTEM_CLOCK, "system_clock", F90, NONE, 0U, 3U,
               F2C_INTRINSIC_TYPE_FIRST, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_DEFAULT, 0U,
               "count", "count_rate", "count_max"),
    SPEC_ARGS1(FUNCTION, TAN, "tan", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS1(FUNCTION, TANH, "tanh", F77, MATHEMATICAL, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS1(FUNCTION, TINY, "tiny", F90, MODEL | ASSUMED_SIZE, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 1U, "x"),
    SPEC_ARGS3(FUNCTION, TRANSFER, "transfer", F90, NONE, 2U, 3U, F2C_INTRINSIC_TYPE_MOLD,
               F2C_INTRINSIC_RANK_MOLD, F2C_INTRINSIC_KIND_DEFAULT, 3U, "source", "mold", "size"),
    SPEC_ARGS1(FUNCTION, TRIM, "trim", F90, CHARACTER, 1U, 1U, F2C_INTRINSIC_TYPE_CHARACTER,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 1U, "string"),
    SPEC_ARGS1(FUNCTION, TRANSPOSE, "transpose", F90, TRANSFORM, 1U, 1U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 1U, "matrix"),
    SPEC_ARGS3(FUNCTION, UBOUND, "ubound", F90, ARRAY_INQUIRY | ASSUMED_SIZE, 1U, 3U,
               F2C_INTRINSIC_TYPE_INTEGER, F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_OPTIONAL,
               1U, "array", "dim", "kind"),
    SPEC_ARGS3(FUNCTION, UNPACK, "unpack", F90, TRANSFORM, 3U, 3U, F2C_INTRINSIC_TYPE_FIRST,
               F2C_INTRINSIC_RANK_SCALAR, F2C_INTRINSIC_KIND_FIRST, 7U, "vector", "mask", "field"),
    SPEC_ARGS4(FUNCTION, VERIFY, "verify", F90, CHARACTER, 2U, 4U, F2C_INTRINSIC_TYPE_INTEGER,
               F2C_INTRINSIC_RANK_ELEMENTAL, F2C_INTRINSIC_KIND_OPTIONAL, 3U, "string", "set",
               "back", "kind"),
};

const F2cIntrinsicSpecification *f2c_intrinsic_specification(F2cIntrinsicId intrinsic) {
    const F2cIntrinsicSpecification *specification;
    if (intrinsic <= F2C_INTRINSIC_NONE || intrinsic >= F2C_INTRINSIC_ID_COUNT)
        return NULL;
    specification = &specifications[intrinsic];
    return specification->descriptor.id == intrinsic &&
                   specification->descriptor.canonical_name != NULL
               ? specification
               : NULL;
}

const F2cIntrinsicSpecification *f2c_find_intrinsic_specification(const char *name) {
    F2cIntrinsicId intrinsic;
    if (name == NULL)
        return NULL;
    for (intrinsic = (F2cIntrinsicId)(F2C_INTRINSIC_NONE + 1); intrinsic < F2C_INTRINSIC_ID_COUNT;
         intrinsic = (F2cIntrinsicId)(intrinsic + 1)) {
        const F2cIntrinsicSpecification *specification = f2c_intrinsic_specification(intrinsic);
        if (specification != NULL && strcmp(specification->descriptor.canonical_name, name) == 0)
            return specification;
    }
    return NULL;
}

const F2cIntrinsicSignature *f2c_intrinsic_canonical_signature(F2cIntrinsicId intrinsic) {
    const F2cIntrinsicSpecification *specification = f2c_intrinsic_specification(intrinsic);
    return specification != NULL &&
                   specification->descriptor.procedure_kind == F2C_INTRINSIC_PROCEDURE_FUNCTION
               ? &specification->signature
               : NULL;
}

const F2cIntrinsicDescriptor *f2c_intrinsic_descriptor(F2cIntrinsicId intrinsic) {
    const F2cIntrinsicSpecification *specification = f2c_intrinsic_specification(intrinsic);
    return specification != NULL ? &specification->descriptor : NULL;
}

const F2cIntrinsicArgumentSchema *f2c_intrinsic_argument_schema(F2cIntrinsicId intrinsic) {
    const F2cIntrinsicSpecification *specification = f2c_intrinsic_specification(intrinsic);
    return specification != NULL ? &specification->arguments : NULL;
}

const F2cIntrinsicDescriptor *f2c_find_intrinsic_descriptor(const char *name) {
    const F2cIntrinsicSpecification *specification = f2c_find_intrinsic_specification(name);
    return specification != NULL ? &specification->descriptor : NULL;
}

int f2c_intrinsic_has_family(F2cIntrinsicId intrinsic, F2cIntrinsicFamily family) {
    const F2cIntrinsicDescriptor *descriptor = f2c_intrinsic_descriptor(intrinsic);
    return descriptor != NULL && family != F2C_INTRINSIC_FAMILY_NONE &&
           (descriptor->families & (unsigned int)family) != 0U;
}

#undef SPEC_VARARGS2
#undef SPEC_ARGS6
#undef SPEC_ARGS5
#undef SPEC_ARGS4
#undef SPEC_ARGS3
#undef SPEC_ARGS2
#undef SPEC_ARGS1
#undef SPEC
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
#undef F2C_INTRINSIC_NULL_ID
#undef F2008
#undef F95
#undef F90
#undef F77
