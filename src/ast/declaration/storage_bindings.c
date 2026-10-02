#include "ast/declaration/bindings_private.h"

#include <stdlib.h>

static int operator_token(const F2cToken *token, const char *text) {
    return token->kind == F2C_TOKEN_OPERATOR && f2c_token_equals(token, text);
}

typedef struct TargetFrame {
    F2cTokenKind closer;
    int grouped;
    int mode; /* 0: next target, 1: designator tail, 2: opaque selector/control */
} TargetFrame;

static int target_names(F2cTokenRange range, F2cDeclarationBindingsSyntax *syntax) {
    TargetFrame *frames = NULL;
    size_t count = 1U;
    size_t capacity = 8U;
    size_t position;
    int status = 1;
    frames = (TargetFrame *)calloc(capacity, sizeof(*frames));
    if (frames == NULL)
        return -1;
    frames[0].grouped = 1;
    for (position = 0U; position < range.count; ++position) {
        const F2cToken *token = &range.tokens[position];
        TargetFrame *frame = &frames[count - 1U];
        if (token->kind == F2C_TOKEN_RIGHT_PAREN || token->kind == F2C_TOKEN_RIGHT_BRACKET ||
            token->kind == F2C_TOKEN_ARRAY_END) {
            if (count == 1U || frame->closer != token->kind) {
                status = 0;
                break;
            }
            --count;
        } else if (token->kind == F2C_TOKEN_LEFT_PAREN || token->kind == F2C_TOKEN_LEFT_BRACKET ||
                   token->kind == F2C_TOKEN_ARRAY_BEGIN) {
            const int grouped = frame->mode == 0 && token->kind == F2C_TOKEN_LEFT_PAREN;
            const F2cTokenKind closer = token->kind == F2C_TOKEN_LEFT_PAREN ? F2C_TOKEN_RIGHT_PAREN
                                        : token->kind == F2C_TOKEN_LEFT_BRACKET
                                            ? F2C_TOKEN_RIGHT_BRACKET
                                            : F2C_TOKEN_ARRAY_END;
            if (frame->mode == 0)
                frame->mode = 1;
            if (count == capacity) {
                TargetFrame *replacement;
                size_t next = capacity * 2U;
                if (next < capacity || next > SIZE_MAX / sizeof(*replacement)) {
                    status = -1;
                    break;
                }
                replacement = (TargetFrame *)realloc(frames, next * sizeof(*frames));
                if (replacement == NULL) {
                    status = -1;
                    break;
                }
                frames = replacement;
                capacity = next;
            }
            frames[count].closer = closer;
            frames[count].grouped = grouped;
            frames[count].mode = grouped ? 0 : 2;
            ++count;
        } else if (frame->mode == 0) {
            if (token->kind != F2C_TOKEN_IDENTIFIER) {
                status = 0;
                break;
            }
            if (position + 1U < range.count && operator_token(&range.tokens[position + 1U], "=")) {
                frame->mode = 2; /* DATA implied-DO control is not an initialized object. */
            } else {
                if (!f2c_binding_name_append(syntax, token)) {
                    status = -1;
                    break;
                }
                frame->mode = 1;
            }
        } else if (token->kind == F2C_TOKEN_COMMA && frame->grouped && frame->mode != 2) {
            frame->mode = 0;
        }
    }
    if (count != 1U && status > 0)
        status = 0;
    free(frames);
    return status;
}

static int common_names(F2cTokenRange range, F2cDeclarationBindingsSyntax *syntax) {
    size_t position = 0U;
    while (position < range.count) {
        const F2cToken *token = &range.tokens[position];
        if (token->kind == F2C_TOKEN_COMMA) {
            ++position;
        } else if (operator_token(token, "//")) {
            ++position;
        } else if (operator_token(token, "/")) {
            ++position;
            if (position < range.count && range.tokens[position].kind == F2C_TOKEN_IDENTIFIER)
                ++position;
            if (position >= range.count || !operator_token(&range.tokens[position], "/"))
                return 0;
            ++position;
        } else if (token->kind == F2C_TOKEN_IDENTIFIER) {
            size_t close;
            if (!f2c_binding_name_append(syntax, token))
                return -1;
            ++position;
            if (position < range.count && range.tokens[position].kind == F2C_TOKEN_LEFT_PAREN) {
                if (!f2c_token_matching_delimiter(range.tokens, range.count, position, &close))
                    return 0;
                position = close + 1U;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

int f2c_storage_bindings_syntax(const Line *line, size_t start,
                                F2cDeclarationBindingsSyntax *syntax) {
    F2cTokenRange range = f2c_line_token_range(line, start + 1U, line->token_count);
    size_t position = 0U;
    if (range.count == 0U ||
        f2c_token_range_find_top_level(range, 0U, F2C_TOKEN_OPERATOR, "=") != SIZE_MAX ||
        f2c_token_range_find_top_level(range, 0U, F2C_TOKEN_OPERATOR, "=>") != SIZE_MAX)
        return 0;
    if (f2c_token_equals(&line->tokens[start], "common"))
        return common_names(range, syntax);
    if (f2c_token_equals(&line->tokens[start], "equivalence"))
        return target_names(range, syntax);
    while (position < range.count) {
        size_t first = f2c_token_range_find_top_level(range, position, F2C_TOKEN_OPERATOR, "/");
        size_t second;
        int status;
        if (first == SIZE_MAX || first == position)
            return 0;
        second = f2c_token_range_find_top_level(range, first + 1U, F2C_TOKEN_OPERATOR, "/");
        if (second == SIZE_MAX)
            return 0;
        status = target_names(f2c_token_range_slice(range, position, first), syntax);
        if (status <= 0)
            return status;
        position = second + 1U;
        if (position < range.count && range.tokens[position].kind == F2C_TOKEN_COMMA)
            ++position;
    }
    return 1;
}
