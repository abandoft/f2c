#include "semantic/constant/private.h"

#include "internal/f2c.h"
#include "semantic/numeric_model.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int complex_type(Type type) { return type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX; }

static int numeric_type(Type type) {
    return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE || complex_type(type);
}

int f2c_constant_value_convert(F2cConstantValue *value, F2cScalarType type,
                               const F2cDerivedType *derived, size_t character_length) {
    const Type source = value->type.type;
    if (type.kind == 0)
        type.kind = f2c_default_kind(type.type);
    if (type.type == TYPE_DERIVED) {
        if (source != TYPE_DERIVED || value->payload.derived == NULL ||
            value->payload.derived->derived_type != derived)
            return 0;
    } else if (type.type == TYPE_CHARACTER) {
        if (source != TYPE_CHARACTER || value->type.kind != type.kind || type.kind != 1 ||
            character_length == SIZE_MAX)
            return 0;
        char *bytes = (char *)malloc(character_length + 1U);
        if (bytes == NULL)
            return 0;
        const size_t copied = value->payload.character.length < character_length
                                  ? value->payload.character.length
                                  : character_length;
        if (copied != 0U)
            memcpy(bytes, value->payload.character.bytes, copied);
        if (copied < character_length)
            memset(bytes + copied, ' ', character_length - copied);
        bytes[character_length] = '\0';
        free(value->payload.character.bytes);
        value->payload.character.bytes = bytes;
        value->payload.character.length = character_length;
    } else if (type.type == TYPE_LOGICAL) {
        if (source != TYPE_LOGICAL ||
            (type.kind != 1 && type.kind != 2 && type.kind != 4 && type.kind != 8))
            return 0;
        value->payload.integer = value->payload.integer != 0;
    } else if (type.type == TYPE_INTEGER) {
        const F2cNumericModel *model = f2c_numeric_model(TYPE_INTEGER, type.kind);
        int64_t integer;
        if (model == NULL || !numeric_type(source))
            return 0;
        if (source == TYPE_INTEGER)
            integer = value->payload.integer;
        else {
            const double real = trunc(value->payload.number.real);
            const double limit = ldexp(1.0, type.kind * 8 - 1);
            if (!isfinite(real) || real < -limit || real >= limit)
                return 0;
            integer = (int64_t)real;
        }
        if (integer < -model->integer_huge - 1 || integer > model->integer_huge)
            return 0;
        value->payload.integer = integer;
    } else if (type.type == TYPE_REAL || type.type == TYPE_DOUBLE || complex_type(type.type)) {
        if (!numeric_type(source) || (type.kind != 4 && type.kind != 8))
            return 0;
        double real = source == TYPE_INTEGER
                          ? (type.kind == 4 ? (double)(float)value->payload.integer
                                            : (double)value->payload.integer)
                          : value->payload.number.real;
        double imaginary = complex_type(source) ? value->payload.number.imaginary : 0.0;
        if (type.kind == 4) {
            if ((isfinite(real) && fabs(real) > FLT_MAX) ||
                (complex_type(type.type) && isfinite(imaginary) && fabs(imaginary) > FLT_MAX))
                return 0;
            real = (double)(float)real;
            if (complex_type(type.type))
                imaginary = (double)(float)imaginary;
        }
        value->payload.number.real = real;
        value->payload.number.imaginary = complex_type(type.type) ? imaginary : 0.0;
    } else {
        return 0;
    }
    value->type = type;
    return 1;
}

int f2c_constant_evaluate_value(F2cConstantEvaluation *evaluation, const F2cExpr *expression,
                                F2cConstantValue *result, size_t depth) {
    F2cConstantValue value = {0};
    if (expression == NULL || expression->rank != 0U ||
        !f2c_constant_consume_step(evaluation, depth))
        return 0;
    value.type = (F2cScalarType){expression->type, expression->type_kind != 0
                                                       ? expression->type_kind
                                                       : f2c_default_kind(expression->type)};
    if (expression->kind == F2C_EXPR_ARRAY_REFERENCE) {
        const F2cConstantValue *element = f2c_constant_array_element(evaluation, expression, depth);
        return element != NULL && f2c_constant_value_copy(result, element);
    }
    switch (expression->type) {
    case TYPE_INTEGER:
    case TYPE_LOGICAL:
        if (!f2c_constant_evaluate_integer(evaluation, expression, &value.payload.integer,
                                           depth + 1U))
            return 0;
        break;
    case TYPE_REAL:
    case TYPE_DOUBLE:
        if (!f2c_constant_evaluate_real(evaluation, expression, &value.payload.number.real,
                                        depth + 1U))
            return 0;
        break;
    case TYPE_COMPLEX:
    case TYPE_DOUBLE_COMPLEX: {
        F2cComplexConstant number;
        if (!f2c_constant_evaluate_complex(evaluation, expression, &number, depth + 1U))
            return 0;
        value.payload.number.real = number.real;
        value.payload.number.imaginary = number.imaginary;
        break;
    }
    case TYPE_CHARACTER:
        if (value.type.kind != 1 ||
            !f2c_constant_evaluate_character(evaluation, expression, &value.payload.character.bytes,
                                             &value.payload.character.length, depth + 1U))
            return 0;
        break;
    case TYPE_DERIVED:
        expression = f2c_expr_value_source(expression);
        if (expression->kind == F2C_EXPR_NAME && expression->symbol != NULL &&
            expression->symbol->parameter)
            return f2c_constant_evaluate_value(
                evaluation, expression->symbol->initializer_expression, result, depth + 1U);
        if (expression->kind != F2C_EXPR_STRUCTURE_CONSTRUCTOR ||
            !f2c_expression_is_initialization_constant(expression))
            return 0;
        value.payload.derived = f2c_expr_clone_substitute_integers(expression, NULL, 0U);
        if (value.payload.derived == NULL)
            return 0;
        break;
    case TYPE_UNKNOWN:
    default:
        return 0;
    }
    if (!f2c_constant_value_convert(
            &value, value.type, expression->derived_type,
            value.type.type == TYPE_CHARACTER ? value.payload.character.length : 0U)) {
        f2c_constant_value_free(&value);
        return 0;
    }
    *result = value;
    return 1;
}
