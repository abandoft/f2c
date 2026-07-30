#include "frontend/declaration/private.h"

#include "ast/declaration/access.h"

#include <stdlib.h>

static int declaration_token_word(const Line *line, size_t index, const char *word) {
    return line != NULL && index < line->token_count &&
           line->tokens[index].kind == F2C_TOKEN_IDENTIFIER &&
           f2c_line_token_equals(line, index, word);
}

static int starts_with_assignment_designator(const Line *line, size_t start) {
    size_t depth = 0U;
    size_t index;
    int saw_double_colon = 0;
    for (index = start; index < line->token_count; ++index) {
        const F2cToken *token = &line->tokens[index];
        if (token->kind == F2C_TOKEN_LEFT_PAREN || token->kind == F2C_TOKEN_LEFT_BRACKET ||
            token->kind == F2C_TOKEN_ARRAY_BEGIN) {
            ++depth;
        } else if (token->kind == F2C_TOKEN_RIGHT_PAREN ||
                   token->kind == F2C_TOKEN_RIGHT_BRACKET ||
                   token->kind == F2C_TOKEN_ARRAY_END) {
            if (depth != 0U)
                --depth;
        } else if (depth == 0U && token->kind == F2C_TOKEN_DOUBLE_COLON) {
            saw_double_colon = 1;
        } else if (depth == 0U && token->kind == F2C_TOKEN_OPERATOR &&
                   (f2c_token_equals(token, "=") || f2c_token_equals(token, "=>"))) {
            return !saw_double_colon;
        }
    }
    return 0;
}

int f2c_declaration_tokens(const Line *line) {
    static const char *const starters[] = {
        "integer",   "real",     "double",   "logical",     "complex",  "character", "dimension",
        "parameter", "implicit", "external", "intrinsic",   "optional", "procedure", "import",
        "interface", "abstract", "save",     "equivalence", "common",   "namelist",  "contiguous",
    };
    size_t index;
    size_t start;
    if (line == NULL || line->token_count == 0U)
        return 0;
    start = line->token_count > 1U && line->tokens[0].kind == F2C_TOKEN_NUMBER ? 1U : 0U;
    if (starts_with_assignment_designator(line, start))
        return 0;
    if (declaration_token_word(line, start, "end") &&
        (declaration_token_word(line, start + 1U, "interface") ||
         declaration_token_word(line, start + 1U, "type")))
        return 1;
    if (declaration_token_word(line, start, "type"))
        return start + 1U < line->token_count &&
               (line->tokens[start + 1U].kind == F2C_TOKEN_LEFT_PAREN ||
                line->tokens[start + 1U].kind == F2C_TOKEN_COMMA ||
                line->tokens[start + 1U].kind == F2C_TOKEN_DOUBLE_COLON ||
                line->tokens[start + 1U].kind == F2C_TOKEN_IDENTIFIER);
    if (declaration_token_word(line, start, "class"))
        return start + 1U < line->token_count &&
               line->tokens[start + 1U].kind == F2C_TOKEN_LEFT_PAREN;
    if (f2c_access_statement_candidate(line))
        return 1;
    for (index = 0U; index < sizeof(starters) / sizeof(starters[0]); ++index) {
        if (declaration_token_word(line, start, starters[index]))
            return 1;
    }
    return 0;
}

int f2c_declaration_line(const char *text) {
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
            const size_t next_capacity = capacity == 0U ? 16U : capacity * 2U;
            replacement =
                next_capacity >= capacity && next_capacity <= SIZE_MAX / sizeof(*replacement)
                    ? (F2cToken *)realloc(tokens, next_capacity * sizeof(*replacement))
                    : NULL;
            if (replacement == NULL)
                goto failed;
            tokens = replacement;
            capacity = next_capacity;
        }
        tokens[count++] = stream.token;
    }
    line.text = (char *)(text != NULL ? text : "");
    line.tokens = tokens;
    line.token_count = count;
    result = f2c_declaration_tokens(&line);
    free(tokens);
    return result;

failed:
    free(tokens);
    return 0;
}
