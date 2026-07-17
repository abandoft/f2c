#ifndef F2C_IR_TYPE_H
#define F2C_IR_TYPE_H

#include "internal/base.h"

typedef enum Type {
    TYPE_UNKNOWN,
    TYPE_INTEGER,
    TYPE_REAL,
    TYPE_DOUBLE,
    TYPE_COMPLEX,
    TYPE_DOUBLE_COMPLEX,
    TYPE_LOGICAL,
    TYPE_CHARACTER,
    TYPE_DERIVED
} Type;

#define F2C_MAX_RANK 15U

typedef enum F2cValueCategory {
    F2C_VALUE_INVALID,
    F2C_VALUE_CONSTANT,
    F2C_VALUE_VARIABLE,
    F2C_VALUE_TEMPORARY,
    F2C_VALUE_PROCEDURE,
    F2C_VALUE_TYPE
} F2cValueCategory;

typedef enum F2cDimensionKind {
    F2C_DIMENSION_EXPLICIT,
    F2C_DIMENSION_ASSUMED_SIZE,
    F2C_DIMENSION_ASSUMED_SHAPE,
    F2C_DIMENSION_DEFERRED
} F2cDimensionKind;

typedef enum F2cShapeKind {
    F2C_SHAPE_SCALAR,
    F2C_SHAPE_EXPLICIT,
    F2C_SHAPE_ASSUMED_SIZE,
    F2C_SHAPE_ASSUMED_SHAPE,
    F2C_SHAPE_DEFERRED,
    F2C_SHAPE_EXPRESSION,
    F2C_SHAPE_UNKNOWN
} F2cShapeKind;

typedef struct F2cShapeDimension {
    F2cDimensionKind kind;
    int lower_known;
    int extent_known;
    int64_t lower;
    uint64_t extent;
} F2cShapeDimension;

typedef struct F2cShape {
    F2cShapeKind kind;
    size_t rank;
    F2cShapeDimension dimensions[F2C_MAX_RANK];
} F2cShape;

typedef struct F2cExpr F2cExpr;

typedef struct Dimension {
    F2cDimensionKind kind;
    char *lower;
    char *upper;
    F2cExpr *lower_expression;
    F2cExpr *upper_expression;
} Dimension;

typedef enum F2cIntent {
    F2C_INTENT_UNSPECIFIED,
    F2C_INTENT_IN,
    F2C_INTENT_OUT,
    F2C_INTENT_INOUT
} F2cIntent;

typedef struct Unit Unit;
typedef struct Symbol Symbol;

const char *f2c_c_type(Type type);
const char *f2c_c_type_kind(Type type, int kind);
const char *f2c_symbol_c_type(const Symbol *symbol);
const char *f2c_expression_c_type(const F2cExpr *expression);
int f2c_default_kind(Type type);
void f2c_shape_from_symbol(Unit *unit, F2cShape *shape, const Symbol *symbol);
int f2c_type_is_numeric(Type type);
Type f2c_common_numeric_type(Type left, Type right);

#endif
