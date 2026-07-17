#include "internal/f2c.h"

#include <string.h>

int f2c_symbol_uses_descriptor(const Symbol *symbol) {
    size_t dimension;
    if (symbol == NULL)
        return 0;
    if (symbol->allocatable || symbol->pointer)
        return 1;
    if (!symbol->argument)
        return 0;
    for (dimension = 0U; dimension < symbol->rank; ++dimension)
        if (symbol->dimensions[dimension].kind == F2C_DIMENSION_ASSUMED_SHAPE)
            return 1;
    return 0;
}

const char *f2c_c_type(Type type) {
    switch (type) {
    case TYPE_INTEGER:
        return "int32_t";
    case TYPE_REAL:
        return "float";
    case TYPE_DOUBLE:
        return "double";
    case TYPE_COMPLEX:
        return "f2c_complex_float";
    case TYPE_DOUBLE_COMPLEX:
        return "f2c_complex_double";
    case TYPE_LOGICAL:
        return "bool";
    case TYPE_CHARACTER:
        return "char";
    case TYPE_DERIVED:
        return "void";
    default:
        return "float";
    }
}

int f2c_default_kind(Type type) {
    switch (type) {
    case TYPE_DOUBLE:
    case TYPE_DOUBLE_COMPLEX:
        return 8;
    case TYPE_CHARACTER:
        return 1;
    case TYPE_INTEGER:
    case TYPE_REAL:
    case TYPE_COMPLEX:
    case TYPE_LOGICAL:
        return 4;
    case TYPE_UNKNOWN:
    case TYPE_DERIVED:
    default:
        return 0;
    }
}

const char *f2c_c_type_kind(Type type, int kind) {
    const int resolved = kind > 0 ? kind : f2c_default_kind(type);
    if (type == TYPE_INTEGER || type == TYPE_LOGICAL) {
        if (type == TYPE_LOGICAL && resolved == 1)
            return "bool";
        switch (resolved) {
        case 1:
            return "int8_t";
        case 2:
            return "int16_t";
        case 8:
            return "int64_t";
        case 4:
        default:
            return "int32_t";
        }
    }
    if (type == TYPE_REAL || type == TYPE_DOUBLE) {
        if (resolved == 4)
            return "float";
        if (resolved == 8)
            return "double";
        return "long double";
    }
    if (type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX) {
        if (resolved == 4)
            return "f2c_complex_float";
        if (resolved == 8)
            return "f2c_complex_double";
        return "f2c_complex_long_double";
    }
    if (type == TYPE_CHARACTER)
        return resolved == 1 ? "char" : "uint32_t";
    return f2c_c_type(type);
}

const char *f2c_symbol_c_type(const Symbol *symbol) {
    return symbol != NULL && symbol->c_type != NULL
               ? symbol->c_type
               : (symbol != NULL ? f2c_c_type_kind(symbol->type, symbol->kind)
                                 : f2c_c_type(TYPE_REAL));
}

const char *f2c_expression_c_type(const F2cExpr *expression) {
    if (expression != NULL && expression->symbol != NULL && expression->symbol->c_type != NULL)
        return expression->symbol->c_type;
    if (expression != NULL && expression->derived_type != NULL &&
        expression->derived_type->c_name != NULL)
        return expression->derived_type->c_name;
    return expression != NULL ? f2c_c_type_kind(expression->type, expression->type_kind)
                              : f2c_c_type(TYPE_REAL);
}

static F2cShapeKind shape_kind_for_dimension(F2cDimensionKind kind) {
    switch (kind) {
    case F2C_DIMENSION_ASSUMED_SIZE:
        return F2C_SHAPE_ASSUMED_SIZE;
    case F2C_DIMENSION_ASSUMED_SHAPE:
        return F2C_SHAPE_ASSUMED_SHAPE;
    case F2C_DIMENSION_DEFERRED:
        return F2C_SHAPE_DEFERRED;
    case F2C_DIMENSION_EXPLICIT:
    default:
        return F2C_SHAPE_EXPLICIT;
    }
}

void f2c_shape_from_symbol(Unit *unit, F2cShape *shape, const Symbol *symbol) {
    size_t dimension;
    if (shape == NULL)
        return;
    memset(shape, 0, sizeof(*shape));
    if (symbol == NULL || symbol->rank == 0U) {
        shape->kind = F2C_SHAPE_SCALAR;
        return;
    }
    shape->rank = symbol->rank <= F2C_MAX_RANK ? symbol->rank : F2C_MAX_RANK;
    shape->kind = F2C_SHAPE_EXPLICIT;
    for (dimension = 0U; dimension < shape->rank; ++dimension) {
        const Dimension *source = &symbol->dimensions[dimension];
        F2cShapeDimension *target = &shape->dimensions[dimension];
        int64_t lower = 0;
        int64_t upper = 0;
        const F2cShapeKind dimension_shape = shape_kind_for_dimension(source->kind);
        target->kind = source->kind;
        if (dimension_shape > shape->kind)
            shape->kind = dimension_shape;
        target->lower_known =
            source->lower_expression != NULL
                ? f2c_evaluate_integer_constant(unit, source->lower_expression, &lower)
                : (source->lower != NULL && f2c_evaluate_integer_text(unit, source->lower, &lower));
        if (target->lower_known)
            target->lower = lower;
        if (source->kind == F2C_DIMENSION_EXPLICIT &&
            (source->upper_expression != NULL
                 ? f2c_evaluate_integer_constant(unit, source->upper_expression, &upper)
                 : (source->upper != NULL &&
                    f2c_evaluate_integer_text(unit, source->upper, &upper))) &&
            target->lower_known) {
            target->extent_known = 1;
            target->extent = upper >= lower ? (uint64_t)upper - (uint64_t)lower + UINT64_C(1) : 0U;
        }
    }
}

int f2c_type_is_numeric(Type type) {
    return type == TYPE_INTEGER || type == TYPE_REAL || type == TYPE_DOUBLE ||
           type == TYPE_COMPLEX || type == TYPE_DOUBLE_COMPLEX;
}

Type f2c_common_numeric_type(Type left, Type right) {
    if (left == TYPE_DOUBLE_COMPLEX || right == TYPE_DOUBLE_COMPLEX)
        return TYPE_DOUBLE_COMPLEX;
    if (left == TYPE_COMPLEX || right == TYPE_COMPLEX)
        return left == TYPE_DOUBLE || right == TYPE_DOUBLE ? TYPE_DOUBLE_COMPLEX : TYPE_COMPLEX;
    if (left == TYPE_DOUBLE || right == TYPE_DOUBLE)
        return TYPE_DOUBLE;
    if (left == TYPE_REAL || right == TYPE_REAL)
        return TYPE_REAL;
    if (left == TYPE_INTEGER && right == TYPE_INTEGER)
        return TYPE_INTEGER;
    return left != TYPE_UNKNOWN ? left : right;
}
