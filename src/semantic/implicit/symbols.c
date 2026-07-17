#include "internal/f2c.h"
#include "semantic/implicit/private.h"

#include <stdlib.h>
#include <string.h>

static int is_fortran_keyword(const char *name) {
    static const char *const keywords[] = {
        "allocate",   "assign",  "backspace", "block",     "call",      "case",   "character",
        "class",      "close",   "complex",   "contains",  "continue",  "cycle",  "data",
        "deallocate", "default", "do",        "double",    "else",      "elseif", "end",
        "enddo",      "endfile", "endif",     "endselect", "endwhere",  "error",  "exit",
        "external",   "forall",  "format",    "function",  "go",        "goto",   "if",
        "implicit",   "import",  "inquire",   "integer",   "interface", "is",     "logical",
        "namelist",   "none",    "open",      "nullify",   "precision", "print",  "program",
        "read",       "real",    "result",    "return",    "rewind",    "select", "stop",
        "subroutine", "then",    "to",        "type",      "use",       "where",  "while",
        "write",
    };
    size_t index;
    for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); ++index) {
        if (strcmp(name, keywords[index]) == 0)
            return 1;
    }
    return 0;
}

static int token_is_operator(const Line *line, size_t index, const char *text) {
    return index < line->token_count && line->tokens[index].kind == F2C_TOKEN_OPERATOR &&
           f2c_token_equals(&line->tokens[index], text);
}

static int preceded_by_call(const Line *line, size_t index) {
    return index != 0U && f2c_line_token_equals(line, index - 1U, "call");
}

static int is_namelist_group_reference(Unit *unit, const Line *line, size_t index,
                                       const char *name) {
    return index >= 2U && f2c_find_namelist(unit, name) != NULL &&
           token_is_operator(line, index - 1U, "=") &&
           f2c_line_token_equals(line, index - 2U, "nml");
}

static Unit *find_internal_definition(Context *context, const Unit *host, const char *name) {
    const size_t host_index = (size_t)(host - context->units.items);
    size_t unit_index;
    for (unit_index = 0U; unit_index < context->units.count; ++unit_index) {
        Unit *candidate = &context->units.items[unit_index];
        if (candidate->internal && candidate->host_index == host_index &&
            candidate->fortran_name != NULL && strcmp(candidate->fortran_name, name) == 0)
            return candidate;
    }
    return NULL;
}

static Symbol *bind_known_internal(Context *context, Unit *host, Unit *definition,
                                   const char *name) {
    Symbol *symbol = f2c_ensure_symbol(host, name);
    if (symbol == NULL)
        return NULL;
    symbol->external = 1;
    symbol->external_declared = 1;
    symbol->external_subroutine = definition->kind == UNIT_SUBROUTINE;
    if (definition->kind == UNIT_FUNCTION) {
        if (!definition->return_type_explicit) {
            f2c_prepare_implicit_map(context, definition);
            definition->return_type = f2c_implicit_type_for_name(
                definition, definition->result_name != NULL ? definition->result_name : name);
            definition->return_kind = f2c_implicit_kind_for_name(
                definition, definition->result_name != NULL ? definition->result_name : name);
        }
        if (!f2c_copy_function_result_metadata(symbol, definition))
            return NULL;
    }
    return symbol;
}

void f2c_discover_implicit_line_symbols(Context *context, Unit *unit, const Line *line) {
    size_t index;
    size_t start =
        line != NULL && line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U
                                                                                           : 0U;
    size_t parenthesis_depth = 0U;
    if (line == NULL || f2c_declaration_tokens(line) || f2c_line_token_equals(line, start, "use") ||
        f2c_line_token_equals(line, start, "contains") ||
        f2c_line_token_equals(line, start, "format"))
        return;
    for (index = start; index < line->token_count; ++index) {
        const F2cToken *token = &line->tokens[index];
        char *name;
        Symbol *symbol;
        Unit *internal;
        if (token->kind == F2C_TOKEN_LEFT_PAREN || token->kind == F2C_TOKEN_LEFT_BRACKET ||
            token->kind == F2C_TOKEN_ARRAY_BEGIN) {
            ++parenthesis_depth;
            continue;
        }
        if (token->kind == F2C_TOKEN_RIGHT_PAREN || token->kind == F2C_TOKEN_RIGHT_BRACKET ||
            token->kind == F2C_TOKEN_ARRAY_END) {
            if (parenthesis_depth != 0U)
                --parenthesis_depth;
            continue;
        }
        if (token->kind != F2C_TOKEN_IDENTIFIER)
            continue;
        name = f2c_token_text(token);
        if (name == NULL) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, token, 1,
                                      "out of memory discovering implicit symbols");
            return;
        }
        if (name[0] == '_' || is_fortran_keyword(name) || f2c_is_intrinsic_name(name) ||
            f2c_find_derived_type(unit, name) != NULL ||
            (index != 0U && line->tokens[index - 1U].kind == F2C_TOKEN_PERCENT) ||
            is_namelist_group_reference(unit, line, index, name) ||
            (index + 1U < line->token_count && token_is_operator(line, index + 1U, "=") &&
             parenthesis_depth != 0U && f2c_find_symbol(unit, name) == NULL) ||
            (index + 1U < line->token_count && line->tokens[index + 1U].kind == F2C_TOKEN_COLON) ||
            (unit->name != NULL && strcmp(name, unit->name) == 0) ||
            (unit->fortran_name != NULL && strcmp(name, unit->fortran_name) == 0)) {
            free(name);
            continue;
        }
        symbol = f2c_find_symbol(unit, name);
        internal = find_internal_definition(context, unit, name);
        if (internal != NULL) {
            symbol = bind_known_internal(context, unit, internal, name);
        } else if (index + 1U < line->token_count &&
                   line->tokens[index + 1U].kind == F2C_TOKEN_LEFT_PAREN &&
                   (symbol == NULL || (symbol->rank == 0U && symbol->type != TYPE_CHARACTER))) {
            symbol = f2c_ensure_symbol(unit, name);
            if (symbol != NULL && !preceded_by_call(line, index)) {
                symbol->external = 1;
                symbol->external_subroutine = 0;
            }
        } else if (!preceded_by_call(line, index)) {
            symbol = f2c_ensure_symbol(unit, name);
        }
        if (symbol == NULL) {
            f2c_diagnostic_token_code(context, F2C_DIAGNOSTIC_OUT_OF_MEMORY, line, token, 1,
                                      "out of memory discovering implicit symbol '%s'", name);
            free(name);
            return;
        }
        if (symbol->first_seen_line == 0U)
            symbol->first_seen_line =
                token->span.begin.line != 0U ? token->span.begin.line : line->number;
        free(name);
    }
}
