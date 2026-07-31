#include "ast/internal.h"
#include "semantic/numeric_model.h"

#include <ctype.h>
#include <errno.h>
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

static uint64_t constructor_unsigned_distance(int64_t lower, int64_t upper) {
    if (lower >= 0 || upper < 0)
        return (uint64_t)(upper - lower);
    return (uint64_t)upper + (uint64_t)(-(lower + 1)) + UINT64_C(1);
}

static uint64_t constructor_unsigned_magnitude(int64_t value) {
    return value >= 0 ? (uint64_t)value : (uint64_t)(-(value + 1)) + UINT64_C(1);
}

static int constructor_extent_add(uint64_t left, uint64_t right, uint64_t *result) {
    if (right > UINT64_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

static int constructor_extent_multiply(uint64_t left, uint64_t right, uint64_t *result) {
    if (left != 0U && right > UINT64_MAX / left)
        return 0;
    *result = left * right;
    return 1;
}

int f2c_ast_constructor_extent(Unit *unit, const F2cExpr *expression, uint64_t *extent) {
    uint64_t total = UINT64_C(0);
    size_t child;
    if (expression == NULL || extent == NULL)
        return 0;
    if (expression->kind == F2C_EXPR_ARRAY_CONSTRUCTOR) {
        for (child = 0U; child < expression->child_count; ++child) {
            uint64_t child_extent;
            const int known =
                f2c_ast_constructor_extent(unit, expression->children[child], &child_extent);
            if (known <= 0)
                return known;
            if (!constructor_extent_add(total, child_extent, &total))
                return -1;
        }
        *extent = total;
        return 1;
    }
    if (expression->kind == F2C_EXPR_IMPLIED_DO) {
        const size_t value_count =
            expression->child_count >= 3U ? expression->child_count - 3U : 0U;
        int64_t first;
        int64_t last;
        int64_t step;
        uint64_t iterations;
        if (value_count == 0U ||
            !f2c_evaluate_integer_constant(unit, expression->children[value_count], &first) ||
            !f2c_evaluate_integer_constant(unit, expression->children[value_count + 1U], &last) ||
            !f2c_evaluate_integer_constant(unit, expression->children[value_count + 2U], &step) ||
            step == 0)
            return 0;
        if ((step > 0 && first > last) || (step < 0 && first < last)) {
            *extent = UINT64_C(0);
            return 1;
        }
        iterations =
            constructor_unsigned_distance(step > 0 ? first : last, step > 0 ? last : first) /
            constructor_unsigned_magnitude(step);
        if (iterations == UINT64_MAX)
            return -1;
        ++iterations;
        for (child = 0U; child < value_count; ++child) {
            uint64_t child_extent;
            const int known =
                f2c_ast_constructor_extent(unit, expression->children[child], &child_extent);
            if (known <= 0)
                return known;
            if (!constructor_extent_add(total, child_extent, &total))
                return -1;
        }
        if (!constructor_extent_multiply(total, iterations, &total))
            return -1;
        *extent = total;
        return 1;
    }
    if (expression->rank == 0U) {
        *extent = UINT64_C(1);
        return 1;
    }
    if (expression->rank == 1U && expression->shape.dimensions[0].extent_known) {
        *extent = expression->shape.dimensions[0].extent;
        return 1;
    }
    return 0;
}

const F2cExpr *f2c_ast_intrinsic_argument(const F2cExpr *call, const char *keyword,
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
    const F2cExpr *source;
    const F2cExpr *shape;
    const F2cExpr *dimension;
    int64_t dimension_value;
    size_t result_dimension;
    size_t source_dimension;
    if (f2c_intrinsic_is_reduction(expression->intrinsic)) {
        const int logical_reduction = expression->intrinsic == F2C_INTRINSIC_ALL ||
                                      expression->intrinsic == F2C_INTRINSIC_ANY ||
                                      expression->intrinsic == F2C_INTRINSIC_COUNT;
        const int location = expression->intrinsic == F2C_INTRINSIC_MAXLOC ||
                             expression->intrinsic == F2C_INTRINSIC_MINLOC;
        source = f2c_ast_intrinsic_argument(expression, logical_reduction ? "mask" : "array", 0U);
        dimension = expression->intrinsic == F2C_INTRINSIC_DOT_PRODUCT
                        ? NULL
                        : f2c_ast_intrinsic_argument(expression, "dim", 1U);
        f2c_ast_set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        if (source == NULL || expression->intrinsic == F2C_INTRINSIC_DOT_PRODUCT)
            return;
        if (dimension == NULL) {
            if (location && expression->rank == 1U) {
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
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_SHAPE ||
        expression->intrinsic == F2C_INTRINSIC_LBOUND ||
        expression->intrinsic == F2C_INTRINSIC_UBOUND) {
        const int shape_inquiry = expression->intrinsic == F2C_INTRINSIC_SHAPE;
        source = f2c_ast_intrinsic_argument(expression, shape_inquiry ? "source" : "array", 0U);
        dimension = shape_inquiry ? NULL : f2c_ast_intrinsic_argument(expression, "dim", 1U);
        if (dimension != NULL) {
            f2c_ast_set_expression_shape(expression, 0U, F2C_SHAPE_SCALAR);
        } else {
            f2c_ast_set_expression_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
            if (source != NULL) {
                expression->shape.dimensions[0].extent_known = 1;
                expression->shape.dimensions[0].extent = source->rank;
            }
        }
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_PACK) {
        const F2cExpr *vector = f2c_ast_intrinsic_argument(expression, "vector", 2U);
        f2c_ast_set_expression_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
        if (vector != NULL && vector->rank == 1U)
            expression->shape.dimensions[0] = vector->shape.dimensions[0];
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_UNPACK) {
        const F2cExpr *mask = f2c_ast_intrinsic_argument(expression, "mask", 1U);
        if (mask != NULL)
            f2c_ast_copy_expression_shape(expression, &mask->shape);
        return;
    }
    if (expression->intrinsic == F2C_INTRINSIC_RESHAPE) {
        shape = f2c_ast_intrinsic_argument(expression, "shape", 1U);
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
    if (expression->intrinsic == F2C_INTRINSIC_SPREAD) {
        source = f2c_ast_intrinsic_argument(expression, "source", 0U);
        dimension = f2c_ast_intrinsic_argument(expression, "dim", 1U);
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
    if (expression->intrinsic == F2C_INTRINSIC_FINDLOC) {
        source = f2c_ast_intrinsic_argument(expression, "array", 0U);
        dimension = f2c_ast_intrinsic_argument(expression, "dim", 2U);
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
                       : symbol->dimension_lower_syntax[argument].count != 0U
                           ? f2c_evaluate_integer_syntax(
                                 parser->unit, symbol->dimension_lower_syntax[argument], &lower)
                           : (lower = 1, 1));
            const int upper_known =
                upper_expression->kind != F2C_EXPR_INVALID
                    ? f2c_evaluate_integer_constant(parser->unit, upper_expression, &upper)
                    : (declared->upper_expression != NULL
                           ? f2c_evaluate_integer_constant(parser->unit, declared->upper_expression,
                                                           &upper)
                           : f2c_evaluate_integer_syntax(
                                 parser->unit, symbol->dimension_upper_syntax[argument], &upper));
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
    if (f2c_ast_is_defined_operator(token))
        return 1;
    if (f2c_token_equals(token, ".or.") || f2c_token_equals(token, ".eqv.") ||
        f2c_token_equals(token, ".neqv."))
        return 2;
    if (f2c_token_equals(token, ".and."))
        return 3;
    if (f2c_token_equals(token, "==") || f2c_token_equals(token, "/=") ||
        f2c_token_equals(token, "<") || f2c_token_equals(token, ">") ||
        f2c_token_equals(token, "<=") || f2c_token_equals(token, ">=") ||
        f2c_token_equals(token, ".eq.") || f2c_token_equals(token, ".ne.") ||
        f2c_token_equals(token, ".lt.") || f2c_token_equals(token, ".le.") ||
        f2c_token_equals(token, ".gt.") || f2c_token_equals(token, ".ge."))
        return 4;
    if (f2c_token_equals(token, "+") || f2c_token_equals(token, "-") ||
        f2c_token_equals(token, "//"))
        return 5;
    if (f2c_token_equals(token, "*") || f2c_token_equals(token, "/"))
        return 6;
    if (f2c_token_equals(token, "**"))
        return 7;
    return 0;
}

int f2c_ast_is_defined_operator(const F2cToken *token) {
    return token != NULL && token->kind == F2C_TOKEN_OPERATOR && token->length >= 3U &&
           token->begin[0] == '.' && token->begin[token->length - 1U] == '.' &&
           !f2c_token_equals(token, ".true.") && !f2c_token_equals(token, ".false.") &&
           !f2c_token_equals(token, ".not.") && !f2c_token_equals(token, ".and.") &&
           !f2c_token_equals(token, ".or.") && !f2c_token_equals(token, ".eqv.") &&
           !f2c_token_equals(token, ".neqv.") && !f2c_ast_is_comparison(token);
}

int f2c_ast_is_comparison(const F2cToken *token) {
    return f2c_token_equals(token, "==") || f2c_token_equals(token, "/=") ||
           f2c_token_equals(token, "<") || f2c_token_equals(token, ">") ||
           f2c_token_equals(token, "<=") || f2c_token_equals(token, ">=") ||
           f2c_token_equals(token, ".eq.") || f2c_token_equals(token, ".ne.") ||
           f2c_token_equals(token, ".lt.") || f2c_token_equals(token, ".le.") ||
           f2c_token_equals(token, ".gt.") || f2c_token_equals(token, ".ge.");
}

static char *literal_kind_name(const F2cToken *token) {
    const char *underscore = (const char *)memchr(token->begin, '_', token->length);
    const char *quote;
    size_t length;
    if (underscore == NULL || underscore + 1 >= token->begin + token->length)
        return NULL;
    if (token->kind == F2C_TOKEN_STRING) {
        quote = f2c_character_literal_quote(token->begin);
        if (quote == NULL || quote <= token->begin || quote > token->begin + token->length ||
            quote[-1] != '_')
            return NULL;
        return f2c_strdup_n(token->begin, (size_t)(quote - token->begin - 1));
    }
    length = (size_t)((token->begin + token->length) - (underscore + 1));
    return f2c_strdup_n(underscore + 1, length);
}

static int symbol_kind_value(Unit *unit, const Symbol *symbol, int64_t *value) {
    if (symbol == NULL || !symbol->parameter)
        return 0;
    if (symbol->initializer_expression != NULL &&
        f2c_evaluate_integer_constant(unit, symbol->initializer_expression, value))
        return 1;
    return symbol->initializer_syntax.count != 0U &&
           f2c_evaluate_integer_syntax(unit, symbol->initializer_syntax, value);
}

static int literal_kind_supported(Type literal_type, int kind) {
    if (literal_type == TYPE_INTEGER)
        return f2c_numeric_model(TYPE_INTEGER, kind) != NULL;
    if (literal_type == TYPE_REAL || literal_type == TYPE_DOUBLE)
        return f2c_numeric_model(TYPE_REAL, kind) != NULL;
    if (literal_type == TYPE_CHARACTER)
        return kind == f2c_default_kind(TYPE_CHARACTER);
    return 0;
}

int f2c_ast_literal_kind_value(AstParser *parser, const F2cToken *token, Type literal_type) {
    char *kind_name = literal_kind_name(token);
    Symbol *kind_symbol;
    char *end = NULL;
    int64_t value;
    if (kind_name == NULL)
        return f2c_default_kind(literal_type);
    errno = 0;
    value = (int64_t)strtoll(kind_name, &end, 10);
    if (errno != 0 || end == kind_name || *end != '\0') {
        kind_symbol = parser != NULL && parser->unit != NULL
                          ? f2c_find_symbol(parser->unit, kind_name)
                          : NULL;
        if (!symbol_kind_value(parser != NULL ? parser->unit : NULL, kind_symbol, &value))
            value = 0;
    }
    free(kind_name);
    if (value <= 0 || value > INT_MAX || !literal_kind_supported(literal_type, (int)value)) {
        if (parser != NULL)
            f2c_ast_parser_error(parser, token != NULL ? token->begin : parser->cursor);
        return f2c_default_kind(literal_type);
    }
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
