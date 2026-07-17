#include "frontend/declaration/private.h"

#include <stdlib.h>
#include <string.h>

static int header_attribute(const Line *line, size_t index, Unit *unit) {
    if (f2c_line_token_equals(line, index, "recursive"))
        unit->recursive = 1;
    else if (f2c_line_token_equals(line, index, "pure"))
        unit->pure = 1;
    else if (f2c_line_token_equals(line, index, "elemental"))
        unit->elemental = 1;
    else if (f2c_line_token_equals(line, index, "impure"))
        unit->impure = 1;
    else if (f2c_line_token_equals(line, index, "module"))
        unit->module_procedure = 1;
    else
        return 0;
    return 1;
}

static int append_argument(Unit *unit, char *argument) {
    char **replacement;
    if (unit->argument_count == SIZE_MAX / sizeof(*unit->arguments))
        return 0;
    replacement =
        (char **)realloc(unit->arguments, (unit->argument_count + 1U) * sizeof(*unit->arguments));
    if (replacement == NULL)
        return 0;
    unit->arguments = replacement;
    unit->arguments[unit->argument_count++] = argument;
    return 1;
}

static int parse_arguments(const Line *line, size_t open, size_t close, Unit *unit) {
    size_t begin = open + 1U;
    size_t index = begin;
    size_t depth = 0U;
    if (begin == close)
        return 1;
    while (index <= close) {
        const int at_end = index == close;
        const F2cTokenKind kind = at_end ? F2C_TOKEN_END : line->tokens[index].kind;
        if (at_end || (kind == F2C_TOKEN_COMMA && depth == 0U)) {
            F2cTokenRange range;
            char *argument;
            if (begin == index)
                return 0;
            range = f2c_line_token_range(line, begin, index);
            argument = f2c_token_range_text(range);
            if (argument == NULL || !append_argument(unit, argument)) {
                free(argument);
                return 0;
            }
            begin = index + 1U;
        } else if (kind == F2C_TOKEN_LEFT_PAREN || kind == F2C_TOKEN_LEFT_BRACKET ||
                   kind == F2C_TOKEN_ARRAY_BEGIN) {
            ++depth;
        } else if ((kind == F2C_TOKEN_RIGHT_PAREN || kind == F2C_TOKEN_RIGHT_BRACKET ||
                    kind == F2C_TOKEN_ARRAY_END) &&
                   depth != 0U) {
            --depth;
        }
        ++index;
    }
    return 1;
}

static void free_header(Unit *unit) {
    size_t index;
    free(unit->name);
    free(unit->result_name);
    free(unit->result_character_length);
    for (index = 0U; index < unit->argument_count; ++index)
        free(unit->arguments[index]);
    free(unit->arguments);
    memset(unit, 0, sizeof(*unit));
}

int f2c_parse_unit_header_tokens(const Line *line, Unit *unit) {
    size_t index = 0U;
    size_t close = SIZE_MAX;
    Type prefix = TYPE_UNKNOWN;
    memset(unit, 0, sizeof(*unit));
    unit->return_type = TYPE_REAL;
    unit->return_kind = f2c_default_kind(TYPE_REAL);
    if (line == NULL || line->token_count == 0U)
        return 0;
    while (index < line->token_count) {
        F2cDeclarationTypeSpec candidate;
        if (header_attribute(line, index, unit)) {
            ++index;
            continue;
        }
        if (prefix == TYPE_UNKNOWN &&
            f2c_parse_type_spec_tokens(NULL, NULL, line, index, &candidate)) {
            prefix = candidate.type;
            unit->return_kind = candidate.kind;
            if (candidate.character_length != NULL) {
                unit->result_character_length = candidate.character_length;
                candidate.character_length = NULL;
            }
            index = candidate.end;
            f2c_release_type_spec(&candidate);
            continue;
        }
        break;
    }
    unit->return_type_explicit = prefix != TYPE_UNKNOWN;
    while (index < line->token_count && header_attribute(line, index, unit))
        ++index;
    if (f2c_line_token_equals(line, index, "program")) {
        unit->kind = UNIT_PROGRAM;
    } else if (f2c_line_token_equals(line, index, "subroutine")) {
        unit->kind = UNIT_SUBROUTINE;
    } else if (f2c_line_token_equals(line, index, "function")) {
        unit->kind = UNIT_FUNCTION;
        unit->return_type = prefix == TYPE_UNKNOWN ? TYPE_REAL : prefix;
    } else {
        free_header(unit);
        return 0;
    }
    ++index;
    if (index >= line->token_count || line->tokens[index].kind != F2C_TOKEN_IDENTIFIER)
        goto failed;
    unit->name = f2c_token_text(&line->tokens[index++]);
    if (unit->name == NULL)
        goto failed;
    if (index < line->token_count && line->tokens[index].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!f2c_token_matching_delimiter(line->tokens, line->token_count, index, &close) ||
            !parse_arguments(line, index, close, unit))
            goto failed;
        index = close + 1U;
    }
    if (unit->kind == UNIT_FUNCTION) {
        size_t result = f2c_line_find_token(line, index, F2C_TOKEN_IDENTIFIER, "result");
        if (result != SIZE_MAX && result + 3U < line->token_count &&
            line->tokens[result + 1U].kind == F2C_TOKEN_LEFT_PAREN &&
            line->tokens[result + 2U].kind == F2C_TOKEN_IDENTIFIER &&
            line->tokens[result + 3U].kind == F2C_TOKEN_RIGHT_PAREN)
            unit->result_name = f2c_token_text(&line->tokens[result + 2U]);
        if (unit->result_name == NULL)
            unit->result_name = f2c_strdup(unit->name);
        if (unit->result_name == NULL)
            goto failed;
    }
    return 1;

failed:
    free_header(unit);
    return 0;
}

int f2c_parse_unit_header(const char *text, Unit *unit) {
    F2cTokenStream stream;
    F2cToken *tokens = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    Line line = {0};
    int result;
    f2c_token_stream_init(&stream, text, 1U, 1U);
    for (;;) {
        F2cToken *replacement;
        f2c_token_stream_next(&stream);
        if (stream.token.kind == F2C_TOKEN_END)
            break;
        if (stream.token.kind == F2C_TOKEN_INVALID)
            goto failed;
        if (count == capacity) {
            capacity = capacity == 0U ? 16U : capacity * 2U;
            replacement = (F2cToken *)realloc(tokens, capacity * sizeof(*tokens));
            if (replacement == NULL)
                goto failed;
            tokens = replacement;
        }
        tokens[count++] = stream.token;
    }
    line.text = (char *)(text != NULL ? text : "");
    line.tokens = tokens;
    line.token_count = count;
    result = f2c_parse_unit_header_tokens(&line, unit);
    free(tokens);
    return result;

failed:
    free(tokens);
    memset(unit, 0, sizeof(*unit));
    return 0;
}
