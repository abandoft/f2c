#include "ast/declaration/procedure.h"

#include <stdlib.h>
#include <string.h>

static int set_error(F2cProcedureDeclarationSyntax *syntax, F2cProcedureDeclarationError error,
                     const F2cToken *token) {
    syntax->error = error;
    syntax->error_token = token;
    return 0;
}

static int tokens_equal(const F2cToken *left, const F2cToken *right) {
    size_t index;
    if (left == NULL || right == NULL || left->length != right->length)
        return 0;
    for (index = 0U; index < left->length; ++index) {
        unsigned char a = (unsigned char)left->begin[index];
        unsigned char b = (unsigned char)right->begin[index];
        if (a >= (unsigned char)'A' && a <= (unsigned char)'Z')
            a = (unsigned char)(a - (unsigned char)'A' + (unsigned char)'a');
        if (b >= (unsigned char)'A' && b <= (unsigned char)'Z')
            b = (unsigned char)(b - (unsigned char)'A' + (unsigned char)'a');
        if (a != b)
            return 0;
    }
    return 1;
}

static size_t find_double_colon(const Line *line, size_t begin) {
    size_t index = begin;
    while (index < line->token_count) {
        size_t close;
        if (line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON)
            return index;
        if (line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN ||
            line->tokens[index].kind == F2C_TOKEN_LEFT_BRACKET ||
            line->tokens[index].kind == F2C_TOKEN_ARRAY_BEGIN) {
            if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close))
                return SIZE_MAX;
            index = close + 1U;
        } else {
            ++index;
        }
    }
    return SIZE_MAX;
}

static int set_once(F2cProcedureDeclarationSyntax *syntax, const F2cToken **slot,
                    const F2cToken *token) {
    if (*slot != NULL)
        return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_DUPLICATE_ATTRIBUTE, token);
    *slot = token;
    return 1;
}

static int parse_intent(const Line *line, size_t *index, size_t end,
                        F2cProcedureDeclarationSyntax *syntax) {
    size_t close;
    const F2cToken *attribute = &line->tokens[*index];
    if (!set_once(syntax, &syntax->intent_attribute, attribute))
        return 0;
    if (*index + 1U >= end || line->tokens[*index + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, end, *index + 1U, &close) ||
        close != *index + 3U || line->tokens[*index + 2U].kind != F2C_TOKEN_IDENTIFIER)
        return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_INTENT, attribute);
    if (f2c_token_equals(&line->tokens[*index + 2U], "in"))
        syntax->intent = F2C_PROCEDURE_INTENT_IN;
    else if (f2c_token_equals(&line->tokens[*index + 2U], "out"))
        syntax->intent = F2C_PROCEDURE_INTENT_OUT;
    else if (f2c_token_equals(&line->tokens[*index + 2U], "inout"))
        syntax->intent = F2C_PROCEDURE_INTENT_INOUT;
    else
        return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_INTENT,
                         &line->tokens[*index + 2U]);
    *index = close + 1U;
    return 1;
}

static int parse_attribute(const Line *line, size_t *index, size_t end,
                           F2cProcedureDeclarationSyntax *syntax) {
    const F2cToken *attribute = &line->tokens[*index];
    if (attribute->kind != F2C_TOKEN_IDENTIFIER)
        return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE, attribute);
    if (f2c_token_equals(attribute, "optional")) {
        ++*index;
        return set_once(syntax, &syntax->optional_attribute, attribute);
    }
    if (f2c_token_equals(attribute, "pointer")) {
        ++*index;
        return set_once(syntax, &syntax->pointer_attribute, attribute);
    }
    if (f2c_token_equals(attribute, "nopass")) {
        ++*index;
        return set_once(syntax, &syntax->nopass_attribute, attribute);
    }
    if (f2c_token_equals(attribute, "pass")) {
        ++*index;
        return set_once(syntax, &syntax->pass_attribute, attribute);
    }
    if (f2c_token_equals(attribute, "public")) {
        ++*index;
        if (syntax->private_attribute != NULL)
            return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_CONFLICTING_ACCESS, attribute);
        return set_once(syntax, &syntax->public_attribute, attribute);
    }
    if (f2c_token_equals(attribute, "private")) {
        ++*index;
        if (syntax->public_attribute != NULL)
            return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_CONFLICTING_ACCESS, attribute);
        return set_once(syntax, &syntax->private_attribute, attribute);
    }
    if (f2c_token_equals(attribute, "intent"))
        return parse_intent(line, index, end, syntax);
    return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_UNKNOWN_ATTRIBUTE, attribute);
}

static int parse_attributes(const Line *line, size_t begin, size_t end,
                            F2cProcedureDeclarationSyntax *syntax) {
    size_t index = begin;
    while (index < end) {
        if (line->tokens[index].kind != F2C_TOKEN_COMMA)
            return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE_SEPARATOR,
                             &line->tokens[index]);
        ++index;
        if (index == end)
            return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE,
                             &line->tokens[index - 1U]);
        if (!parse_attribute(line, &index, end, syntax))
            return 0;
    }
    return 1;
}

static int append_entity(F2cProcedureDeclarationSyntax *syntax, const F2cToken *entity) {
    const F2cToken **replacement;
    size_t index;
    for (index = 0U; index < syntax->entity_count; ++index) {
        if (tokens_equal(syntax->entities[index], entity))
            return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_DUPLICATE_ENTITY, entity);
    }
    if (syntax->entity_count == SIZE_MAX / sizeof(*replacement))
        return -1;
    replacement = (const F2cToken **)realloc(syntax->entities,
                                             (syntax->entity_count + 1U) * sizeof(*replacement));
    if (replacement == NULL)
        return -1;
    syntax->entities = replacement;
    syntax->entities[syntax->entity_count++] = entity;
    return 1;
}

static int parse_entities(const Line *line, size_t begin, F2cProcedureDeclarationSyntax *syntax) {
    size_t index = begin;
    if (index == line->token_count)
        return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_ENTITY_LIST,
                         &line->tokens[begin - 1U]);
    while (index < line->token_count) {
        int appended;
        if (line->tokens[index].kind != F2C_TOKEN_IDENTIFIER)
            return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_ENTITY, &line->tokens[index]);
        appended = append_entity(syntax, &line->tokens[index]);
        if (appended <= 0)
            return appended;
        ++index;
        if (index == line->token_count)
            return 1;
        if (line->tokens[index].kind != F2C_TOKEN_COMMA)
            return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_ENTITY_LIST,
                             &line->tokens[index]);
        ++index;
        if (index == line->token_count)
            return set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_ENTITY_LIST,
                             &line->tokens[index - 1U]);
    }
    return 1;
}

F2cProcedureDeclarationStatus
f2c_parse_procedure_declaration_syntax(const Line *line, F2cProcedureDeclarationSyntax *syntax) {
    size_t start;
    size_t open;
    size_t close;
    size_t double_colon;
    size_t entities_begin;
    int parsed;
    if (syntax == NULL)
        return F2C_PROCEDURE_DECLARATION_INVALID;
    memset(syntax, 0, sizeof(*syntax));
    if (line == NULL || line->token_count == 0U)
        return F2C_PROCEDURE_DECLARATION_NOT_MATCHED;
    start = line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
    if (!f2c_line_token_equals(line, start, "procedure"))
        return F2C_PROCEDURE_DECLARATION_NOT_MATCHED;
    syntax->span = f2c_source_span_cover(&line->tokens[start].span,
                                         &line->tokens[line->token_count - 1U].span);
    open = start + 1U;
    if (open >= line->token_count || line->tokens[open].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, open, &close) ||
        close != open + 2U || line->tokens[open + 1U].kind != F2C_TOKEN_IDENTIFIER) {
        set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_INTERFACE,
                  open < line->token_count ? &line->tokens[open] : &line->tokens[start]);
        return F2C_PROCEDURE_DECLARATION_INVALID;
    }
    syntax->interface_name = &line->tokens[open + 1U];
    double_colon = find_double_colon(line, close + 1U);
    if (double_colon != SIZE_MAX) {
        if (!parse_attributes(line, close + 1U, double_colon, syntax))
            return F2C_PROCEDURE_DECLARATION_INVALID;
        entities_begin = double_colon + 1U;
    } else {
        entities_begin = close + 1U;
        if (entities_begin < line->token_count &&
            line->tokens[entities_begin].kind == F2C_TOKEN_COMMA) {
            set_error(syntax, F2C_PROCEDURE_DECLARATION_ERROR_ATTRIBUTE_SEPARATOR,
                      &line->tokens[entities_begin]);
            return F2C_PROCEDURE_DECLARATION_INVALID;
        }
    }
    parsed = parse_entities(line, entities_begin, syntax);
    if (parsed < 0)
        return F2C_PROCEDURE_DECLARATION_NO_MEMORY;
    if (!parsed)
        return F2C_PROCEDURE_DECLARATION_INVALID;
    return F2C_PROCEDURE_DECLARATION_PARSED;
}

void f2c_procedure_declaration_syntax_discard(F2cProcedureDeclarationSyntax *syntax) {
    if (syntax == NULL)
        return;
    free(syntax->entities);
    memset(syntax, 0, sizeof(*syntax));
}
