#include "frontend/declaration/private.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int left_delimiter(F2cTokenKind kind) {
    return kind == F2C_TOKEN_LEFT_PAREN || kind == F2C_TOKEN_LEFT_BRACKET ||
           kind == F2C_TOKEN_ARRAY_BEGIN;
}

static Type normalized_kind_type(Type type) {
    if (type == TYPE_DOUBLE_COMPLEX)
        return TYPE_DOUBLE;
    if (type == TYPE_COMPLEX)
        return TYPE_REAL;
    return type;
}

static int intrinsic_selected_kind(const F2cExpr *expression, Type *kind_type) {
    int64_t precision = 0;
    int64_t range = 0;
    if (expression == NULL || expression->kind != F2C_EXPR_CALL || expression->text == NULL)
        return 0;
    if (strcmp(expression->text, "selected_real_kind") == 0 && expression->child_count != 0U &&
        f2c_evaluate_integer_constant(NULL, expression->children[0], &precision)) {
        if (expression->child_count >= 2U &&
            !f2c_evaluate_integer_constant(NULL, expression->children[1], &range))
            return 0;
        if (precision <= 6 && (range == 0 || range <= 37)) {
            *kind_type = TYPE_REAL;
            return 1;
        }
        if (precision <= 15 && (range == 0 || range <= 307)) {
            *kind_type = TYPE_DOUBLE;
            return 1;
        }
    }
    return 0;
}

Type f2c_kind_type_from_tokens(Unit *unit, const Line *line, size_t begin, size_t end) {
    const char *error_at = NULL;
    F2cExpr *expression;
    Type result = TYPE_UNKNOWN;
    if (line == NULL || begin >= end || end > line->token_count)
        return TYPE_UNKNOWN;
    if (end == begin + 1U && line->tokens[begin].kind == F2C_TOKEN_IDENTIFIER) {
        char *name = f2c_token_text(&line->tokens[begin]);
        Symbol *symbol = name != NULL && unit != NULL ? f2c_find_symbol(unit, name) : NULL;
        if (name != NULL &&
            (strcmp(name, "real64") == 0 || strcmp(name, "dp") == 0 || strcmp(name, "int64") == 0))
            result = TYPE_DOUBLE;
        else if (name != NULL && (strcmp(name, "real32") == 0 || strcmp(name, "sp") == 0 ||
                                  strcmp(name, "int32") == 0))
            result = TYPE_REAL;
        else if (symbol != NULL)
            result = symbol->kind_type;
        free(name);
        if (result != TYPE_UNKNOWN)
            return result;
    }
    expression =
        f2c_parse_expression_tokens(unit, &line->tokens[begin], end - begin, line->text, &error_at);
    if (expression != NULL && error_at == NULL) {
        if (expression->kind == F2C_EXPR_CALL && expression->text != NULL &&
            strcmp(expression->text, "kind") == 0 && expression->child_count == 1U)
            result = normalized_kind_type(expression->children[0]->type);
        else
            (void)intrinsic_selected_kind(expression, &result);
    }
    f2c_expr_free(expression);
    return result == TYPE_REAL || result == TYPE_DOUBLE ? result : TYPE_UNKNOWN;
}

static int named_kind_value(Unit *unit, const F2cToken *token, int *value) {
    char *name;
    Symbol *symbol;
    int64_t constant;
    if (token == NULL || token->kind != F2C_TOKEN_IDENTIFIER)
        return 0;
    name = f2c_token_text(token);
    if (name == NULL)
        return 0;
    if (strcmp(name, "int8") == 0) {
        *value = 1;
    } else if (strcmp(name, "int16") == 0) {
        *value = 2;
    } else if (strcmp(name, "int32") == 0 || strcmp(name, "real32") == 0 ||
               strcmp(name, "sp") == 0) {
        *value = 4;
    } else if (strcmp(name, "int64") == 0 || strcmp(name, "real64") == 0 ||
               strcmp(name, "dp") == 0) {
        *value = 8;
    } else {
        symbol = unit != NULL ? f2c_find_symbol(unit, name) : NULL;
        if (symbol == NULL) {
            free(name);
            return 0;
        }
        if (symbol->kind_type != TYPE_UNKNOWN) {
            *value = f2c_default_kind(symbol->kind_type);
        } else if (symbol->initializer_expression != NULL &&
                   f2c_evaluate_integer_constant(unit, symbol->initializer_expression, &constant) &&
                   constant > 0 && constant <= INT_MAX) {
            *value = (int)constant;
        } else if (symbol->initializer_syntax.count != 0U &&
                   f2c_evaluate_integer_syntax(unit, symbol->initializer_syntax, &constant) &&
                   constant > 0 && constant <= INT_MAX) {
            *value = (int)constant;
        } else {
            free(name);
            return 0;
        }
    }
    free(name);
    return 1;
}

static int kind_value(Unit *unit, const Line *line, size_t begin, size_t end, int *value,
                      Type *kind_type) {
    const char *error_at = NULL;
    F2cExpr *expression;
    int64_t constant;
    *kind_type = f2c_kind_type_from_tokens(unit, line, begin, end);
    if (end == begin + 1U && named_kind_value(unit, &line->tokens[begin], value))
        return 1;
    expression = begin < end ? f2c_parse_expression_tokens(unit, &line->tokens[begin], end - begin,
                                                           line->text, &error_at)
                             : NULL;
    if (expression != NULL && error_at == NULL &&
        f2c_evaluate_integer_constant(unit, expression, &constant) && constant > 0 &&
        constant <= INT_MAX) {
        *value = (int)constant;
        f2c_expr_free(expression);
        return 1;
    }
    if (*kind_type != TYPE_UNKNOWN) {
        *value = f2c_default_kind(*kind_type);
        f2c_expr_free(expression);
        return 1;
    }
    f2c_expr_free(expression);
    return 0;
}

static int set_character_length(Context *context, const Line *line, size_t begin, size_t end,
                                F2cDeclarationTypeSpec *specification) {
    if (begin >= end || specification->character_length != NULL) {
        if (context != NULL)
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                      begin < line->token_count ? &line->tokens[begin] : NULL, 1,
                                      "invalid or duplicate CHARACTER length selector");
        return 0;
    }
    specification->character_length_syntax = f2c_line_token_range(line, begin, end);
    specification->character_length = f2c_token_range_text(specification->character_length_syntax);
    if (specification->character_length == NULL) {
        if (context != NULL)
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line,
                                      &line->tokens[begin], 1,
                                      "out of memory parsing CHARACTER length");
        return 0;
    }
    return 1;
}

static int parse_selector_item(Context *context, Unit *unit, const Line *line, size_t begin,
                               size_t end, size_t position, F2cDeclarationTypeSpec *specification) {
    size_t equals = SIZE_MAX;
    size_t index;
    const char *key = NULL;
    for (index = begin; index < end; ++index) {
        size_t close;
        if (left_delimiter(line->tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(line->tokens, end, index, &close))
                return 0;
            index = close;
        } else if (line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
                   f2c_token_equals(&line->tokens[index], "=")) {
            equals = index;
            break;
        }
    }
    if (equals != SIZE_MAX) {
        if (equals != begin + 1U || line->tokens[begin].kind != F2C_TOKEN_IDENTIFIER ||
            equals + 1U == end) {
            if (context != NULL)
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[begin], 1,
                                          "malformed type selector keyword");
            return 0;
        }
        if (f2c_token_equals(&line->tokens[begin], "kind"))
            key = "kind";
        else if (f2c_token_equals(&line->tokens[begin], "len"))
            key = "len";
        else {
            if (context != NULL)
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[begin], 1, "unknown type selector keyword");
            return 0;
        }
        begin = equals + 1U;
    }
    if ((key != NULL && strcmp(key, "kind") == 0) ||
        (key == NULL && specification->type != TYPE_CHARACTER && position == 0U)) {
        Type kind_type = TYPE_UNKNOWN;
        if (specification->kind != 0 ||
            !kind_value(unit, line, begin, end, &specification->kind, &kind_type)) {
            if (context != NULL)
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                          &line->tokens[begin], 1,
                                          "kind selector must be a supported positive constant");
            return 0;
        }
        specification->kind_type = kind_type;
        return 1;
    }
    if ((key != NULL && strcmp(key, "len") == 0) ||
        (key == NULL && specification->type == TYPE_CHARACTER && position == 0U)) {
        if (specification->type != TYPE_CHARACTER) {
            if (context != NULL)
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[begin], 1,
                                          "LEN selector is valid only for CHARACTER");
            return 0;
        }
        return set_character_length(context, line, begin, end, specification);
    }
    if (context != NULL)
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[begin], 1,
                                  "unsupported or misplaced type selector");
    return 0;
}

static int parse_parenthesized_selector(Context *context, Unit *unit, const Line *line, size_t open,
                                        size_t close, F2cDeclarationTypeSpec *specification) {
    size_t begin = open + 1U;
    size_t index;
    size_t position = 0U;
    if (begin == close)
        return 0;
    for (index = begin; index <= close; ++index) {
        size_t nested_close;
        const int at_end = index == close;
        if (!at_end && left_delimiter(line->tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(line->tokens, close, index, &nested_close))
                return 0;
            index = nested_close;
            continue;
        }
        if (at_end || line->tokens[index].kind == F2C_TOKEN_COMMA) {
            if (begin == index ||
                !parse_selector_item(context, unit, line, begin, index, position, specification))
                return 0;
            ++position;
            begin = index + 1U;
        }
    }
    return 1;
}

static int parse_legacy_selector(Context *context, Unit *unit, const Line *line, size_t star,
                                 F2cDeclarationTypeSpec *specification) {
    size_t begin = star + 1U;
    size_t end;
    size_t close;
    if (begin >= line->token_count)
        return 0;
    if (line->tokens[begin].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!f2c_token_matching_delimiter(line->tokens, line->token_count, begin, &close))
            return 0;
        end = close;
        specification->end = close + 1U;
        ++begin;
    } else {
        end = begin + 1U;
        specification->end = end;
    }
    if (specification->type == TYPE_CHARACTER)
        return set_character_length(context, line, begin, end, specification);
    if (!kind_value(unit, line, begin, end, &specification->kind, &specification->kind_type)) {
        if (context != NULL)
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[begin],
                                      1,
                                      "legacy kind selector must be a supported positive constant");
        return 0;
    }
    if ((specification->type == TYPE_COMPLEX || specification->type == TYPE_DOUBLE_COMPLEX) &&
        specification->kind > 0)
        specification->kind /= 2;
    return 1;
}

int f2c_parse_type_spec_tokens(Context *context, Unit *unit, const Line *line, size_t begin,
                               F2cDeclarationTypeSpec *specification) {
    size_t index = begin;
    size_t close;
    memset(specification, 0, sizeof(*specification));
    specification->type = TYPE_UNKNOWN;
    specification->kind_type = TYPE_UNKNOWN;
    if (line == NULL || begin >= line->token_count)
        return 0;
    if (f2c_line_token_equals(line, index, "double") &&
        f2c_line_token_equals(line, index + 1U, "precision")) {
        specification->type = TYPE_DOUBLE;
        index += 2U;
    } else if (f2c_line_token_equals(line, index, "double") &&
               f2c_line_token_equals(line, index + 1U, "complex")) {
        specification->type = TYPE_DOUBLE_COMPLEX;
        index += 2U;
    } else if (f2c_line_token_equals(line, index, "integer")) {
        specification->type = TYPE_INTEGER;
        ++index;
    } else if (f2c_line_token_equals(line, index, "real")) {
        specification->type = TYPE_REAL;
        ++index;
    } else if (f2c_line_token_equals(line, index, "complex")) {
        specification->type = TYPE_COMPLEX;
        ++index;
    } else if (f2c_line_token_equals(line, index, "logical")) {
        specification->type = TYPE_LOGICAL;
        ++index;
    } else if (f2c_line_token_equals(line, index, "character")) {
        specification->type = TYPE_CHARACTER;
        ++index;
    } else if ((f2c_line_token_equals(line, index, "type") ||
                f2c_line_token_equals(line, index, "class")) &&
               index + 1U < line->token_count &&
               line->tokens[index + 1U].kind == F2C_TOKEN_LEFT_PAREN) {
        specification->polymorphic = f2c_line_token_equals(line, index, "class");
        specification->type = TYPE_DERIVED;
        if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index + 1U, &close) ||
            close != index + 3U ||
            (line->tokens[index + 2U].kind != F2C_TOKEN_IDENTIFIER &&
             !(specification->polymorphic && line->tokens[index + 2U].kind == F2C_TOKEN_OPERATOR &&
               f2c_token_equals(&line->tokens[index + 2U], "*")))) {
            if (context != NULL)
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[index], 1,
                                          "malformed derived type specification");
            return 0;
        }
        specification->derived_type_name = f2c_token_text(&line->tokens[index + 2U]);
        specification->end = close + 1U;
        return specification->derived_type_name != NULL;
    } else {
        return 0;
    }
    specification->end = index;
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close) ||
            !parse_parenthesized_selector(context, unit, line, index, close, specification))
            return 0;
        specification->end = close + 1U;
    } else if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
               f2c_token_equals(&line->tokens[index], "*") &&
               !parse_legacy_selector(context, unit, line, index, specification)) {
        return 0;
    }
    if (specification->kind == 0)
        specification->kind = f2c_default_kind(specification->type);
    if ((specification->type == TYPE_REAL || specification->type == TYPE_COMPLEX) &&
        (specification->kind == 8 || specification->kind_type == TYPE_DOUBLE))
        specification->type = specification->type == TYPE_REAL ? TYPE_DOUBLE : TYPE_DOUBLE_COMPLEX;
    return 1;
}

void f2c_release_type_spec(F2cDeclarationTypeSpec *specification) {
    if (specification == NULL)
        return;
    free(specification->derived_type_name);
    free(specification->character_length);
    memset(specification, 0, sizeof(*specification));
}
