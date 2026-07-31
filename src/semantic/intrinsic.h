#ifndef F2C_SEMANTIC_INTRINSIC_H
#define F2C_SEMANTIC_INTRINSIC_H

#include "ir/expression.h"

typedef enum F2cIntrinsicTypeRule {
    F2C_INTRINSIC_TYPE_FIRST,
    F2C_INTRINSIC_TYPE_COMMON,
    F2C_INTRINSIC_TYPE_ABSOLUTE,
    F2C_INTRINSIC_TYPE_DOUBLE,
    F2C_INTRINSIC_TYPE_REAL,
    F2C_INTRINSIC_TYPE_INTEGER,
    F2C_INTRINSIC_TYPE_COMPLEX,
    F2C_INTRINSIC_TYPE_DOUBLE_COMPLEX,
    F2C_INTRINSIC_TYPE_CHARACTER,
    F2C_INTRINSIC_TYPE_LOGICAL,
    F2C_INTRINSIC_TYPE_MOLD
} F2cIntrinsicTypeRule;

typedef enum F2cIntrinsicRankRule {
    F2C_INTRINSIC_RANK_SCALAR,
    F2C_INTRINSIC_RANK_ELEMENTAL,
    F2C_INTRINSIC_RANK_FIRST,
    F2C_INTRINSIC_RANK_MOLD,
    F2C_INTRINSIC_RANK_REDUCTION,
    F2C_INTRINSIC_RANK_LOCATION
} F2cIntrinsicRankRule;

typedef enum F2cIntrinsicKindRule {
    F2C_INTRINSIC_KIND_DEFAULT,
    F2C_INTRINSIC_KIND_FIRST,
    F2C_INTRINSIC_KIND_COMMON,
    F2C_INTRINSIC_KIND_OPTIONAL,
    F2C_INTRINSIC_KIND_FIRST_OPTIONAL
} F2cIntrinsicKindRule;

typedef enum F2cIntrinsicStandard {
    F2C_INTRINSIC_STANDARD_FORTRAN_77,
    F2C_INTRINSIC_STANDARD_FORTRAN_90,
    F2C_INTRINSIC_STANDARD_FORTRAN_95,
    F2C_INTRINSIC_STANDARD_FORTRAN_2008,
    F2C_INTRINSIC_STANDARD_EXTENSION
} F2cIntrinsicStandard;

typedef enum F2cIntrinsicProcedureKind {
    F2C_INTRINSIC_PROCEDURE_FUNCTION,
    F2C_INTRINSIC_PROCEDURE_SUBROUTINE
} F2cIntrinsicProcedureKind;

typedef enum F2cIntrinsicFamily {
    F2C_INTRINSIC_FAMILY_NONE = 0U,
    F2C_INTRINSIC_FAMILY_BIT = 1U << 0,
    F2C_INTRINSIC_FAMILY_CHARACTER = 1U << 1,
    F2C_INTRINSIC_FAMILY_CONVERSION = 1U << 2,
    F2C_INTRINSIC_FAMILY_MATHEMATICAL = 1U << 3,
    F2C_INTRINSIC_FAMILY_NUMERIC_MODEL = 1U << 4,
    F2C_INTRINSIC_FAMILY_NUMERIC_OPERATION = 1U << 5,
    F2C_INTRINSIC_FAMILY_REAL_REPRESENTATION = 1U << 6,
    F2C_INTRINSIC_FAMILY_REDUCTION = 1U << 7,
    F2C_INTRINSIC_FAMILY_TRANSFORMATIONAL = 1U << 8,
    F2C_INTRINSIC_FAMILY_ARRAY_INQUIRY = 1U << 9,
    F2C_INTRINSIC_FAMILY_ASSUMED_SIZE_INQUIRY = 1U << 10
} F2cIntrinsicFamily;

typedef struct F2cIntrinsicDescriptor {
    F2cIntrinsicId id;
    const char *canonical_name;
    F2cIntrinsicStandard standard;
    F2cIntrinsicProcedureKind procedure_kind;
    unsigned int families;
} F2cIntrinsicDescriptor;

typedef struct F2cIntrinsicSignature {
    const char *name;
    size_t minimum_arguments;
    size_t maximum_arguments;
    F2cIntrinsicTypeRule type_rule;
    F2cIntrinsicRankRule rank_rule;
    F2cIntrinsicId id;
    F2cIntrinsicKindRule kind_rule;
} F2cIntrinsicSignature;

const F2cIntrinsicDescriptor *f2c_intrinsic_descriptor(F2cIntrinsicId intrinsic);
const F2cIntrinsicDescriptor *f2c_find_intrinsic_descriptor(const char *name);
int f2c_intrinsic_has_family(F2cIntrinsicId intrinsic, F2cIntrinsicFamily family);
size_t f2c_intrinsic_signature_count(void);
const F2cIntrinsicSignature *f2c_intrinsic_signature_at(size_t index);
const F2cIntrinsicSignature *f2c_find_intrinsic(const char *name);
int f2c_is_intrinsic_name(const char *name);
int f2c_is_intrinsic_subroutine(const char *name);
int f2c_intrinsic_is_bit(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_character(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_conversion(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_mathematical(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_numeric_model(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_numeric_operation(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_real_representation(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_reduction(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_transformational(F2cIntrinsicId intrinsic);
const F2cExpr *f2c_intrinsic_argument(F2cExpr *const *arguments, size_t count, const char *keyword,
                                      size_t position);
Type f2c_resolve_intrinsic_type(const char *name, const Type *arguments, size_t count);
size_t f2c_resolve_intrinsic_rank(const char *name, F2cExpr *const *arguments, size_t count);
int f2c_resolve_intrinsic_kind(const char *name, F2cExpr *const *arguments, size_t count);

#endif
