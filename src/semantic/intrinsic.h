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
    F2C_INTRINSIC_RANK_MOLD
} F2cIntrinsicRankRule;

typedef enum F2cIntrinsicKindRule {
    F2C_INTRINSIC_KIND_DEFAULT,
    F2C_INTRINSIC_KIND_FIRST,
    F2C_INTRINSIC_KIND_OPTIONAL
} F2cIntrinsicKindRule;

typedef struct F2cIntrinsicSignature {
    const char *name;
    size_t minimum_arguments;
    size_t maximum_arguments;
    F2cIntrinsicTypeRule type_rule;
    F2cIntrinsicRankRule rank_rule;
    F2cIntrinsicId id;
    F2cIntrinsicKindRule kind_rule;
} F2cIntrinsicSignature;

const F2cIntrinsicSignature *f2c_find_intrinsic(const char *name);
int f2c_is_intrinsic_name(const char *name);
int f2c_is_intrinsic_subroutine(const char *name);
int f2c_intrinsic_is_bit(F2cIntrinsicId intrinsic);
int f2c_intrinsic_is_character(F2cIntrinsicId intrinsic);
const F2cExpr *f2c_intrinsic_argument(F2cExpr *const *arguments, size_t count, const char *keyword,
                                      size_t position);
Type f2c_resolve_intrinsic_type(const char *name, const Type *arguments, size_t count);
size_t f2c_resolve_intrinsic_rank(const char *name, F2cExpr *const *arguments, size_t count);
int f2c_resolve_intrinsic_kind(const char *name, F2cExpr *const *arguments, size_t count);

#endif
