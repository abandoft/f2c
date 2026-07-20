#include "frontend/declaration/private.h"

#include <stdlib.h>
#include <string.h>

enum F2cDeclarationAttributeFlag {
    F2C_DECL_ALLOCATABLE = 1U << 0,
    F2C_DECL_EXTERNAL = 1U << 1,
    F2C_DECL_OPTIONAL = 1U << 2,
    F2C_DECL_PARAMETER = 1U << 3,
    F2C_DECL_POINTER = 1U << 4,
    F2C_DECL_SAVE = 1U << 5,
    F2C_DECL_TARGET = 1U << 6,
    F2C_DECL_INTENT = 1U << 7,
    F2C_DECL_DIMENSION = 1U << 8,
    F2C_DECL_PUBLIC = 1U << 9,
    F2C_DECL_PRIVATE = 1U << 10,
    F2C_DECL_CONTIGUOUS = 1U << 11
};

typedef struct F2cDeclarationAttributes {
    unsigned int flags;
    F2cIntent intent;
    size_t dimension_open;
    size_t dimension_close;
    F2cAccessibility access;
    const F2cToken *access_token;
} F2cDeclarationAttributes;

typedef struct F2cEntitySpec {
    size_t begin;
    size_t end;
    size_t dimension_open;
    size_t dimension_close;
    size_t length_begin;
    size_t length_end;
    size_t initializer_begin;
    size_t initializer_end;
    int pointer_initializer;
} F2cEntitySpec;

static int left_delimiter(F2cTokenKind kind) {
    return kind == F2C_TOKEN_LEFT_PAREN || kind == F2C_TOKEN_LEFT_BRACKET ||
           kind == F2C_TOKEN_ARRAY_BEGIN;
}

static int token_operator(const F2cToken *token, const char *text) {
    return token != NULL && token->kind == F2C_TOKEN_OPERATOR && f2c_token_equals(token, text);
}

static int next_top_level_range(const Line *line, size_t limit, size_t *position, size_t *begin,
                                size_t *end) {
    size_t index;
    if (*position >= limit)
        return 0;
    *begin = *position;
    for (index = *position; index < limit; ++index) {
        size_t close;
        if (left_delimiter(line->tokens[index].kind)) {
            if (!f2c_token_matching_delimiter(line->tokens, limit, index, &close))
                return -1;
            index = close;
        } else if (line->tokens[index].kind == F2C_TOKEN_COMMA) {
            if (*begin == index)
                return -1;
            *end = index;
            *position = index + 1U;
            return 1;
        }
    }
    *end = limit;
    *position = limit;
    return *begin == *end ? -1 : 1;
}

static unsigned int simple_attribute_flag(const F2cToken *token) {
    if (f2c_token_equals(token, "allocatable"))
        return F2C_DECL_ALLOCATABLE;
    if (f2c_token_equals(token, "external"))
        return F2C_DECL_EXTERNAL;
    if (f2c_token_equals(token, "optional"))
        return F2C_DECL_OPTIONAL;
    if (f2c_token_equals(token, "parameter"))
        return F2C_DECL_PARAMETER;
    if (f2c_token_equals(token, "pointer"))
        return F2C_DECL_POINTER;
    if (f2c_token_equals(token, "save"))
        return F2C_DECL_SAVE;
    if (f2c_token_equals(token, "target"))
        return F2C_DECL_TARGET;
    if (f2c_token_equals(token, "contiguous"))
        return F2C_DECL_CONTIGUOUS;
    if (f2c_token_equals(token, "public"))
        return F2C_DECL_PUBLIC;
    if (f2c_token_equals(token, "private"))
        return F2C_DECL_PRIVATE;
    return 0U;
}

static int record_attribute(Context *context, const Line *line, const F2cToken *token,
                            unsigned int flag, F2cDeclarationAttributes *attributes) {
    if ((attributes->flags & flag) != 0U) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, token, 1,
                                  "duplicate declaration attribute");
        return 0;
    }
    attributes->flags |= flag;
    return 1;
}

static int parse_intent_attribute(Context *context, const Line *line, size_t begin, size_t end,
                                  F2cDeclarationAttributes *attributes) {
    size_t close;
    if (end != begin + 4U || line->tokens[begin + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, end, begin + 1U, &close) ||
        close != begin + 3U || line->tokens[begin + 2U].kind != F2C_TOKEN_IDENTIFIER ||
        !record_attribute(context, line, &line->tokens[begin], F2C_DECL_INTENT, attributes))
        return 0;
    if (f2c_token_equals(&line->tokens[begin + 2U], "in"))
        attributes->intent = F2C_INTENT_IN;
    else if (f2c_token_equals(&line->tokens[begin + 2U], "out"))
        attributes->intent = F2C_INTENT_OUT;
    else if (f2c_token_equals(&line->tokens[begin + 2U], "inout"))
        attributes->intent = F2C_INTENT_INOUT;
    else {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[begin + 2U],
                                  1, "INTENT must be IN, OUT, or INOUT");
        return 0;
    }
    return 1;
}

static int parse_dimension_attribute(Context *context, const Line *line, size_t begin, size_t end,
                                     F2cDeclarationAttributes *attributes) {
    size_t close;
    if (begin + 2U >= end || line->tokens[begin + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, end, begin + 1U, &close) || close + 1U != end) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[begin], 1,
                                  "malformed DIMENSION attribute");
        return 0;
    }
    if (!record_attribute(context, line, &line->tokens[begin], F2C_DECL_DIMENSION, attributes))
        return 0;
    attributes->dimension_open = begin + 1U;
    attributes->dimension_close = close;
    return 1;
}

static int parse_attribute(Context *context, const Line *line, size_t begin, size_t end,
                           F2cDeclarationAttributes *attributes) {
    unsigned int flag;
    if (begin >= end || line->tokens[begin].kind != F2C_TOKEN_IDENTIFIER) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                  begin < line->token_count ? &line->tokens[begin] : NULL, 1,
                                  "malformed declaration attribute");
        return 0;
    }
    if (f2c_token_equals(&line->tokens[begin], "intent"))
        return parse_intent_attribute(context, line, begin, end, attributes);
    if (f2c_token_equals(&line->tokens[begin], "dimension"))
        return parse_dimension_attribute(context, line, begin, end, attributes);
    flag = simple_attribute_flag(&line->tokens[begin]);
    if (flag != 0U && end == begin + 1U) {
        if ((flag == F2C_DECL_PUBLIC && (attributes->flags & F2C_DECL_PRIVATE) != 0U) ||
            (flag == F2C_DECL_PRIVATE && (attributes->flags & F2C_DECL_PUBLIC) != 0U)) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[begin],
                                      1,
                                      "declaration cannot have both PUBLIC and PRIVATE attributes");
            return 0;
        }
        if (!record_attribute(context, line, &line->tokens[begin], flag, attributes))
            return 0;
        if (flag == F2C_DECL_PUBLIC || flag == F2C_DECL_PRIVATE) {
            attributes->access =
                flag == F2C_DECL_PUBLIC ? F2C_ACCESSIBILITY_PUBLIC : F2C_ACCESSIBILITY_PRIVATE;
            attributes->access_token = &line->tokens[begin];
        }
        return 1;
    }
    f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, line, &line->tokens[begin], 1,
                              "unsupported declaration attribute");
    return 0;
}

static int parse_attributes(Context *context, const Line *line, size_t begin, size_t end,
                            F2cDeclarationAttributes *attributes) {
    size_t position = begin;
    memset(attributes, 0, sizeof(*attributes));
    attributes->dimension_open = SIZE_MAX;
    attributes->dimension_close = SIZE_MAX;
    while (position < end) {
        size_t attribute_begin;
        size_t attribute_end;
        size_t index;
        if (line->tokens[position].kind != F2C_TOKEN_COMMA) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[position],
                                      1, "declaration attributes must follow a comma");
            return 0;
        }
        attribute_begin = ++position;
        for (index = position; index < end; ++index) {
            size_t close;
            if (left_delimiter(line->tokens[index].kind)) {
                if (!f2c_token_matching_delimiter(line->tokens, end, index, &close))
                    return 0;
                index = close;
            } else if (line->tokens[index].kind == F2C_TOKEN_COMMA) {
                break;
            }
        }
        attribute_end = index;
        position = index;
        if (attribute_begin == attribute_end ||
            !parse_attribute(context, line, attribute_begin, attribute_end, attributes))
            return 0;
    }
    return 1;
}

static int parse_character_entity_length(Context *context, const Line *line, size_t *position,
                                         size_t end, F2cEntitySpec *entity) {
    size_t close;
    size_t begin = *position + 1U;
    if (entity->length_begin != SIZE_MAX || begin >= end) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[*position], 1,
                                  "invalid or duplicate CHARACTER entity length");
        return 0;
    }
    if (line->tokens[begin].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!f2c_token_matching_delimiter(line->tokens, end, begin, &close) ||
            close == begin + 1U) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[begin], 1,
                                      "malformed CHARACTER entity length");
            return 0;
        }
        entity->length_begin = begin + 1U;
        entity->length_end = close;
        *position = close + 1U;
    } else {
        entity->length_begin = begin;
        entity->length_end = begin + 1U;
        *position = begin + 1U;
    }
    return 1;
}

static int parse_entity(Context *context, const Line *line, size_t begin, size_t end, Type type,
                        F2cEntitySpec *entity) {
    size_t position;
    memset(entity, 0, sizeof(*entity));
    entity->begin = begin;
    entity->end = end;
    entity->dimension_open = SIZE_MAX;
    entity->dimension_close = SIZE_MAX;
    entity->length_begin = SIZE_MAX;
    entity->length_end = SIZE_MAX;
    entity->initializer_begin = SIZE_MAX;
    entity->initializer_end = SIZE_MAX;
    if (begin >= end || line->tokens[begin].kind != F2C_TOKEN_IDENTIFIER) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                  begin < line->token_count ? &line->tokens[begin] : NULL, 1,
                                  "invalid or empty declaration target");
        return 0;
    }
    position = begin + 1U;
    while (position < end) {
        if (line->tokens[position].kind == F2C_TOKEN_LEFT_PAREN) {
            size_t close;
            if (entity->dimension_open != SIZE_MAX ||
                !f2c_token_matching_delimiter(line->tokens, end, position, &close)) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[position], 1,
                                          "malformed or duplicate entity array specification");
                return 0;
            }
            entity->dimension_open = position;
            entity->dimension_close = close;
            position = close + 1U;
        } else if (type == TYPE_CHARACTER && token_operator(&line->tokens[position], "*")) {
            if (!parse_character_entity_length(context, line, &position, end, entity))
                return 0;
        } else if (token_operator(&line->tokens[position], "=") ||
                   token_operator(&line->tokens[position], "=>")) {
            entity->pointer_initializer = token_operator(&line->tokens[position], "=>");
            entity->initializer_begin = position + 1U;
            entity->initializer_end = end;
            if (entity->initializer_begin == end) {
                f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line,
                                          &line->tokens[position], 1,
                                          "declaration initializer is empty");
                return 0;
            }
            position = end;
        } else {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, line, &line->tokens[position],
                                      1, "unexpected token in declaration entity");
            return 0;
        }
    }
    return 1;
}

static int null_pointer_initializer(const Line *line, const F2cEntitySpec *entity) {
    size_t close;
    const size_t begin = entity->initializer_begin;
    return begin != SIZE_MAX && begin + 2U < entity->initializer_end &&
           f2c_line_token_equals(line, begin, "null") &&
           line->tokens[begin + 1U].kind == F2C_TOKEN_LEFT_PAREN &&
           f2c_token_matching_delimiter(line->tokens, entity->initializer_end, begin + 1U,
                                        &close) &&
           close + 1U == entity->initializer_end;
}

static int assign_string_range(Context *context, const Line *line, size_t begin, size_t end,
                               char **target, F2cTokenRange *syntax, const char *role) {
    char *text;
    if (begin == SIZE_MAX)
        return 1;
    *syntax = f2c_line_token_range(line, begin, end);
    text = f2c_token_range_text(*syntax);
    if (text == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, &line->tokens[begin],
                                  1, "out of memory recording %s", role);
        return 0;
    }
    free(*target);
    *target = text;
    return 1;
}

static int configure_derived_type(Context *context, Unit *unit, const Line *line,
                                  const F2cDeclarationTypeSpec *type_spec, Symbol *symbol) {
    Buffer c_type = {0};
    if (type_spec->derived_type_name == NULL)
        return 0;
    if (strcmp(type_spec->derived_type_name, "*") == 0) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, line, &line->tokens[0], 1,
                                  "unlimited polymorphic CLASS(*) is not yet supported");
        return 0;
    }
    symbol->derived_type = f2c_find_derived_type(unit, type_spec->derived_type_name);
    if (symbol->derived_type == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[0], 1,
                                  "derived type '%s' is not declared in this scope",
                                  type_spec->derived_type_name);
        return 0;
    }
    free(symbol->derived_type_name);
    symbol->derived_type_name = f2c_strdup(type_spec->derived_type_name);
    if (symbol->derived_type->c_name != NULL)
        f2c_buffer_append(&c_type, symbol->derived_type->c_name);
    else
        f2c_buffer_printf(&c_type, "f2c_type_%s_%s", unit->name != NULL ? unit->name : "scope",
                          type_spec->derived_type_name);
    free(symbol->c_type);
    symbol->c_type = f2c_buffer_take(&c_type);
    if (symbol->derived_type_name == NULL || symbol->c_type == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, &line->tokens[0], 1,
                                  "out of memory recording derived type declaration");
        return 0;
    }
    return 1;
}

static int apply_entity(Context *context, Unit *unit, const Line *line,
                        const F2cDeclarationTypeSpec *type_spec,
                        const F2cDeclarationAttributes *attributes, const F2cEntitySpec *entity) {
    char *name = f2c_token_text(&line->tokens[entity->begin]);
    Symbol *symbol = name != NULL ? f2c_ensure_symbol_impl(unit, name) : NULL;
    const unsigned int flags = attributes->flags;
    free(name);
    if (symbol == NULL) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line,
                                  &line->tokens[entity->begin], 1,
                                  "out of memory recording declaration entity");
        return 0;
    }
    if (symbol->type != TYPE_UNKNOWN && symbol->declaration_line != 0U) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                  &line->tokens[entity->begin], 1,
                                  "duplicate explicit type declaration for '%s'", symbol->name);
        return 0;
    }
    symbol->type = type_spec->type;
    symbol->kind = type_spec->kind;
    symbol->kind_type = type_spec->kind_type;
    symbol->value_category =
        (flags & F2C_DECL_PARAMETER) != 0U ? F2C_VALUE_CONSTANT : F2C_VALUE_VARIABLE;
    symbol->declaration_line = line->number;
    symbol->declaration_span = line->tokens[entity->begin].span;
    symbol->allocatable |= (flags & F2C_DECL_ALLOCATABLE) != 0U;
    symbol->pointer |= (flags & F2C_DECL_POINTER) != 0U;
    symbol->contiguous |= (flags & F2C_DECL_CONTIGUOUS) != 0U;
    symbol->polymorphic |= type_spec->polymorphic;
    symbol->target |= (flags & F2C_DECL_TARGET) != 0U;
    symbol->optional |= (flags & F2C_DECL_OPTIONAL) != 0U;
    symbol->parameter |= (flags & F2C_DECL_PARAMETER) != 0U;
    symbol->saved |= (flags & F2C_DECL_SAVE) != 0U;
    if (attributes->access != F2C_ACCESS_UNSPECIFIED) {
        symbol->access = attributes->access;
        symbol->access_span = attributes->access_token->span;
    }
    if ((flags & F2C_DECL_INTENT) != 0U)
        symbol->intent = attributes->intent;
    if ((flags & F2C_DECL_EXTERNAL) != 0U) {
        symbol->external = 1;
        symbol->external_declared = 1;
        symbol->external_subroutine = 0;
    } else if (symbol->external) {
        symbol->external_subroutine = 0;
    }
    if (type_spec->type == TYPE_DERIVED &&
        !configure_derived_type(context, unit, line, type_spec, symbol))
        return 0;
    if (type_spec->type == TYPE_CHARACTER) {
        const size_t length_begin =
            entity->length_begin != SIZE_MAX ? entity->length_begin
            : type_spec->character_length_syntax.count != 0U
                ? (size_t)(type_spec->character_length_syntax.tokens - line->tokens)
                : SIZE_MAX;
        const size_t length_end = entity->length_begin != SIZE_MAX ? entity->length_end
                                  : length_begin != SIZE_MAX
                                      ? length_begin + type_spec->character_length_syntax.count
                                      : SIZE_MAX;
        if (!assign_string_range(context, line, length_begin, length_end, &symbol->character_length,
                                 &symbol->character_length_syntax, "CHARACTER length"))
            return 0;
        symbol->deferred_character =
            symbol->character_length != NULL && strcmp(symbol->character_length, ":") == 0;
        if (symbol->deferred_character && !symbol->allocatable && !symbol->pointer) {
            f2c_diagnostic_token_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[entity->begin], 1,
                "deferred-length CHARACTER entity '%s' must be ALLOCATABLE or POINTER",
                symbol->name);
        }
    }
    if (entity->dimension_open != SIZE_MAX && (attributes->flags & F2C_DECL_DIMENSION) != 0U) {
        f2c_diagnostic_token_code(
            context, F2C_DIAGNOSTIC_SEMANTIC, line, &line->tokens[entity->dimension_open], 1,
            "entity and DIMENSION attribute both specify shape for '%s'", symbol->name);
        return 0;
    }
    if (entity->dimension_open != SIZE_MAX) {
        if (symbol->rank != 0U ||
            !f2c_parse_dimensions_tokens(context, unit, symbol, line, entity->dimension_open,
                                         entity->dimension_close))
            return 0;
    } else if ((attributes->flags & F2C_DECL_DIMENSION) != 0U) {
        if (symbol->rank != 0U ||
            !f2c_parse_dimensions_tokens(context, unit, symbol, line, attributes->dimension_open,
                                         attributes->dimension_close))
            return 0;
    }
    if (entity->pointer_initializer && !symbol->pointer) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SEMANTIC, line,
                                  &line->tokens[entity->initializer_begin - 1U], 1,
                                  "pointer initialization requires the POINTER attribute");
        return 0;
    }
    if (entity->pointer_initializer && !null_pointer_initializer(line, entity)) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_UNSUPPORTED, line,
                                  &line->tokens[entity->initializer_begin], 1,
                                  "non-NULL pointer declaration initialization is not yet "
                                  "supported");
        return 0;
    }
    if (entity->initializer_begin != SIZE_MAX &&
        !assign_string_range(context, line, entity->initializer_begin, entity->initializer_end,
                             &symbol->initializer, &symbol->initializer_syntax,
                             "declaration initializer"))
        return 0;
    if (entity->initializer_begin != SIZE_MAX)
        symbol->kind_type = f2c_kind_type_from_tokens(unit, line, entity->initializer_begin,
                                                      entity->initializer_end);
    f2c_shape_from_symbol(unit, &symbol->shape, symbol);
    return 1;
}

void f2c_parse_entity_declaration(Context *context, Unit *unit, Line *source_line) {
    F2cDeclarationTypeSpec type_spec;
    F2cDeclarationAttributes attributes;
    size_t start = source_line != NULL && source_line->token_count > 1U &&
                           source_line->tokens[0].kind == F2C_TOKEN_NUMBER
                       ? 1U
                       : 0U;
    size_t double_colon;
    size_t entity_begin;
    size_t position;
    memset(&type_spec, 0, sizeof(type_spec));
    if (source_line == NULL)
        return;
    if (!f2c_parse_type_spec_tokens(context, unit, source_line, start, &type_spec)) {
        f2c_release_type_spec(&type_spec);
        return;
    }
    double_colon = f2c_line_find_token(source_line, type_spec.end, F2C_TOKEN_DOUBLE_COLON, NULL);
    if (double_colon != SIZE_MAX) {
        if (!parse_attributes(context, source_line, type_spec.end, double_colon, &attributes))
            goto cleanup;
        if (attributes.access != F2C_ACCESS_UNSPECIFIED && unit->kind != UNIT_MODULE) {
            f2c_diagnostic_token_code(
                context, F2C_DIAGNOSTIC_SEMANTIC, source_line, attributes.access_token, 1,
                "PUBLIC and PRIVATE declaration attributes are valid only in a module");
            goto cleanup;
        }
        entity_begin = double_colon + 1U;
    } else {
        memset(&attributes, 0, sizeof(attributes));
        attributes.dimension_open = SIZE_MAX;
        attributes.dimension_close = SIZE_MAX;
        entity_begin = type_spec.end;
        if (entity_begin < source_line->token_count &&
            source_line->tokens[entity_begin].kind == F2C_TOKEN_COMMA) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      &source_line->tokens[entity_begin], 1,
                                      "declarations with attributes require '::'");
            goto cleanup;
        }
    }
    if (entity_begin >= source_line->token_count) {
        f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                  &source_line->tokens[start], 1,
                                  "type declaration has no entities");
        goto cleanup;
    }
    position = entity_begin;
    while (position < source_line->token_count) {
        size_t begin;
        size_t end;
        F2cEntitySpec entity;
        const int status =
            next_top_level_range(source_line, source_line->token_count, &position, &begin, &end);
        if (status <= 0) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_SYNTAX, source_line,
                                      position < source_line->token_count
                                          ? &source_line->tokens[position]
                                          : &source_line->tokens[entity_begin],
                                      1, "malformed declaration entity list");
            break;
        }
        if (!parse_entity(context, source_line, begin, end, type_spec.type, &entity) ||
            !apply_entity(context, unit, source_line, &type_spec, &attributes, &entity))
            break;
    }

cleanup:
    f2c_release_type_spec(&type_spec);
}

void f2c_parse_declaration(Context *context, Unit *unit, Line *source_line) {
    f2c_parse_entity_declaration(context, unit, source_line);
}
