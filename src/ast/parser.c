#include "ast/internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void set_expression_shape(F2cExpr *expression, size_t rank, F2cShapeKind kind);
static void copy_expression_shape(F2cExpr *expression, const F2cShape *source);

static const F2cExpr *intrinsic_argument_value(const F2cExpr *argument) {
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
                return intrinsic_argument_value(argument);
        } else if (positional++ == position) {
            return argument;
        }
    }
    return NULL;
}

static void set_transform_intrinsic_shape(AstParser *parser, F2cExpr *expression) {
    const char *name = expression->text;
    const F2cExpr *source;
    const F2cExpr *shape;
    const F2cExpr *dimension;
    int64_t dimension_value;
    size_t result_dimension;
    size_t source_dimension;
    if (strcmp(name, "pack") == 0) {
        const F2cExpr *vector = intrinsic_named_argument(expression, "vector", 2U);
        set_expression_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
        if (vector != NULL && vector->rank == 1U)
            expression->shape.dimensions[0] = vector->shape.dimensions[0];
        return;
    }
    if (strcmp(name, "unpack") == 0) {
        const F2cExpr *mask = intrinsic_named_argument(expression, "mask", 1U);
        if (mask != NULL)
            copy_expression_shape(expression, &mask->shape);
        return;
    }
    if (strcmp(name, "reshape") == 0) {
        shape = intrinsic_named_argument(expression, "shape", 1U);
        set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
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
        set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
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
        set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
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

static F2cExpr *parse_binary(AstParser *parser, int minimum_precedence);

static void set_expression_shape(F2cExpr *expression, size_t rank, F2cShapeKind kind) {
    if (expression == NULL)
        return;
    memset(&expression->shape, 0, sizeof(expression->shape));
    expression->rank = rank;
    expression->shape.rank = rank;
    expression->shape.kind = rank == 0U ? F2C_SHAPE_SCALAR : kind;
}

static int common_expression_kind(Type result_type, const F2cExpr *left, const F2cExpr *right) {
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

static void copy_expression_shape(F2cExpr *expression, const F2cShape *source) {
    if (expression == NULL || source == NULL)
        return;
    expression->shape = *source;
    expression->rank = source->rank;
    if (expression->rank != 0U)
        expression->shape.kind = F2C_SHAPE_EXPRESSION;
}

static void set_elemental_shape(F2cExpr *expression, const F2cExpr *left, const F2cExpr *right) {
    const F2cExpr *source = NULL;
    size_t dimension;
    if (expression == NULL)
        return;
    if (left != NULL && left->rank != 0U)
        source = left;
    if (right != NULL && right->rank > (source != NULL ? source->rank : 0U))
        source = right;
    if (source == NULL) {
        set_expression_shape(expression, 0U, F2C_SHAPE_SCALAR);
        return;
    }
    copy_expression_shape(expression, &source->shape);
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

static void set_array_reference_shape(AstParser *parser, F2cExpr *expression, Symbol *symbol) {
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

static Type common_constructor_type(Type left, Type right) {
    if (left == TYPE_UNKNOWN)
        return right;
    if (right == TYPE_UNKNOWN || left == right)
        return left;
    if (f2c_type_is_numeric(left) && f2c_type_is_numeric(right))
        return f2c_common_numeric_type(left, right);
    return TYPE_UNKNOWN;
}

static int precedence(const AstToken *token) {
    if (f2c_ast_token_equals(token, ".or.") || f2c_ast_token_equals(token, ".eqv.") ||
        f2c_ast_token_equals(token, ".neqv."))
        return 1;
    if (f2c_ast_token_equals(token, ".and."))
        return 2;
    if (f2c_ast_token_equals(token, "==") || f2c_ast_token_equals(token, "/=") ||
        f2c_ast_token_equals(token, "<") || f2c_ast_token_equals(token, ">") ||
        f2c_ast_token_equals(token, "<=") || f2c_ast_token_equals(token, ">=") ||
        f2c_ast_token_equals(token, ".eq.") || f2c_ast_token_equals(token, ".ne.") ||
        f2c_ast_token_equals(token, ".lt.") || f2c_ast_token_equals(token, ".le.") ||
        f2c_ast_token_equals(token, ".gt.") || f2c_ast_token_equals(token, ".ge."))
        return 3;
    if (f2c_ast_token_equals(token, "+") || f2c_ast_token_equals(token, "-") ||
        f2c_ast_token_equals(token, "//"))
        return 4;
    if (f2c_ast_token_equals(token, "*") || f2c_ast_token_equals(token, "/"))
        return 5;
    if (f2c_ast_token_equals(token, "**"))
        return 6;
    return 0;
}

static int is_comparison(const AstToken *token) {
    return f2c_ast_token_equals(token, "==") || f2c_ast_token_equals(token, "/=") ||
           f2c_ast_token_equals(token, "<") || f2c_ast_token_equals(token, ">") ||
           f2c_ast_token_equals(token, "<=") || f2c_ast_token_equals(token, ">=") ||
           f2c_ast_token_equals(token, ".eq.") || f2c_ast_token_equals(token, ".ne.") ||
           f2c_ast_token_equals(token, ".lt.") || f2c_ast_token_equals(token, ".le.") ||
           f2c_ast_token_equals(token, ".gt.") || f2c_ast_token_equals(token, ".ge.");
}

static int token_has_suffix(const AstToken *token, const char *suffix) {
    const size_t length = strlen(suffix);
    return token->length >= length &&
           strncmp(token->begin + token->length - length, suffix, length) == 0;
}

static Type literal_kind_type(AstParser *parser, const AstToken *token) {
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

static int literal_kind_value(AstParser *parser, const AstToken *token, Type literal_type) {
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

static Type kind_type_from_argument(const F2cExpr *argument) {
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

static int kind_value_from_argument(const F2cExpr *argument) {
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
    kind_type = kind_type_from_argument(argument);
    return kind_type != TYPE_UNKNOWN ? f2c_default_kind(kind_type) : 0;
}

static int is_generated_c_intrinsic(const char *name) {
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

static void set_expression_range(const AstParser *parser, F2cExpr *expression, const char *begin,
                                 const char *end) {
    if (parser == NULL || expression == NULL || parser->source == NULL || begin == NULL ||
        end == NULL || begin < parser->source || end < begin)
        return;
    while (begin < end && isspace((unsigned char)*begin))
        ++begin;
    while (end > begin && isspace((unsigned char)end[-1]))
        --end;
    expression->source_offset = (size_t)(begin - parser->source);
    expression->source_length = (size_t)(end - begin);
}

static void set_combined_expression_range(F2cExpr *expression, const F2cExpr *left,
                                          const F2cExpr *right) {
    size_t end;
    if (expression == NULL || left == NULL || right == NULL || left->source_offset == SIZE_MAX ||
        right->source_offset == SIZE_MAX)
        return;
    end = right->source_offset + right->source_length;
    if (end < right->source_offset || end < left->source_offset)
        return;
    expression->source_offset = left->source_offset;
    expression->source_length = end - left->source_offset;
}

static F2cExpr *parse_section(AstParser *parser, F2cExpr *lower) {
    F2cExpr *section = f2c_expr_new(F2C_EXPR_ARRAY_SECTION, TYPE_UNKNOWN, NULL, 0U);
    F2cExpr *upper = NULL;
    F2cExpr *stride = NULL;
    if (section == NULL)
        return NULL;
    if (lower != NULL && !f2c_expr_push(section, lower))
        goto failed;
    if (lower == NULL &&
        !f2c_expr_push(section, f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, NULL, 0U)))
        goto failed;
    f2c_ast_next_token(parser);
    if (parser->token.kind != AST_TOKEN_COMMA && parser->token.kind != AST_TOKEN_RIGHT_PAREN &&
        parser->token.kind != AST_TOKEN_COLON)
        upper = parse_binary(parser, 1);
    if (upper == NULL)
        upper = f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, NULL, 0U);
    if (upper == NULL || !f2c_expr_push(section, upper))
        goto failed;
    if (parser->token.kind == AST_TOKEN_COLON) {
        f2c_ast_next_token(parser);
        if (parser->token.kind != AST_TOKEN_COMMA && parser->token.kind != AST_TOKEN_RIGHT_PAREN)
            stride = parse_binary(parser, 1);
    }
    if (stride == NULL)
        stride = f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, NULL, 0U);
    if (stride == NULL || !f2c_expr_push(section, stride))
        goto failed;
    section->rank = 1U;
    set_expression_shape(section, 1U, F2C_SHAPE_EXPRESSION);
    return section;

failed:
    if (lower != NULL && section->child_count == 0U)
        f2c_expr_free(lower);
    if (upper != NULL && section->child_count < 2U)
        f2c_expr_free(upper);
    if (stride != NULL && section->child_count < 3U)
        f2c_expr_free(stride);
    f2c_expr_free(section);
    return NULL;
}

static F2cExpr *parse_argument(AstParser *parser) {
    const char *begin = parser->token.begin;
    F2cExpr *lower = NULL;
    if (parser->token.kind == AST_TOKEN_NAME) {
        AstParser probe = *parser;
        const AstToken keyword = parser->token;
        f2c_ast_next_token(&probe);
        if (probe.token.kind == AST_TOKEN_OPERATOR && f2c_ast_token_equals(&probe.token, "=")) {
            F2cExpr *argument;
            F2cExpr *value;
            f2c_ast_next_token(parser);
            f2c_ast_next_token(parser);
            value = parse_binary(parser, 1);
            argument =
                f2c_expr_new(F2C_EXPR_KEYWORD_ARGUMENT, value != NULL ? value->type : TYPE_UNKNOWN,
                             keyword.begin, keyword.length);
            if (argument == NULL || value == NULL || !f2c_expr_push(argument, value)) {
                f2c_expr_free(value);
                f2c_expr_free(argument);
                return NULL;
            }
            argument->rank = value->rank;
            argument->definable = value->definable;
            argument->type_kind = value->type_kind;
            argument->value_category = value->value_category;
            argument->shape = value->shape;
            set_expression_range(parser, argument, begin, parser->token.begin);
            return argument;
        }
    }
    if (parser->token.kind != AST_TOKEN_COLON)
        lower = parse_binary(parser, 1);
    if (parser->token.kind == AST_TOKEN_COLON) {
        F2cExpr *section = parse_section(parser, lower);
        set_expression_range(parser, section, begin, parser->token.begin);
        return section;
    }
    return lower;
}

static F2cExpr *parse_postfix(AstParser *parser, const AstToken *name_token) {
    char *name = f2c_strdup_n(name_token->begin, name_token->length);
    Symbol *symbol = name != NULL ? f2c_find_symbol(parser->unit, name) : NULL;
    F2cDerivedType *derived_type = name != NULL ? f2c_find_derived_type(parser->unit, name) : NULL;
    F2cExprKind kind = F2C_EXPR_CALL;
    F2cExpr *expression;
    if (name == NULL)
        return NULL;
    if (derived_type != NULL)
        kind = F2C_EXPR_STRUCTURE_CONSTRUCTOR;
    else if (symbol != NULL && symbol->rank != 0U)
        kind = F2C_EXPR_ARRAY_REFERENCE;
    else if (symbol != NULL && symbol->type == TYPE_CHARACTER && !symbol->external)
        kind = F2C_EXPR_SUBSTRING;
    expression = f2c_expr_new(
        kind, derived_type != NULL ? TYPE_DERIVED : (symbol != NULL ? symbol->type : TYPE_UNKNOWN),
        name, strlen(name));
    free(name);
    if (expression == NULL)
        return NULL;
    expression->symbol = symbol;
    expression->derived_type =
        derived_type != NULL ? derived_type : (symbol != NULL ? symbol->derived_type : NULL);
    if (symbol != NULL)
        expression->type_kind = symbol->kind != 0 ? symbol->kind : f2c_default_kind(symbol->type);
    f2c_ast_next_token(parser);
    while (parser->token.kind != AST_TOKEN_RIGHT_PAREN && parser->token.kind != AST_TOKEN_END) {
        F2cExpr *argument = parse_argument(parser);
        if (argument == NULL || !f2c_expr_push(expression, argument)) {
            f2c_expr_free(argument);
            f2c_expr_free(expression);
            return NULL;
        }
        if (parser->token.kind == AST_TOKEN_COMMA)
            f2c_ast_next_token(parser);
        else
            break;
    }
    if (parser->token.kind != AST_TOKEN_RIGHT_PAREN) {
        f2c_ast_parser_error(parser, parser->token.begin);
    } else {
        f2c_ast_next_token(parser);
    }
    if (kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR) {
        expression->type = TYPE_DERIVED;
        expression->type_kind = 0;
        expression->value_category = F2C_VALUE_TEMPORARY;
        set_expression_shape(expression, 0U, F2C_SHAPE_SCALAR);
    } else if (kind == F2C_EXPR_ARRAY_REFERENCE) {
        set_array_reference_shape(parser, expression, symbol);
        expression->definable =
            symbol != NULL && !symbol->parameter && symbol->intent != F2C_INTENT_IN;
    } else if (kind == F2C_EXPR_SUBSTRING) {
        expression->rank = 0U;
        set_expression_shape(expression, 0U, F2C_SHAPE_SCALAR);
        expression->definable =
            symbol != NULL && !symbol->parameter && symbol->intent != F2C_INTENT_IN;
    } else if (f2c_is_intrinsic_name(expression->text) ||
               is_generated_c_intrinsic(expression->text)) {
        Type *argument_types =
            expression->child_count != 0U
                ? (Type *)malloc(expression->child_count * sizeof(*argument_types))
                : NULL;
        size_t i;
        for (i = 0U; i < expression->child_count && argument_types != NULL; ++i)
            argument_types[i] = expression->children[i]->type;
        expression->type = argument_types != NULL || expression->child_count == 0U
                               ? f2c_resolve_intrinsic_type(expression->text, argument_types,
                                                            expression->child_count)
                               : TYPE_UNKNOWN;
        if (strcmp(expression->text, "real") == 0 && expression->child_count >= 2U) {
            Type kind_type = kind_type_from_argument(expression->children[1]);
            if (kind_type == TYPE_REAL || kind_type == TYPE_DOUBLE)
                expression->type = kind_type;
        } else if (strcmp(expression->text, "cmplx") == 0 && expression->child_count >= 3U) {
            Type kind_type = kind_type_from_argument(expression->children[2]);
            if (kind_type == TYPE_REAL)
                expression->type = TYPE_COMPLEX;
            else if (kind_type == TYPE_DOUBLE)
                expression->type = TYPE_DOUBLE_COMPLEX;
        }
        free(argument_types);
        expression->rank = f2c_is_intrinsic_name(expression->text)
                               ? f2c_resolve_intrinsic_rank(expression->text, expression->children,
                                                            expression->child_count)
                               : 0U;
        if (!f2c_is_intrinsic_name(expression->text)) {
            for (i = 0U; i < expression->child_count; ++i) {
                if (expression->children[i]->rank > expression->rank)
                    expression->rank = expression->children[i]->rank;
            }
        }
        set_expression_shape(expression, expression->rank, F2C_SHAPE_EXPRESSION);
        if (expression->rank != 0U) {
            for (i = 0U; i < expression->child_count; ++i) {
                if (expression->children[i]->rank == expression->rank) {
                    copy_expression_shape(expression, &expression->children[i]->shape);
                    break;
                }
            }
        }
        if (strcmp(expression->text, "transpose") == 0 && expression->child_count == 1U &&
            expression->children[0]->shape.rank == 2U) {
            const F2cShape source_shape = expression->children[0]->shape;
            expression->shape.rank = 2U;
            expression->shape.kind = F2C_SHAPE_EXPRESSION;
            expression->shape.dimensions[0] = source_shape.dimensions[1];
            expression->shape.dimensions[1] = source_shape.dimensions[0];
        } else if (strcmp(expression->text, "matmul") == 0 && expression->child_count == 2U &&
                   expression->rank == 2U) {
            const F2cExpr *left = intrinsic_argument_value(expression->children[0]);
            const F2cExpr *right = intrinsic_argument_value(expression->children[1]);
            expression->shape.rank = 2U;
            expression->shape.kind = F2C_SHAPE_EXPRESSION;
            if (left != NULL && left->rank == 2U)
                expression->shape.dimensions[0] = left->shape.dimensions[0];
            if (right != NULL && right->rank == 2U)
                expression->shape.dimensions[1] = right->shape.dimensions[1];
        }
        if (strcmp(expression->text, "pack") == 0 || strcmp(expression->text, "unpack") == 0 ||
            strcmp(expression->text, "reshape") == 0 || strcmp(expression->text, "spread") == 0 ||
            strcmp(expression->text, "findloc") == 0)
            set_transform_intrinsic_shape(parser, expression);
        expression->type_kind = f2c_default_kind(expression->type);
        if (expression->child_count != 0U) {
            const F2cExpr *first_argument = intrinsic_argument_value(expression->children[0]);
            if (first_argument != NULL && first_argument->type == expression->type) {
                expression->type_kind = first_argument->type_kind;
                if (expression->type == TYPE_DERIVED)
                    expression->derived_type = first_argument->derived_type;
            }
        }
        if (strcmp(expression->text, "real") == 0 && expression->child_count >= 2U) {
            const int selected_kind = kind_value_from_argument(expression->children[1]);
            if (selected_kind != 0)
                expression->type_kind = selected_kind;
        } else if (strcmp(expression->text, "cmplx") == 0 && expression->child_count >= 3U) {
            const int selected_kind = kind_value_from_argument(expression->children[2]);
            if (selected_kind != 0)
                expression->type_kind = selected_kind;
        }
    } else if (symbol != NULL) {
        expression->type = symbol->type;
        expression->type_kind = symbol->kind != 0 ? symbol->kind : f2c_default_kind(symbol->type);
        if (symbol->external_result_allocatable) {
            set_expression_shape(expression, symbol->external_result_rank, F2C_SHAPE_DEFERRED);
        }
    }
    set_expression_range(parser, expression, name_token->begin, parser->token.begin);
    return expression;
}

static int token_begins_implied_do_control(const AstParser *parser) {
    AstParser probe;
    if (parser->token.kind != AST_TOKEN_NAME)
        return 0;
    probe = *parser;
    f2c_ast_next_token(&probe);
    return probe.token.kind == AST_TOKEN_OPERATOR && f2c_ast_token_equals(&probe.token, "=");
}

static int push_parenthesized_value(F2cExpr ***values, size_t *count, size_t *capacity,
                                    F2cExpr *value) {
    F2cExpr **replacement;
    if (*count == *capacity) {
        const size_t next = *capacity == 0U ? 2U : *capacity * 2U;
        replacement = (F2cExpr **)realloc(*values, next * sizeof(**values));
        if (replacement == NULL)
            return 0;
        *values = replacement;
        *capacity = next;
    }
    (*values)[(*count)++] = value;
    return 1;
}

static void free_parenthesized_values(F2cExpr **values, size_t count) {
    while (count != 0U)
        f2c_expr_free(values[--count]);
    free(values);
}

static F2cExpr *parse_implied_do(AstParser *parser, F2cExpr **values, size_t value_count) {
    const AstToken iterator_token = parser->token;
    F2cExpr *initial = NULL;
    F2cExpr *limit = NULL;
    F2cExpr *step = NULL;
    F2cExpr *expression = NULL;
    char *iterator_name = NULL;
    size_t i;

    iterator_name = f2c_strdup_n(iterator_token.begin, iterator_token.length);
    if (iterator_name == NULL)
        goto failed;
    expression = f2c_expr_new(F2C_EXPR_IMPLIED_DO, TYPE_UNKNOWN, iterator_token.begin,
                              iterator_token.length);
    if (expression == NULL)
        goto failed;
    expression->symbol = f2c_find_symbol(parser->unit, iterator_name);
    free(iterator_name);
    iterator_name = NULL;

    f2c_ast_next_token(parser);
    if (parser->token.kind != AST_TOKEN_OPERATOR || !f2c_ast_token_equals(&parser->token, "=")) {
        f2c_ast_parser_error(parser, parser->token.begin);
        goto failed;
    }
    f2c_ast_next_token(parser);
    initial = parse_binary(parser, 1);
    if (initial == NULL || parser->token.kind != AST_TOKEN_COMMA) {
        f2c_ast_parser_error(parser, parser->token.begin);
        goto failed;
    }
    f2c_ast_next_token(parser);
    limit = parse_binary(parser, 1);
    if (limit == NULL)
        goto failed;
    if (parser->token.kind == AST_TOKEN_COMMA) {
        f2c_ast_next_token(parser);
        step = parse_binary(parser, 1);
    } else {
        step = f2c_expr_new(F2C_EXPR_INTEGER_LITERAL, TYPE_INTEGER, "1", 1U);
    }
    if (step == NULL)
        goto failed;
    for (i = 0U; i < value_count; ++i) {
        expression->type = common_constructor_type(expression->type, values[i]->type);
        if (!f2c_expr_push(expression, values[i]))
            goto failed;
        values[i] = NULL;
    }
    if (!f2c_expr_push(expression, initial))
        goto failed;
    initial = NULL;
    if (!f2c_expr_push(expression, limit))
        goto failed;
    limit = NULL;
    if (!f2c_expr_push(expression, step))
        goto failed;
    step = NULL;
    expression->rank = 1U;
    set_expression_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
    return expression;

failed:
    free(iterator_name);
    f2c_expr_free(initial);
    f2c_expr_free(limit);
    f2c_expr_free(step);
    f2c_expr_free(expression);
    return NULL;
}

static F2cExpr *parse_parenthesized_expression(AstParser *parser) {
    F2cExpr **values = NULL;
    size_t value_count = 0U;
    size_t value_capacity = 0U;
    F2cExpr *result = NULL;

    f2c_ast_next_token(parser);
    for (;;) {
        F2cExpr *value = parse_binary(parser, 1);
        if (value == NULL ||
            !push_parenthesized_value(&values, &value_count, &value_capacity, value)) {
            f2c_expr_free(value);
            goto failed;
        }
        if (parser->token.kind != AST_TOKEN_COMMA)
            break;
        f2c_ast_next_token(parser);
        if (token_begins_implied_do_control(parser)) {
            result = parse_implied_do(parser, values, value_count);
            if (result == NULL)
                goto failed;
            break;
        }
    }
    if (parser->token.kind != AST_TOKEN_RIGHT_PAREN) {
        f2c_ast_parser_error(parser, parser->token.begin);
        goto failed;
    }
    f2c_ast_next_token(parser);
    if (result != NULL) {
        free(values);
        return result;
    }
    if (value_count == 1U) {
        result = values[0];
        values[0] = NULL;
    } else if (value_count == 2U) {
        const Type common = f2c_common_numeric_type(values[0]->type, values[1]->type);
        result = f2c_expr_new(F2C_EXPR_COMPLEX_LITERAL,
                              common == TYPE_DOUBLE ? TYPE_DOUBLE_COMPLEX : TYPE_COMPLEX, NULL, 0U);
        if (result == NULL || !f2c_expr_push(result, values[0]))
            goto failed;
        values[0] = NULL;
        if (!f2c_expr_push(result, values[1]))
            goto failed;
        values[1] = NULL;
        result->type_kind =
            common_expression_kind(result->type, result->children[0], result->children[1]);
    } else {
        f2c_ast_parser_error(parser, parser->token.begin);
        goto failed;
    }
    free(values);
    return result;

failed:
    f2c_expr_free(result);
    free_parenthesized_values(values, value_count);
    return NULL;
}

static F2cExpr *parse_primary(AstParser *parser) {
    AstToken token = parser->token;
    F2cExpr *expression = NULL;
    if (token.kind == AST_TOKEN_NUMBER) {
        const int real = memchr(token.begin, '.', token.length) != NULL ||
                         memchr(token.begin, 'e', token.length) != NULL ||
                         memchr(token.begin, 'E', token.length) != NULL ||
                         memchr(token.begin, 'd', token.length) != NULL ||
                         memchr(token.begin, 'D', token.length) != NULL;
        const Type suffix_type = literal_kind_type(parser, &token);
        const int double_precision =
            memchr(token.begin, 'd', token.length) != NULL ||
            memchr(token.begin, 'D', token.length) != NULL || token_has_suffix(&token, "_8") ||
            token_has_suffix(&token, "_dp") || token_has_suffix(&token, "_real64") ||
            suffix_type == TYPE_DOUBLE;
        expression =
            f2c_expr_new(real ? F2C_EXPR_REAL_LITERAL : F2C_EXPR_INTEGER_LITERAL,
                         real ? (double_precision ? TYPE_DOUBLE : TYPE_REAL) : TYPE_INTEGER,
                         token.begin, token.length);
        if (expression != NULL)
            expression->type_kind = literal_kind_value(parser, &token, expression->type);
        f2c_ast_next_token(parser);
    } else if (token.kind == AST_TOKEN_STRING || token.kind == AST_TOKEN_HOLLERITH) {
        expression =
            f2c_expr_new(F2C_EXPR_STRING_LITERAL, TYPE_CHARACTER, token.begin, token.length);
        f2c_ast_next_token(parser);
    } else if (token.kind == AST_TOKEN_BOZ) {
        expression =
            f2c_expr_new(F2C_EXPR_INTEGER_LITERAL, TYPE_INTEGER, token.begin, token.length);
        f2c_ast_next_token(parser);
    } else if (token.kind == AST_TOKEN_NAME) {
        Symbol *symbol;
        char *name;
        f2c_ast_next_token(parser);
        if (parser->token.kind == AST_TOKEN_LEFT_PAREN) {
            expression = parse_postfix(parser, &token);
        } else {
            name = f2c_strdup_n(token.begin, token.length);
            symbol = name != NULL ? f2c_find_symbol(parser->unit, name) : NULL;
            expression = f2c_expr_new(F2C_EXPR_NAME, symbol != NULL ? symbol->type : TYPE_UNKNOWN,
                                      token.begin, token.length);
            free(name);
            if (expression != NULL) {
                expression->symbol = symbol;
                expression->type_kind = symbol != NULL && symbol->kind != 0
                                            ? symbol->kind
                                            : f2c_default_kind(expression->type);
                if (symbol != NULL) {
                    if (symbol->shape.rank == symbol->rank) {
                        expression->shape = symbol->shape;
                    } else {
                        f2c_shape_from_symbol(parser->unit, &expression->shape, symbol);
                    }
                    expression->rank = symbol->rank;
                    expression->value_category =
                        symbol->external
                            ? F2C_VALUE_PROCEDURE
                            : (symbol->parameter ? F2C_VALUE_CONSTANT : F2C_VALUE_VARIABLE);
                    expression->derived_type = symbol->derived_type;
                } else {
                    set_expression_shape(expression, 0U, F2C_SHAPE_SCALAR);
                }
                expression->definable = symbol != NULL && !symbol->external && !symbol->parameter &&
                                        symbol->intent != F2C_INTENT_IN;
            }
        }
    } else if (token.kind == AST_TOKEN_OPERATOR && (f2c_ast_token_equals(&token, ".true.") ||
                                                    f2c_ast_token_equals(&token, ".false."))) {
        expression =
            f2c_expr_new(F2C_EXPR_LOGICAL_LITERAL, TYPE_LOGICAL, token.begin, token.length);
        f2c_ast_next_token(parser);
    } else if (token.kind == AST_TOKEN_OPERATOR &&
               (f2c_ast_token_equals(&token, "+") || f2c_ast_token_equals(&token, "-") ||
                f2c_ast_token_equals(&token, ".not."))) {
        F2cExpr *operand;
        f2c_ast_next_token(parser);
        operand = f2c_ast_token_equals(&token, ".not.") ? parse_binary(parser, 3)
                                                        : parse_binary(parser, 6);
        expression = f2c_expr_new(F2C_EXPR_UNARY,
                                  f2c_ast_token_equals(&token, ".not.")
                                      ? TYPE_LOGICAL
                                      : (operand != NULL ? operand->type : TYPE_UNKNOWN),
                                  token.begin, token.length);
        if (expression == NULL || operand == NULL || !f2c_expr_push(expression, operand)) {
            f2c_expr_free(operand);
            f2c_expr_free(expression);
            return NULL;
        }
        expression->type_kind = f2c_ast_token_equals(&token, ".not.")
                                    ? f2c_default_kind(TYPE_LOGICAL)
                                    : operand->type_kind;
        copy_expression_shape(expression, &operand->shape);
    } else if (token.kind == AST_TOKEN_ARRAY_BEGIN) {
        Type element_type = TYPE_UNKNOWN;
        expression = f2c_expr_new(F2C_EXPR_ARRAY_CONSTRUCTOR, TYPE_UNKNOWN, NULL, 0U);
        f2c_ast_next_token(parser);
        while (expression != NULL && parser->token.kind != AST_TOKEN_ARRAY_END &&
               parser->token.kind != AST_TOKEN_END) {
            F2cExpr *element = parse_binary(parser, 1);
            if (element == NULL || !f2c_expr_push(expression, element)) {
                f2c_expr_free(element);
                f2c_expr_free(expression);
                return NULL;
            }
            element_type = common_constructor_type(element_type, element->type);
            if (parser->token.kind == AST_TOKEN_COMMA)
                f2c_ast_next_token(parser);
            else
                break;
        }
        if (parser->token.kind != AST_TOKEN_ARRAY_END)
            f2c_ast_parser_error(parser, parser->token.begin);
        else
            f2c_ast_next_token(parser);
        if (expression != NULL) {
            expression->type = element_type;
            expression->type_kind = f2c_default_kind(element_type);
            for (size_t element_index = 0U; element_index < expression->child_count;
                 ++element_index) {
                if (expression->children[element_index]->type == element_type &&
                    expression->children[element_index]->type_kind > expression->type_kind)
                    expression->type_kind = expression->children[element_index]->type_kind;
            }
            set_expression_shape(expression, 1U, F2C_SHAPE_EXPRESSION);
            expression->shape.dimensions[0].kind = F2C_DIMENSION_EXPLICIT;
            expression->shape.dimensions[0].lower_known = 1;
            expression->shape.dimensions[0].lower = 1;
            expression->shape.dimensions[0].extent_known = 1;
            expression->shape.dimensions[0].extent = expression->child_count;
        }
    } else if (token.kind == AST_TOKEN_LEFT_PAREN) {
        expression = parse_parenthesized_expression(parser);
    } else {
        f2c_ast_parser_error(parser, token.begin);
        expression = f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, token.begin, token.length);
        if (token.kind != AST_TOKEN_END)
            f2c_ast_next_token(parser);
    }
    while (expression != NULL && parser->token.kind == AST_TOKEN_PERCENT) {
        F2cDerivedType *derived = expression->derived_type;
        AstToken component_token;
        Symbol *component = NULL;
        F2cTypeBinding *binding = NULL;
        F2cExpr *selection;
        size_t component_index;
        f2c_ast_next_token(parser);
        component_token = parser->token;
        if (component_token.kind != AST_TOKEN_NAME || derived == NULL) {
            f2c_ast_parser_error(parser, component_token.begin);
            break;
        }
        {
            F2cDerivedType *owner = derived;
            while (owner != NULL && component == NULL) {
                for (component_index = 0U; component_index < owner->component_count;
                     ++component_index) {
                    if (strlen(owner->components[component_index].name) == component_token.length &&
                        strncmp(owner->components[component_index].name, component_token.begin,
                                component_token.length) == 0) {
                        component = &owner->components[component_index];
                        break;
                    }
                }
                if (component == NULL) {
                    for (component_index = 0U; component_index < owner->binding_count;
                         ++component_index) {
                        if (strlen(owner->bindings[component_index].name) ==
                                component_token.length &&
                            strncmp(owner->bindings[component_index].name, component_token.begin,
                                    component_token.length) == 0) {
                            binding = &owner->bindings[component_index];
                            component = &binding->procedure;
                            break;
                        }
                    }
                }
                owner = owner->parent;
            }
        }
        if (component == NULL) {
            f2c_ast_parser_error(parser, component_token.begin);
            break;
        }
        selection = f2c_expr_new(F2C_EXPR_COMPONENT, component->type, component_token.begin,
                                 component_token.length);
        if (selection == NULL || !f2c_expr_push(selection, expression)) {
            f2c_expr_free(selection);
            f2c_expr_free(expression);
            return NULL;
        }
        selection->symbol = component;
        selection->derived_type = component->derived_type;
        selection->type_kind = component->kind;
        f2c_ast_next_token(parser);
        if (binding != NULL && parser->token.kind == AST_TOKEN_LEFT_PAREN) {
            F2cExpr *call = f2c_expr_new(F2C_EXPR_CALL, component->type, component_token.begin,
                                         component_token.length);
            if (call == NULL || !f2c_expr_push(call, selection)) {
                f2c_expr_free(call);
                f2c_expr_free(selection);
                return NULL;
            }
            call->symbol = component;
            call->type_kind = component->kind;
            call->rank = component->external_result_rank;
            call->value_category = F2C_VALUE_TEMPORARY;
            f2c_ast_next_token(parser);
            while (parser->token.kind != AST_TOKEN_RIGHT_PAREN &&
                   parser->token.kind != AST_TOKEN_END) {
                F2cExpr *argument = parse_argument(parser);
                if (argument == NULL || !f2c_expr_push(call, argument)) {
                    f2c_expr_free(argument);
                    f2c_expr_free(call);
                    return NULL;
                }
                if (parser->token.kind == AST_TOKEN_COMMA)
                    f2c_ast_next_token(parser);
                else
                    break;
            }
            if (parser->token.kind != AST_TOKEN_RIGHT_PAREN)
                f2c_ast_parser_error(parser, parser->token.begin);
            else
                f2c_ast_next_token(parser);
            set_expression_shape(call, call->rank,
                                 call->rank == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_EXPRESSION);
            expression = call;
            continue;
        }
        if (parser->token.kind == AST_TOKEN_LEFT_PAREN && component->rank != 0U) {
            size_t selector_count = 0U;
            f2c_ast_next_token(parser);
            while (parser->token.kind != AST_TOKEN_RIGHT_PAREN &&
                   parser->token.kind != AST_TOKEN_END) {
                F2cExpr *selector = parse_argument(parser);
                if (selector == NULL || !f2c_expr_push(selection, selector)) {
                    f2c_expr_free(selector);
                    f2c_expr_free(selection);
                    return NULL;
                }
                ++selector_count;
                if (parser->token.kind == AST_TOKEN_COMMA)
                    f2c_ast_next_token(parser);
                else
                    break;
            }
            if (parser->token.kind != AST_TOKEN_RIGHT_PAREN || selector_count != component->rank)
                f2c_ast_parser_error(parser, parser->token.begin);
            else
                f2c_ast_next_token(parser);
        }
        if (selection->child_count > 1U) {
            size_t selector;
            selection->rank = 0U;
            set_expression_shape(selection, 0U, F2C_SHAPE_SCALAR);
            for (selector = 1U; selector < selection->child_count; ++selector) {
                if (selection->children[selector]->kind == F2C_EXPR_ARRAY_SECTION ||
                    selection->children[selector]->rank != 0U) {
                    selection->rank = 1U;
                    set_expression_shape(selection, 1U, F2C_SHAPE_EXPRESSION);
                }
            }
        } else if (expression->rank == 0U) {
            selection->rank = component->rank;
            selection->shape = component->shape;
        } else if (component->rank == 0U) {
            selection->rank = expression->rank;
            selection->shape = expression->shape;
            selection->shape.kind = F2C_SHAPE_EXPRESSION;
        } else if (expression->rank + component->rank <= F2C_MAX_RANK) {
            size_t dimension;
            selection->rank = expression->rank + component->rank;
            selection->shape = expression->shape;
            selection->shape.rank = selection->rank;
            selection->shape.kind = F2C_SHAPE_EXPRESSION;
            for (dimension = 0U; dimension < component->rank; ++dimension)
                selection->shape.dimensions[expression->rank + dimension] =
                    component->shape.dimensions[dimension];
        } else {
            set_expression_shape(selection, F2C_MAX_RANK, F2C_SHAPE_UNKNOWN);
        }
        selection->value_category = F2C_VALUE_VARIABLE;
        selection->definable = expression->definable && !component->parameter;
        expression = selection;
    }
    set_expression_range(parser, expression, token.begin, parser->token.begin);
    return expression;
}

static F2cExpr *parse_binary(AstParser *parser, int minimum_precedence) {
    F2cExpr *left = parse_primary(parser);
    while (left != NULL && parser->token.kind == AST_TOKEN_OPERATOR) {
        const AstToken operator_token = parser->token;
        const int operator_precedence = precedence(&operator_token);
        F2cExpr *right;
        F2cExpr *binary;
        Type type;
        if (operator_precedence < minimum_precedence)
            break;
        f2c_ast_next_token(parser);
        right = parse_binary(parser, operator_precedence +
                                         (f2c_ast_token_equals(&operator_token, "**") ? 0 : 1));
        if (right == NULL) {
            f2c_expr_free(left);
            return NULL;
        }
        if (is_comparison(&operator_token)) {
            type = TYPE_LOGICAL;
        } else if (f2c_ast_token_equals(&operator_token, ".and.") ||
                   f2c_ast_token_equals(&operator_token, ".or.") ||
                   f2c_ast_token_equals(&operator_token, ".eqv.") ||
                   f2c_ast_token_equals(&operator_token, ".neqv.")) {
            type = TYPE_LOGICAL;
        } else if (f2c_ast_token_equals(&operator_token, "//")) {
            type = TYPE_CHARACTER;
        } else if (f2c_ast_token_equals(&operator_token, "**")) {
            type = left->type;
        } else {
            type = f2c_common_numeric_type(left->type, right->type);
        }
        binary = f2c_expr_new(F2C_EXPR_BINARY, type, operator_token.begin, operator_token.length);
        if (binary == NULL) {
            f2c_expr_free(left);
            f2c_expr_free(right);
            return NULL;
        }
        if (!f2c_expr_push(binary, left)) {
            f2c_expr_free(left);
            f2c_expr_free(right);
            f2c_expr_free(binary);
            return NULL;
        }
        left = NULL;
        if (!f2c_expr_push(binary, right)) {
            f2c_expr_free(right);
            f2c_expr_free(binary);
            return NULL;
        }
        binary->type_kind = common_expression_kind(type, binary->children[0], binary->children[1]);
        set_elemental_shape(binary, binary->children[0], binary->children[1]);
        set_combined_expression_range(binary, binary->children[0], binary->children[1]);
        left = binary;
    }
    return left;
}

static int materialize_expression_sources(F2cExpr *expression, const char *source,
                                          size_t source_length, int root) {
    size_t i;
    if (expression == NULL)
        return 1;
    for (i = 0U; i < expression->child_count; ++i) {
        if (!materialize_expression_sources(expression->children[i], source, source_length, 0))
            return 0;
    }
    free(expression->source);
    expression->source = NULL;
    if (root) {
        expression->source = f2c_strdup_n(source, source_length);
        expression->source_offset = 0U;
        expression->source_length = source_length;
    } else if (expression->source_offset != SIZE_MAX &&
               expression->source_offset <= source_length &&
               expression->source_length <= source_length - expression->source_offset) {
        expression->source =
            f2c_strdup_n(source + expression->source_offset, expression->source_length);
    }
    return expression->source != NULL || (!root && expression->source_offset == SIZE_MAX);
}

F2cExpr *f2c_parse_expression_ast(Unit *unit, const char *expression, const char **error_at) {
    AstParser parser;
    F2cExpr *result;
    const char *source = expression != NULL ? expression : "";
    memset(&parser, 0, sizeof(parser));
    parser.unit = unit;
    parser.source = source;
    parser.cursor = source;
    f2c_ast_next_token(&parser);
    result = parse_binary(&parser, 1);
    if (parser.token.kind != AST_TOKEN_END)
        f2c_ast_parser_error(&parser, parser.token.begin);
    if (result == NULL && parser.error_at != NULL)
        result = f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, parser.error_at, 0U);
    if (result != NULL) {
        const size_t length = strlen(source);
        if (!materialize_expression_sources(result, source, length, 1)) {
            f2c_expr_free(result);
            result = NULL;
        } else if (parser.error_at != NULL) {
            result->parse_error_offset =
                parser.error_at >= source && parser.error_at <= source + length
                    ? (size_t)(parser.error_at - source)
                    : length;
        }
    }
    if (error_at != NULL)
        *error_at = parser.error_at;
    return result;
}

Type f2c_expression_type(Unit *unit, const char *expression) {
    const char *error_at = NULL;
    F2cExpr *ast = f2c_parse_expression_ast(unit, expression, &error_at);
    Type type = ast != NULL && error_at == NULL ? ast->type : TYPE_UNKNOWN;
    f2c_expr_free(ast);
    return type;
}

int f2c_expression_is_designator(Unit *unit, const char *expression) {
    const char *error_at = NULL;
    F2cExpr *ast = f2c_parse_expression_ast(unit, expression, &error_at);
    const int result = ast != NULL && error_at == NULL && ast->definable;
    f2c_expr_free(ast);
    return result;
}
