#include "frontend/declaration/private.h"

#include <stdlib.h>
#include <string.h>

typedef enum F2cEntityAttribute {
    F2C_ENTITY_ATTRIBUTE_NONE,
    F2C_ENTITY_ATTRIBUTE_OPTIONAL,
    F2C_ENTITY_ATTRIBUTE_CONTIGUOUS,
    F2C_ENTITY_ATTRIBUTE_TARGET,
    F2C_ENTITY_ATTRIBUTE_VALUE,
    F2C_ENTITY_ATTRIBUTE_ASYNCHRONOUS,
    F2C_ENTITY_ATTRIBUTE_VOLATILE
} F2cEntityAttribute;

static F2cEntityAttribute entity_attribute(const Line *line, size_t index) {
    const size_t next = index + 1U;
    if (line == NULL || next >= line->token_count ||
        (line->tokens[next].kind != F2C_TOKEN_DOUBLE_COLON &&
         line->tokens[next].kind != F2C_TOKEN_IDENTIFIER))
        return F2C_ENTITY_ATTRIBUTE_NONE;
    if (f2c_line_token_equals(line, index, "optional"))
        return F2C_ENTITY_ATTRIBUTE_OPTIONAL;
    if (f2c_line_token_equals(line, index, "contiguous"))
        return F2C_ENTITY_ATTRIBUTE_CONTIGUOUS;
    if (f2c_line_token_equals(line, index, "target"))
        return F2C_ENTITY_ATTRIBUTE_TARGET;
    if (f2c_line_token_equals(line, index, "value"))
        return F2C_ENTITY_ATTRIBUTE_VALUE;
    if (f2c_line_token_equals(line, index, "asynchronous"))
        return F2C_ENTITY_ATTRIBUTE_ASYNCHRONOUS;
    if (f2c_line_token_equals(line, index, "volatile"))
        return F2C_ENTITY_ATTRIBUTE_VOLATILE;
    return F2C_ENTITY_ATTRIBUTE_NONE;
}

static const char *attribute_name(F2cEntityAttribute attribute) {
    switch (attribute) {
    case F2C_ENTITY_ATTRIBUTE_OPTIONAL:
        return "OPTIONAL";
    case F2C_ENTITY_ATTRIBUTE_CONTIGUOUS:
        return "CONTIGUOUS";
    case F2C_ENTITY_ATTRIBUTE_TARGET:
        return "TARGET";
    case F2C_ENTITY_ATTRIBUTE_VALUE:
        return "VALUE";
    case F2C_ENTITY_ATTRIBUTE_ASYNCHRONOUS:
        return "ASYNCHRONOUS";
    case F2C_ENTITY_ATTRIBUTE_VOLATILE:
        return "VOLATILE";
    case F2C_ENTITY_ATTRIBUTE_NONE:
    default:
        return "attribute";
    }
}

static int *attribute_storage(Symbol *symbol, F2cEntityAttribute attribute) {
    switch (attribute) {
    case F2C_ENTITY_ATTRIBUTE_OPTIONAL:
        return &symbol->optional;
    case F2C_ENTITY_ATTRIBUTE_CONTIGUOUS:
        return &symbol->contiguous;
    case F2C_ENTITY_ATTRIBUTE_TARGET:
        return &symbol->target;
    case F2C_ENTITY_ATTRIBUTE_VALUE:
        return &symbol->value;
    case F2C_ENTITY_ATTRIBUTE_ASYNCHRONOUS:
        return &symbol->asynchronous;
    case F2C_ENTITY_ATTRIBUTE_VOLATILE:
        return &symbol->volatile_entity;
    case F2C_ENTITY_ATTRIBUTE_NONE:
    default:
        return NULL;
    }
}

void f2c_parse_entity_attribute_declaration(Context *context, Unit *unit, Line *source_line) {
    size_t index = source_line != NULL && source_line->token_count > 1U &&
                           source_line->tokens[0].kind == F2C_TOKEN_NUMBER
                       ? 1U
                       : 0U;
    const F2cEntityAttribute attribute = entity_attribute(source_line, index);
    const char *name = attribute_name(attribute);
    if (attribute == F2C_ENTITY_ATTRIBUTE_NONE)
        return;
    ++index;
    if (index < source_line->token_count &&
        source_line->tokens[index].kind == F2C_TOKEN_DOUBLE_COLON)
        ++index;
    if (index == source_line->token_count) {
        f2c_diagnostic(context, source_line->number, 1, "%s declaration has no entities", name);
        return;
    }
    while (index < source_line->token_count) {
        const F2cToken *token = &source_line->tokens[index];
        char *entity_name;
        Symbol *symbol;
        int *storage;
        if (token->kind != F2C_TOKEN_IDENTIFIER) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line, token, 1,
                                      "malformed %s declaration entity", name);
            return;
        }
        entity_name = f2c_token_text(token);
        ++index;
        symbol = entity_name != NULL ? f2c_ensure_symbol_impl(unit, entity_name) : NULL;
        free(entity_name);
        if (symbol == NULL) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, source_line, token, 1,
                                      "out of memory in %s declaration", name);
            return;
        }
        storage = attribute_storage(symbol, attribute);
        if (storage == NULL)
            return;
        if (*storage) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, source_line, token, 1,
                                      "duplicate %s attribute for '%s'", name, symbol->name);
        } else {
            *storage = 1;
            if (symbol->declaration_line == 0U) {
                symbol->declaration_line = source_line->number;
                symbol->declaration_span = token->span;
            }
        }
        if (attribute == F2C_ENTITY_ATTRIBUTE_TARGET && index < source_line->token_count &&
            source_line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN) {
            size_t close;
            if (!f2c_token_matching_delimiter(source_line->tokens, source_line->token_count, index,
                                              &close)) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                          &source_line->tokens[index], 1,
                                          "malformed TARGET array specification");
                return;
            }
            if (symbol->rank != 0U) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, source_line, token, 1,
                                          "duplicate array specification for '%s'", symbol->name);
                return;
            }
            if (!f2c_parse_dimensions_tokens(context, unit, symbol, source_line, index, close))
                return;
            index = close + 1U;
        }
        if (index == source_line->token_count)
            break;
        if (source_line->tokens[index].kind != F2C_TOKEN_COMMA ||
            ++index == source_line->token_count) {
            f2c_diagnostic(context, source_line->number, 1, "malformed %s declaration entity list",
                           name);
            return;
        }
    }
}
