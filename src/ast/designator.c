#include "ast/internal.h"
#include "ir/storage.h"

#include <stdlib.h>
#include <string.h>

static int push_expression(AstParser *parser, F2cExpr *parent, F2cExpr *child) {
    return f2c_ast_push_expression(parser, parent, child);
}

static F2cExpr *parse_section(AstParser *parser, F2cExpr *lower) {
    F2cExpr *section = f2c_expr_new(F2C_EXPR_ARRAY_SECTION, TYPE_UNKNOWN, NULL, 0U);
    F2cExpr *upper = NULL;
    F2cExpr *stride = NULL;
    if (section == NULL)
        return NULL;
    if (lower != NULL && !push_expression(parser, section, lower))
        goto failed;
    if (lower == NULL &&
        !push_expression(parser, section, f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, NULL, 0U)))
        goto failed;
    f2c_ast_next_token(parser);
    if (parser->token.kind != F2C_TOKEN_COMMA && parser->token.kind != F2C_TOKEN_RIGHT_PAREN &&
        parser->token.kind != F2C_TOKEN_COLON)
        upper = f2c_ast_parse_binary(parser, 1);
    if (upper == NULL)
        upper = f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, NULL, 0U);
    if (upper == NULL || !push_expression(parser, section, upper))
        goto failed;
    if (parser->token.kind == F2C_TOKEN_COLON) {
        f2c_ast_next_token(parser);
        if (parser->token.kind != F2C_TOKEN_COMMA && parser->token.kind != F2C_TOKEN_RIGHT_PAREN)
            stride = f2c_ast_parse_binary(parser, 1);
    }
    if (stride == NULL)
        stride = f2c_expr_new(F2C_EXPR_INVALID, TYPE_UNKNOWN, NULL, 0U);
    if (stride == NULL || !push_expression(parser, section, stride))
        goto failed;
    section->rank = 1U;
    f2c_ast_set_expression_shape(section, 1U, F2C_SHAPE_EXPRESSION);
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

F2cExpr *f2c_ast_parse_argument(AstParser *parser) {
    const char *begin = parser->token.begin;
    F2cExpr *lower = NULL;
    if (parser->token.kind == F2C_TOKEN_IDENTIFIER) {
        AstParser probe = *parser;
        const F2cToken keyword = parser->token;
        f2c_ast_next_token(&probe);
        if (probe.token.kind == F2C_TOKEN_OPERATOR && f2c_token_equals(&probe.token, "=")) {
            F2cExpr *argument;
            F2cExpr *value;
            f2c_ast_next_token(parser);
            f2c_ast_next_token(parser);
            value = f2c_ast_parse_binary(parser, 1);
            argument =
                f2c_expr_new(F2C_EXPR_KEYWORD_ARGUMENT, value != NULL ? value->type : TYPE_UNKNOWN,
                             keyword.begin, keyword.length);
            if (argument == NULL || value == NULL || !push_expression(parser, argument, value)) {
                f2c_expr_free(value);
                f2c_expr_free(argument);
                return NULL;
            }
            argument->rank = value->rank;
            argument->definable = value->definable;
            argument->type_kind = value->type_kind;
            argument->value_category = value->value_category;
            argument->shape = value->shape;
            f2c_analyze_expression_access(argument);
            f2c_ast_set_expression_range(parser, argument, begin, parser->token.begin);
            return argument;
        }
    }
    if (parser->token.kind != F2C_TOKEN_COLON)
        lower = f2c_ast_parse_binary(parser, 1);
    if (parser->token.kind == F2C_TOKEN_COLON) {
        F2cExpr *section = parse_section(parser, lower);
        f2c_ast_set_expression_range(parser, section, begin, parser->token.begin);
        return section;
    }
    return lower;
}

F2cExpr *f2c_ast_parse_name(AstParser *parser, const F2cToken *name_token) {
    char *name = f2c_strdup_n(name_token->begin, name_token->length);
    Symbol *symbol = name != NULL ? f2c_find_symbol(parser->unit, name) : NULL;
    F2cDerivedType *derived = name != NULL ? f2c_find_derived_type(parser->unit, name) : NULL;
    F2cExprKind kind = F2C_EXPR_NAME;
    F2cExpr *expression;
    if (name == NULL)
        return NULL;
    if (parser->token.kind == F2C_TOKEN_LEFT_PAREN) {
        if (derived != NULL)
            kind = F2C_EXPR_STRUCTURE_CONSTRUCTOR;
        else if (symbol != NULL && symbol->rank != 0U)
            kind = F2C_EXPR_ARRAY_REFERENCE;
        else if (symbol == NULL || symbol->type != TYPE_CHARACTER || symbol->external ||
                 symbol->intrinsic != NULL)
            kind = F2C_EXPR_CALL;
    }
    if (kind == F2C_EXPR_CALL && symbol != NULL && symbol->intrinsic != NULL) {
        char *intrinsic_name = f2c_strdup(symbol->intrinsic->name);
        free(name);
        name = intrinsic_name;
        if (name == NULL)
            return NULL;
    }
    expression = f2c_expr_new(kind,
                              kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR
                                  ? TYPE_DERIVED
                                  : (symbol != NULL ? symbol->type : TYPE_UNKNOWN),
                              name, strlen(name));
    free(name);
    if (expression == NULL)
        return NULL;
    expression->symbol = symbol;
    expression->derived_type = kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR
                                   ? derived
                                   : (symbol != NULL ? symbol->derived_type : NULL);
    expression->type_kind =
        symbol != NULL && symbol->kind != 0 ? symbol->kind : f2c_default_kind(expression->type);
    if (kind != F2C_EXPR_NAME) {
        f2c_ast_next_token(parser);
        while (parser->token.kind != F2C_TOKEN_RIGHT_PAREN && parser->token.kind != F2C_TOKEN_END) {
            F2cExpr *argument = f2c_ast_parse_argument(parser);
            if (argument == NULL || !f2c_ast_push_expression(parser, expression, argument)) {
                f2c_expr_free(argument);
                f2c_expr_free(expression);
                return NULL;
            }
            if (parser->token.kind == F2C_TOKEN_COMMA)
                f2c_ast_next_token(parser);
            else
                break;
        }
        if (parser->token.kind != F2C_TOKEN_RIGHT_PAREN)
            f2c_ast_parser_error(parser, parser->token.begin);
        else
            f2c_ast_next_token(parser);
    }
    if (kind == F2C_EXPR_NAME) {
        if (symbol != NULL) {
            if (symbol->shape.rank == symbol->rank)
                expression->shape = symbol->shape;
            else
                f2c_shape_from_symbol(parser->unit, &expression->shape, symbol);
            expression->rank = symbol->rank;
            expression->value_category = (symbol->external || symbol->intrinsic != NULL)
                                             ? F2C_VALUE_PROCEDURE
                                         : symbol->parameter ? F2C_VALUE_CONSTANT
                                                             : F2C_VALUE_VARIABLE;
        } else {
            f2c_ast_set_expression_shape(expression, 0U, F2C_SHAPE_SCALAR);
        }
        expression->definable = symbol != NULL && !symbol->external && symbol->intrinsic == NULL &&
                                !f2c_ir_symbol_storage_reference(symbol).readonly_storage;
    } else if (kind == F2C_EXPR_STRUCTURE_CONSTRUCTOR) {
        expression->type_kind = 0;
        expression->value_category = F2C_VALUE_TEMPORARY;
        f2c_ast_set_expression_shape(expression, 0U, F2C_SHAPE_SCALAR);
    } else if (kind == F2C_EXPR_ARRAY_REFERENCE) {
        f2c_ast_set_array_reference_shape(parser, expression, symbol);
        expression->definable =
            symbol != NULL && !f2c_ir_symbol_storage_reference(symbol).readonly_storage;
    } else if ((f2c_is_intrinsic_name(expression->text) ||
                f2c_ast_is_generated_c_intrinsic(expression->text)) &&
               (symbol == NULL || (!symbol->external && !symbol->statement_function))) {
        f2c_ast_resolve_intrinsic_call(parser, expression);
    } else if (symbol != NULL && symbol->external_result_rank != 0U) {
        expression->rank = symbol->external_result_rank;
        expression->shape = symbol->shape;
        if (expression->shape.kind == F2C_SHAPE_UNKNOWN ||
            expression->shape.rank != symbol->external_result_rank)
            f2c_ast_set_expression_shape(
                expression, symbol->external_result_rank,
                symbol->external_result_allocatable ? F2C_SHAPE_DEFERRED : F2C_SHAPE_EXPRESSION);
    }
    f2c_analyze_expression_access(expression);
    f2c_ast_set_expression_range(parser, expression, name_token->begin, parser->token.begin);
    return expression;
}

F2cExpr *f2c_ast_parse_designator(AstParser *parser, F2cExpr *expression) {
    const char *begin = expression != NULL && expression->source_offset != SIZE_MAX
                            ? parser->source + expression->source_offset
                            : parser->token.begin;
    while (expression != NULL &&
           (parser->token.kind == F2C_TOKEN_PERCENT ||
            (parser->token.kind == F2C_TOKEN_LEFT_PAREN && expression->type == TYPE_CHARACTER))) {
        if (parser->token.kind == F2C_TOKEN_LEFT_PAREN) {
            F2cExpr *range;
            F2cExpr *substring = f2c_expr_new(F2C_EXPR_SUBSTRING, TYPE_CHARACTER, NULL, 0U);
            if (substring == NULL) {
                f2c_expr_free(expression);
                return NULL;
            }
            substring->symbol = expression->symbol;
            substring->type_kind = expression->type_kind;
            substring->value_category = expression->value_category;
            substring->definable = expression->definable;
            f2c_ast_copy_expression_shape(substring, &expression->shape);
            if (!f2c_ast_push_expression(parser, substring, expression)) {
                f2c_expr_free(expression);
                f2c_expr_free(substring);
                return NULL;
            }
            f2c_ast_next_token(parser);
            range = f2c_ast_parse_argument(parser);
            if (range == NULL || !f2c_ast_push_expression(parser, substring, range)) {
                f2c_expr_free(range);
                f2c_expr_free(substring);
                return NULL;
            }
            if (parser->token.kind != F2C_TOKEN_RIGHT_PAREN)
                f2c_ast_parser_error(parser, parser->token.begin);
            else
                f2c_ast_next_token(parser);
            f2c_ast_set_expression_range(parser, substring, begin, parser->token.begin);
            expression = substring;
            f2c_analyze_expression_access(expression);
            continue;
        }
        F2cDerivedType *derived = expression->derived_type;
        F2cToken component_token;
        Symbol *component = NULL;
        F2cTypeBinding *binding = NULL;
        F2cExpr *selection;
        size_t component_index;
        f2c_ast_next_token(parser);
        component_token = parser->token;
        if (component_token.kind != F2C_TOKEN_IDENTIFIER || derived == NULL) {
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
        if (selection == NULL || !f2c_ast_push_expression(parser, selection, expression)) {
            f2c_expr_free(selection);
            f2c_expr_free(expression);
            return NULL;
        }
        selection->symbol = component;
        selection->derived_type = component->derived_type;
        selection->type_kind = component->kind;
        f2c_ast_next_token(parser);
        if (binding != NULL && parser->token.kind == F2C_TOKEN_LEFT_PAREN) {
            F2cExpr *call = f2c_expr_new(F2C_EXPR_CALL, component->type, component_token.begin,
                                         component_token.length);
            if (call == NULL || !f2c_ast_push_expression(parser, call, selection)) {
                f2c_expr_free(call);
                f2c_expr_free(selection);
                return NULL;
            }
            call->symbol = component;
            call->derived_type = component->derived_type;
            call->type_kind = component->kind;
            call->rank = component->external_result_rank;
            call->value_category = F2C_VALUE_TEMPORARY;
            f2c_ast_next_token(parser);
            while (parser->token.kind != F2C_TOKEN_RIGHT_PAREN &&
                   parser->token.kind != F2C_TOKEN_END) {
                F2cExpr *argument = f2c_ast_parse_argument(parser);
                if (argument == NULL || !f2c_ast_push_expression(parser, call, argument)) {
                    f2c_expr_free(argument);
                    f2c_expr_free(call);
                    return NULL;
                }
                if (parser->token.kind == F2C_TOKEN_COMMA)
                    f2c_ast_next_token(parser);
                else
                    break;
            }
            if (parser->token.kind != F2C_TOKEN_RIGHT_PAREN)
                f2c_ast_parser_error(parser, parser->token.begin);
            else
                f2c_ast_next_token(parser);
            f2c_ast_set_expression_shape(
                call, call->rank, call->rank == 0U ? F2C_SHAPE_SCALAR : F2C_SHAPE_EXPRESSION);
            f2c_ast_set_expression_range(parser, call, begin, parser->token.begin);
            expression = call;
            continue;
        }
        if (parser->token.kind == F2C_TOKEN_LEFT_PAREN && component->rank != 0U) {
            size_t selector_count = 0U;
            f2c_ast_next_token(parser);
            while (parser->token.kind != F2C_TOKEN_RIGHT_PAREN &&
                   parser->token.kind != F2C_TOKEN_END) {
                F2cExpr *selector = f2c_ast_parse_argument(parser);
                if (selector == NULL || !f2c_ast_push_expression(parser, selection, selector)) {
                    f2c_expr_free(selector);
                    f2c_expr_free(selection);
                    return NULL;
                }
                ++selector_count;
                if (parser->token.kind == F2C_TOKEN_COMMA)
                    f2c_ast_next_token(parser);
                else
                    break;
            }
            if (parser->token.kind != F2C_TOKEN_RIGHT_PAREN || selector_count != component->rank)
                f2c_ast_parser_error(parser, parser->token.begin);
            else
                f2c_ast_next_token(parser);
        }
        if (selection->child_count > 1U) {
            size_t selector;
            selection->rank = 0U;
            f2c_ast_set_expression_shape(selection, 0U, F2C_SHAPE_SCALAR);
            for (selector = 1U; selector < selection->child_count; ++selector) {
                if (selection->children[selector]->kind == F2C_EXPR_ARRAY_SECTION ||
                    selection->children[selector]->rank != 0U) {
                    if (selection->rank < F2C_MAX_RANK)
                        ++selection->rank;
                }
            }
            f2c_ast_set_expression_shape(selection, selection->rank,
                                         selection->rank == 0U ? F2C_SHAPE_SCALAR
                                                               : F2C_SHAPE_EXPRESSION);
            if (expression->rank != 0U) {
                if (selection->rank != 0U)
                    f2c_ast_parser_error(parser, component_token.begin);
                else
                    f2c_ast_copy_expression_shape(selection, &expression->shape);
            }
        } else if (expression->rank == 0U) {
            selection->rank = component->rank;
            selection->shape = component->shape;
        } else if (component->rank == 0U) {
            selection->rank = expression->rank;
            selection->shape = expression->shape;
            selection->shape.kind = F2C_SHAPE_EXPRESSION;
        } else {
            f2c_ast_parser_error(parser, component_token.begin);
            f2c_ast_set_expression_shape(selection, F2C_MAX_RANK, F2C_SHAPE_UNKNOWN);
        }
        selection->value_category = F2C_VALUE_VARIABLE;
        selection->definable =
            !component->parameter && (expression->definable || component->pointer);
        f2c_analyze_expression_access(selection);
        f2c_ast_set_expression_range(parser, selection, begin, parser->token.begin);
        expression = selection;
    }
    return expression;
}
