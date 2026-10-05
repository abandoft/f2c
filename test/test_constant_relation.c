#include "semantic/constant/relation.h"

#include <math.h>
#include <stdio.h>

static int failures;

static void relation(F2cOperator operation, F2cConstantValue left, F2cConstantValue right,
                      int64_t expected, const char *label) {
    int64_t result = -1;
    if (!f2c_constant_value_relation(operation, &left, &right, &result) || result != expected) {
        fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

static void rejected(F2cOperator operation, F2cConstantValue left, F2cConstantValue right) {
    int64_t result = 17;
    if (f2c_constant_value_relation(operation, &left, &right, &result) || result != 17) {
        fprintf(stderr, "FAIL: invalid relation changed its output\n");
        ++failures;
    }
}

static F2cConstantValue integer(int kind, int64_t payload) {
    F2cConstantValue value = {0};
    value.type = (F2cScalarType){TYPE_INTEGER, kind};
    value.payload.integer = payload;
    return value;
}

static F2cConstantValue number(Type type, int kind, double real, double imaginary) {
    F2cConstantValue value = {0};
    value.type = (F2cScalarType){type, kind};
    value.payload.number.real = real;
    value.payload.number.imaginary = imaginary;
    return value;
}

int main(void) {
    const F2cConstantValue narrow = integer(1, 2), wide = integer(8, 2);
    relation(F2C_OPERATOR_EQUAL, narrow, wide, 1, "mixed integer kinds");
    relation(F2C_OPERATOR_LESS, integer(8, INT64_MIN), integer(8, INT64_MAX), 1,
             "exact full-width integer ordering");
    relation(F2C_OPERATOR_NOT_EQUAL, integer(8, INT64_C(9007199254740992)),
             integer(8, INT64_C(9007199254740993)), 1, "integers are not converted through double");
    relation(F2C_OPERATOR_EQUAL, integer(8, INT64_C(16777217)),
             number(TYPE_REAL, 4, 16777216.0, 0.0), 1, "integer to selected REAL(4) model");
    relation(F2C_OPERATOR_EQUAL, integer(8, INT64_C(16777217)),
             number(TYPE_DOUBLE, 8, 16777216.0, 0.0), 0, "integer to selected REAL(8) model");
    relation(F2C_OPERATOR_EQUAL, integer(8, INT64_C(4611686293305294849)),
             number(TYPE_REAL, 4, 4611686568183201792.0, 0.0), 1,
             "INTEGER(8) to REAL(4) has no intermediate double rounding");
    relation(F2C_OPERATOR_EQUAL, number(TYPE_COMPLEX, 4, 2.0, 0.0),
             number(TYPE_DOUBLE, 8, 2.0, 0.0), 1, "mixed real-complex equality");
    relation(F2C_OPERATOR_NOT_EQUAL, number(TYPE_COMPLEX, 4, 2.0, 1.0), integer(8, 2), 1,
             "nonzero imaginary component participates in equality");
    relation(F2C_OPERATOR_EQUAL, number(TYPE_REAL, 4, -0.0, 0.0),
             number(TYPE_DOUBLE, 8, 0.0, 0.0), 1, "signed zero values compare equal");
    relation(F2C_OPERATOR_EQUAL, number(TYPE_DOUBLE, 8, INFINITY, 0.0),
             number(TYPE_DOUBLE, 8, INFINITY, 0.0), 1, "infinite equality");
    relation(F2C_OPERATOR_NOT_EQUAL, number(TYPE_REAL, 4, NAN, 0.0), integer(8, 2), 1,
             "NaN inequality");
    relation(F2C_OPERATOR_LESS_EQUAL, number(TYPE_DOUBLE, 8, NAN, 0.0), integer(4, 2), 0,
             "NaN is unordered");
    F2cConstantValue a = integer(1, -1), b = integer(8, 2);
    a.type.type = b.type.type = TYPE_LOGICAL;
    relation(F2C_OPERATOR_EQUIVALENT, a, b, 1, "logical truth, not raw ABI payload equality");
    relation(F2C_OPERATOR_NOT_EQUIVALENT, a, b, 0, "logical nonequivalence");
    a.type = b.type = (F2cScalarType){TYPE_CHARACTER, 1};
    char first[] = {'A', '\0'}, second[] = {'A', '\0', ' '};
    a.payload.character.bytes = first;
    a.payload.character.length = sizeof(first);
    b.payload.character.bytes = second;
    b.payload.character.length = sizeof(second);
    relation(F2C_OPERATOR_EQUAL, a, b, 1, "blank padding preserves embedded NUL");
    b.payload.character.length = 1U;
    relation(F2C_OPERATOR_LESS, a, b, 1, "character ordering uses padded byte values");
    b.type.kind = 4;
    rejected(F2C_OPERATOR_EQUAL, a, b);
    rejected(F2C_OPERATOR_EQUAL, wide, a);
    rejected(F2C_OPERATOR_LESS, number(TYPE_COMPLEX, 4, 2.0, 0.0), wide);
    rejected(F2C_OPERATOR_EQUAL, number(TYPE_REAL, 16, 2.0, 0.0), wide);
    if (failures == 0)
        puts("typed constant relational model contracts passed");
    return failures == 0 ? 0 : 1;
}
