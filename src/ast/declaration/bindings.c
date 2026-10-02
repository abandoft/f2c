#include "ast/declaration/bindings.h"
#include "ast/declaration/bindings_private.h"

#include "ast/declaration/procedure.h"

#include <stdlib.h>
#include <string.h>

int f2c_binding_name_append(F2cDeclarationBindingsSyntax *syntax, const F2cToken *token) {
    if (syntax->count == syntax->capacity) {
        size_t capacity = syntax->capacity == 0U ? 8U : syntax->capacity * 2U;
        const F2cToken **names;
        if (capacity < syntax->capacity || capacity > SIZE_MAX / sizeof(*names))
            return 0;
        names = (const F2cToken **)realloc(syntax->names, capacity * sizeof(*names));
        if (names == NULL)
            return 0;
        syntax->names = names;
        syntax->capacity = capacity;
    }
    syntax->names[syntax->count++] = token;
    return 1;
}

static int word(const Line *line, size_t index, const char *text) {
    return index < line->token_count && line->tokens[index].kind == F2C_TOKEN_IDENTIFIER &&
           f2c_token_equals(&line->tokens[index], text);
}

static int skip_selector(const Line *line, size_t *index, size_t end) {
    size_t close;
    if (*index < end && line->tokens[*index].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!f2c_token_matching_delimiter(line->tokens, end, *index, &close))
            return 0;
        *index = close + 1U;
    }
    return 1;
}

static int type_spec_end(const Line *line, size_t start, size_t *end) {
    static const char *const intrinsic[] = {"integer", "real", "logical", "complex", "character"};
    size_t index;
    int matched = 0;
    for (index = 0U; index < sizeof(intrinsic) / sizeof(intrinsic[0]); ++index)
        if (word(line, start, intrinsic[index]))
            matched = 1;
    *end = start + 1U;
    if (word(line, start, "double") &&
        (word(line, *end, "precision") || word(line, *end, "complex"))) {
        matched = 1;
        ++*end;
    }
    if ((word(line, start, "type") || word(line, start, "class")) && *end < line->token_count &&
        line->tokens[*end].kind == F2C_TOKEN_LEFT_PAREN)
        matched = 1;
    if (!matched || !skip_selector(line, end, line->token_count))
        return 0;
    if (*end < line->token_count && line->tokens[*end].kind == F2C_TOKEN_OPERATOR &&
        f2c_token_equals(&line->tokens[*end], "*")) {
        ++*end;
        if (*end == line->token_count)
            return 0;
        if (line->tokens[*end].kind == F2C_TOKEN_LEFT_PAREN) {
            if (!skip_selector(line, end, line->token_count))
                return 0;
        } else {
            ++*end;
        }
    }
    return 1;
}

static int list_names(const Line *line, size_t begin, size_t end,
                      F2cDeclarationBindingsSyntax *syntax) {
    F2cTokenRange *items = NULL;
    size_t count = 0U;
    size_t index;
    if (begin >= end)
        return 0;
    if (!f2c_token_range_balanced(&line->tokens[begin], end - begin))
        return 0;
    if (!f2c_token_range_split_top_level(f2c_line_token_range(line, begin, end), F2C_TOKEN_COMMA,
                                         NULL, &items, &count))
        return -1;
    if (count > SIZE_MAX / sizeof(*syntax->names)) {
        free(items);
        return -1;
    }
    syntax->names = (const F2cToken **)malloc(count * sizeof(*syntax->names));
    if (syntax->names == NULL) {
        free(items);
        return -1;
    }
    syntax->capacity = count;
    for (index = 0U; index < count; ++index) {
        if (items[index].count != 0U && items[index].tokens[0].kind == F2C_TOKEN_IDENTIFIER)
            syntax->names[syntax->count++] = &items[index].tokens[0];
    }
    free(items);
    return 1;
}

int f2c_parse_declaration_bindings_syntax(const Line *line, F2cDeclarationBindingsSyntax *syntax) {
    static const char *const attributes[] = {"dimension", "external",    "intrinsic", "save",
                                             "target",    "allocatable", "pointer"};
    size_t start;
    size_t begin;
    size_t end;
    size_t index;
    int matched = 0;
    memset(syntax, 0, sizeof(*syntax));
    if (line == NULL || line->token_count == 0U)
        return 0;
    start = line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
    if (word(line, start, "data") || word(line, start, "common") ||
        word(line, start, "equivalence"))
        return f2c_storage_bindings_syntax(line, start, syntax);
    if (word(line, start, "procedure")) {
        F2cProcedureDeclarationSyntax procedure;
        F2cProcedureDeclarationStatus status =
            f2c_parse_procedure_declaration_syntax(line, &procedure);
        if (status == F2C_PROCEDURE_DECLARATION_PARSED) {
            syntax->names = procedure.entities;
            syntax->count = procedure.entity_count;
            syntax->capacity = procedure.entity_count;
            procedure.entities = NULL;
            procedure.entity_count = 0U;
        }
        f2c_procedure_declaration_syntax_discard(&procedure);
        return status == F2C_PROCEDURE_DECLARATION_PARSED      ? 1
               : status == F2C_PROCEDURE_DECLARATION_NO_MEMORY ? -1
                                                               : 0;
    }
    end = line->token_count;
    if (type_spec_end(line, start, &begin)) {
        size_t separator = f2c_token_range_find_top_level(f2c_line_token_range(line, 0U, end),
                                                          begin, F2C_TOKEN_DOUBLE_COLON, NULL);
        if (separator != SIZE_MAX)
            begin = separator + 1U;
        return list_names(line, begin, end, syntax);
    }
    begin = start + 1U;
    if (word(line, start, "parameter")) {
        size_t close;
        if (begin >= end || line->tokens[begin].kind != F2C_TOKEN_LEFT_PAREN ||
            !f2c_token_matching_delimiter(line->tokens, end, begin, &close))
            return 0;
        return list_names(line, begin + 1U, close, syntax);
    }
    for (index = 0U; index < sizeof(attributes) / sizeof(attributes[0]); ++index)
        if (word(line, start, attributes[index]))
            matched = 1;
    if (!matched)
        return 0;
    if (begin < end && line->tokens[begin].kind == F2C_TOKEN_DOUBLE_COLON)
        ++begin;
    return list_names(line, begin, end, syntax);
}

void f2c_declaration_bindings_syntax_discard(F2cDeclarationBindingsSyntax *syntax) {
    free(syntax->names);
    memset(syntax, 0, sizeof(*syntax));
}
