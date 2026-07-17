#include "ast/internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void f2c_ast_set_expression_shape(F2cExpr *expression, size_t rank, F2cShapeKind kind);
void f2c_ast_copy_expression_shape(F2cExpr *expression, const F2cShape *source);

const F2cExpr *f2c_ast_intrinsic_argument_value(const F2cExpr *argument) {
    return argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
                   argument->child_count == 1U
               ? argument->children[0]
               : argument;
}

static const F2cExpr *intrinsic_named_argument(const F2cExpr *call, const char *keyword,
                                               size_t position) {
    size_t positional = 0U;
    size_t i;
    if (call == NULL)
        return NULL;
    for (i = 0U; i < call->child_count; ++i) {
        const F2cExpr *argument = call->children[i];
        if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT) {
            if (argument->text != NULL && strcmp(argument->text, keyword) == 0)
                return f2c_ast_intrinsic_argument_value(argument);
        } else if (positional++ == position) {
            return argument;
        }
    }
    return NULL;
}

void f2c_ast_set_transform_intrinsic_shape(AstParser *parser, F2cExpr *expression) {
    const char *name = expression->text;
    const F2cExpr *source;
    const F2cExpr *shape;
    const F2cExpr *dimension;
    int64_t dimension_value;
    size_t result_dimension;
    size_t source_dimension;
    if (strcmp(name, "pack") == 0) {
        const F2cExpr *vector = intrinsic_named_argument(expression, "vector", 2U);
        f2c_ast_set_expression_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
        if (vector != NULL && vector->rank == 1U)
            expression->shape.dimensions[0] = vector->shape.dimensions[0];
        return;
    }
    if (strcmp(name, "unpack") == 0) {
        const F2cExpr *mask = intrinsic_named_argument(expression, "mask", 1U);
        if (mask != NULL)
            f2c_ast_copy_expression_shape(expression, &mask->shape);
        return;
    }
    if (strcmp(name, "reshape") == 0) {
        shape = intrinsic_named_argument(expression, "shape", 1U);
        f2c_ast_set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        if (shape != NULL && shape->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
            for (result_dimension = 0U;
                 result_dimension < expression->rank && result_dimension < shape->child_count;
                 ++result_dimension) {
                int64_t extent;
                if (f2c_evaluate_integer_constant(parser->unit, shape->children[result_dimension],
                                                  &extent) &&
                    extent >= 0) {
                    expression->shape.dimensions[result_dimension].extent_known = 1;
                    expression->shape.dimensions[result_dimension].extent = (uint64_t)extent;
                }
            }
        }
        return;
    }
    if (strcmp(name, "spread") == 0) {
        source = intrinsic_named_argument(expression, "source", 0U);
        dimension = intrinsic_named_argument(expression, "dim", 1U);
        f2c_ast_set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        if (source == NULL || dimension == NULL ||
            !f2c_evaluate_integer_constant(parser->unit, dimension, &dimension_value) ||
            dimension_value < 1 || (uint64_t)dimension_value > expression->rank)
            return;
        source_dimension = 0U;
        for (result_dimension = 0U; result_dimension < expression->rank; ++result_dimension) {
            if (result_dimension + 1U == (size_t)dimension_value)
                continue;
            if (source_dimension < source->rank)
                expression->shape.dimensions[result_dimension] =
                    source->shape.dimensions[source_dimension++];
        }
        return;
    }
    if (strcmp(name, "findloc") == 0) {
        source = intrinsic_named_argument(expression, "array", 0U);
        dimension = intrinsic_named_argument(expression, "dim", 2U);
        f2c_ast_set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        if (source == NULL)
            return;
        if (dimension == NULL) {
            if (expression->rank == 1U) {
                expression->shape.dimensions[0].extent_known = 1;
                expression->shape.dimensions[0].extent = source->rank;
            }
            return;
        }
        if (!f2c_evaluate_integer_constant(parser->unit, dimension, &dimension_value) ||
            dimension_value < 1 || (uint64_t)dimension_value > source->rank)
            return;
        result_dimension = 0U;
        for (source_dimension = 0U; source_dimension < source->rank; ++source_dimension) {
            if (source_dimension + 1U != (size_t)dimension_value)
                expression->shape.dimensions[result_dimension++] =
                    source->shape.dimensions[source_dimension];
        }
    }
}

void f2c_ast_set_expression_shape(F2cExpr *expression, size_t rank, F2cShapeKind kind) {
    if (expression == NULL)
        return;
    memset(&expression->shape, 0, sizeof(expression->shape));
    expression->rank = rank;
    expression->shape.rank = rank;
    expression->shape.kind = rank == 0U ? F2C_SHAPE_SCALAR : kind;
}

int f2c_ast_common_expression_kind(Type result_type, const F2cExpr *left, const F2cExpr *right) {
    int kind = f2c_default_kind(result_type);
    const int numeric_result = f2c_type_is_numeric(result_type);
    if (left != NULL &&
        (left->type == result_type || (numeric_result && f2c_type_is_numeric(left->type))) &&
        left->type_kind > kind)
        kind = left->type_kind;
    if (right != NULL &&
        (right->type == result_type || (numeric_result && f2c_type_is_numeric(right->type))) &&
        right->type_kind > kind)
        kind = right->type_kind;
    return kind;
}

void f2c_ast_copy_expression_shape(F2cExpr *expression, const F2cShape *source) {
    if (expression == NULL || source == NULL)
        return;
    expression->shape = *source;
    expression->rank = source->rank;
    if (expression->rank != 0U)
        expression->shape.kind = F2C_SHAPE_EXPRESSION;
}

void f2c_ast_set_elemental_shape(F2cExpr *expression, const F2cExpr *left, const F2cExpr *right) {
    const F2cExpr *source = NULL;
    size_t dimension;
    if (expression == NULL)
        return;
    if (left != NULL && left->rank != 0U)
        source = left;
    if (right != NULL && right->rank > (source != NULL ? source->rank : 0U))
        source = right;
    if (source == NULL) {
        f2c_ast_set_expression_shape(expression, 0U, F2C_SHAPE_SCALAR);
        return;
    }
    f2c_ast_copy_expression_shape(expression, &source->shape);
    if (left == NULL || right == NULL || left->rank == 0U || right->rank == 0U ||
        left->rank != right->rank)
        return;
    for (dimension = 0U; dimension < expression->rank; ++dimension) {
        const F2cShapeDimension *left_dimension = &left->shape.dimensions[dimension];
        const F2cShapeDimension *right_dimension = &right->shape.dimensions[dimension];
        F2cShapeDimension *result_dimension = &expression->shape.dimensions[dimension];
        if (left_dimension->extent_known && right_dimension->extent_known &&
            left_dimension->extent != right_dimension->extent) {
            result_dimension->extent_known = 0;
        } else if (!result_dimension->extent_known && right_dimension->extent_known) {
            *result_dimension = *right_dimension;
        }
    }
}

static uint64_t section_extent(int64_t lower, int64_t upper, int64_t stride) {
    uint64_t distance;
    uint64_t magnitude;
    if (stride == 0 || (stride > 0 && lower > upper) || (stride < 0 && lower < upper))
        return 0U;
    if (stride > 0) {
        distance = lower >= 0 || upper < 0
                       ? (uint64_t)upper - (uint64_t)lower
                       : (uint64_t)upper + (uint64_t)(-(lower + 1)) + UINT64_C(1);
        magnitude = (uint64_t)stride;
    } else {
        distance = upper >= 0 || lower < 0
                       ? (uint64_t)lower - (uint64_t)upper
                       : (uint64_t)lower + (uint64_t)(-(upper + 1)) + UINT64_C(1);
        magnitude = (uint64_t)(-(stride + 1)) + UINT64_C(1);
    }
    return distance / magnitude + UINT64_C(1);
}

void f2c_ast_set_array_reference_shape(AstParser *parser, F2cExpr *expression, Symbol *symbol) {
    size_t argument;
    size_t result_dimension = 0U;
    F2cShape shape;
    memset(&shape, 0, sizeof(shape));
    shape.kind = F2C_SHAPE_SCALAR;
    if (expression == NULL || symbol == NULL)
        return;
    for (argument = 0U; argument < expression->child_count && argument < symbol->rank; ++argument) {
        const F2cExpr *selector = expression->children[argument];
        F2cShapeDimension *target;
        if (selector == NULL || (selector->kind != F2C_EXPR_ARRAY_SECTION && selector->rank == 0U))
            continue;
        if (result_dimension >= F2C_MAX_RANK)
            break;
        target = &shape.dimensions[result_dimension++];
        target->kind = F2C_DIMENSION_EXPLICIT;
        target->lower_known = 1;
        target->lower = 1;
        if (selector->kind == F2C_EXPR_ARRAY_SECTION && selector->child_count == 3U) {
            const Dimension *declared = &symbol->dimensions[argument];
            const F2cExpr *lower_expression = selector->children[0];
            const F2cExpr *upper_expression = selector->children[1];
            const F2cExpr *stride_expression = selector->children[2];
            int64_t lower = 0;
            int64_t upper = 0;
            int64_t stride = 1;
            const int lower_known =
                lower_expression->kind != F2C_EXPR_INVALID
                    ? f2c_evaluate_integer_constant(parser->unit, lower_expression, &lower)
                    : (declared->lower_expression != NULL
                           ? f2c_evaluate_integer_constant(parser->unit, declared->lower_expression,
                                                           &lower)
                           : (declared->lower != NULL &&
                              f2c_evaluate_integer_text(parser->unit, declared->lower, &lower)));
            const int upper_known =
                upper_expression->kind != F2C_EXPR_INVALID
                    ? f2c_evaluate_integer_constant(parser->unit, upper_expression, &upper)
                    : (declared->upper_expression != NULL
                           ? f2c_evaluate_integer_constant(parser->unit, declared->upper_expression,
                                                           &upper)
                           : (declared->upper != NULL &&
                              f2c_evaluate_integer_text(parser->unit, declared->upper, &upper)));
            const int stride_known =
                stride_expression->kind == F2C_EXPR_INVALID ||
                f2c_evaluate_integer_constant(parser->unit, stride_expression, &stride);
            if (lower_known && upper_known && stride_known && stride != 0) {
                target->extent_known = 1;
                target->extent = section_extent(lower, upper, stride);
            }
        } else if (selector->shape.rank != 0U) {
            target->extent_known = selector->shape.dimensions[0].extent_known;
            target->extent = selector->shape.dimensions[0].extent;
        }
    }
    shape.rank = result_dimension;
    shape.kind = result_dimension == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_EXPRESSION;
    expression->shape = shape;
    expression->rank = result_dimension;
}

Type f2c_ast_common_constructor_type(Type left, Type right) {
    if (left == TYPE_UNKNOWN)
        return right;
    if (right == TYPE_UNKNOWN || left == right)
        return left;
    if (f2c_type_is_numeric(left) && f2c_type_is_numeric(right))
        return f2c_common_numeric_type(left, right);
    return TYPE_UNKNOWN;
}

int f2c_ast_precedence(const F2cToken *token) {
    if (f2c_token_equals(token, ".or.") || f2c_token_equals(token, ".eqv.") ||
        f2c_token_equals(token, ".neqv."))
        return 1;
    if (f2c_token_equals(token, ".and."))
        return 2;
    if (f2c_token_equals(token, "==") || f2c_token_equals(token, "/=") ||
        f2c_token_equals(token, "<") || f2c_token_equals(token, ">") ||
        f2c_token_equals(token, "<=") || f2c_token_equals(token, ">=") ||
        f2c_token_equals(token, ".eq.") || f2c_token_equals(token, ".ne.") ||
        f2c_token_equals(token, ".lt.") || f2c_token_equals(token, ".le.") ||
        f2c_token_equals(token, ".gt.") || f2c_token_equals(token, ".ge."))
        return 3;
    if (f2c_token_equals(token, "+") || f2c_token_equals(token, "-") ||
        f2c_token_equals(token, "//"))
        return 4;
    if (f2c_token_equals(token, "*") || f2c_token_equals(token, "/"))
        return 5;
    if (f2c_token_equals(token, "**"))
        return 6;
    return 0;
}

int f2c_ast_is_comparison(const F2cToken *token) {
    return f2c_token_equals(token, "==") || f2c_token_equals(token, "/=") ||
           f2c_token_equals(token, "<") || f2c_token_equals(token, ">") ||
           f2c_token_equals(token, "<=") || f2c_token_equals(token, ">=") ||
           f2c_token_equals(token, ".eq.") || f2c_token_equals(token, ".ne.") ||
           f2c_token_equals(token, ".lt.") || f2c_token_equals(token, ".le.") ||
           f2c_token_equals(token, ".gt.") || f2c_token_equals(token, ".ge.");
}

int f2c_ast_token_has_suffix(const F2cToken *token, const char *suffix) {
    const size_t length = strlen(suffix);
    return token->length >= length &&
           strncmp(token->begin + token->length - length, suffix, length) == 0;
}

Type f2c_ast_literal_kind_type(AstParser *parser, const F2cToken *token) {
    const char *underscore = (const char *)memchr(token->begin, '_', token->length);
    char *kind_name;
    Symbol *kind_symbol;
    size_t length;
    if (underscore == NULL || underscore + 1 >= token->begin + token->length)
        return TYPE_UNKNOWN;
    length = (size_t)((token->begin + token->length) - (underscore + 1));
    kind_name = f2c_strdup_n(underscore + 1, length);
    if (kind_name == NULL)
        return TYPE_UNKNOWN;
    kind_symbol = f2c_find_symbol(parser->unit, kind_name);
    if (kind_symbol != NULL && kind_symbol->kind_type != TYPE_UNKNOWN) {
        Type type = kind_symbol->kind_type;
        free(kind_name);
        return type;
    }
    if (strcmp(kind_name, "8") == 0 || strcmp(kind_name, "dp") == 0 ||
        strcmp(kind_name, "real64") == 0) {
        free(kind_name);
        return TYPE_DOUBLE;
    }
    if (strcmp(kind_name, "4") == 0 || strcmp(kind_name, "sp") == 0 ||
        strcmp(kind_name, "real32") == 0) {
        free(kind_name);
        return TYPE_REAL;
    }
    free(kind_name);
    return TYPE_UNKNOWN;
}

int f2c_ast_literal_kind_value(AstParser *parser, const F2cToken *token, Type literal_type) {
    const char *underscore = (const char *)memchr(token->begin, '_', token->length);
    char *kind_name;
    Symbol *kind_symbol;
    char *end = NULL;
    long value;
    size_t length;
    if (underscore == NULL || underscore + 1 >= token->begin + token->length)
        return f2c_default_kind(literal_type);
    length = (size_t)((token->begin + token->length) - (underscore + 1));
    kind_name = f2c_strdup_n(underscore + 1, length);
    if (kind_name == NULL)
        return f2c_default_kind(literal_type);
    value = strtol(kind_name, &end, 10);
    if (end != kind_name && *end == '\0' && value > 0 && value <= INT_MAX) {
        free(kind_name);
        return (int)value;
    }
    kind_symbol = f2c_find_symbol(parser->unit, kind_name);
    if (kind_symbol != NULL && kind_symbol->kind_type != TYPE_UNKNOWN)
        value = f2c_default_kind(kind_symbol->kind_type);
    else if (strcmp(kind_name, "dp") == 0 || strcmp(kind_name, "real64") == 0 ||
             strcmp(kind_name, "int64") == 0)
        value = 8;
    else if (strcmp(kind_name, "sp") == 0 || strcmp(kind_name, "real32") == 0 ||
             strcmp(kind_name, "int32") == 0)
        value = 4;
    else
        value = f2c_default_kind(literal_type);
    free(kind_name);
    return (int)value;
}

Type f2c_ast_kind_type_from_argument(const F2cExpr *argument) {
    if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
        argument->child_count == 1U)
        argument = argument->children[0];
    if (argument == NULL)
        return TYPE_UNKNOWN;
    if (argument->kind == F2C_EXPR_NAME && argument->symbol != NULL &&
        argument->symbol->kind_type != TYPE_UNKNOWN)
        return argument->symbol->kind_type;
    if (argument->kind == F2C_EXPR_INTEGER_LITERAL && argument->text != NULL) {
        if (strcmp(argument->text, "8") == 0)
            return TYPE_DOUBLE;
        if (strcmp(argument->text, "4") == 0)
            return TYPE_REAL;
    }
    return TYPE_UNKNOWN;
}

int f2c_ast_kind_value_from_argument(const F2cExpr *argument) {
    char *end = NULL;
    long value;
    Type kind_type;
    if (argument != NULL && argument->kind == F2C_EXPR_KEYWORD_ARGUMENT &&
        argument->child_count == 1U)
        argument = argument->children[0];
    if (argument == NULL)
        return 0;
    if (argument->kind == F2C_EXPR_INTEGER_LITERAL && argument->text != NULL) {
        value = strtol(argument->text, &end, 10);
        if (end != argument->text && *end == '\0' && value > 0 && value <= INT_MAX)
            return (int)value;
    }
    kind_type = f2c_ast_kind_type_from_argument(argument);
    return kind_type != TYPE_UNKNOWN ? f2c_default_kind(kind_type) : 0;
}

int f2c_ast_is_generated_c_intrinsic(const char *name) {
    static const char *const names[] = {"crealf",
                                        "cimagf",
                                        "cabsf",
                                        "fabsf",
                                        "sqrtf",
                                        "sinf",
                                        "cosf",
                                        "tanf",
                                        "expf",
                                        "logf",
                                        "log10f",
                                        "atanf",
                                        "asinf",
                                        "acosf",
                                        "atan2f",
                                        "powf",
                                        "creal",
                                        "cimag",
                                        "cabs",
                                        "fabs",
                                        "sqrt",
                                        "sin",
                                        "cos",
                                        "tan",
                                        "exp",
                                        "log",
                                        "log10",
                                        "atan",
                                        "asin",
                                        "acos",
                                        "atan2",
                                        "pow",
                                        "conjf",
                                        "conj",
                                        "f2c_cdiv",
                                        "f2c_zdiv",
                                        "csqrtf",
                                        "csqrt",
                                        "cexpf",
                                        "cexp",
                                        "clogf",
                                        "clog",
                                        "csinf",
                                        "csin",
                                        "ccosf",
                                        "ccos",
                                        "F2C_ABS",
                                        "F2C_TRANSFER",
                                        "F2C_FORTRAN_MAX",
                                        "F2C_FORTRAN_MIN"};
    size_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i]) == 0)
            return 1;
    }
    return 0;
}
