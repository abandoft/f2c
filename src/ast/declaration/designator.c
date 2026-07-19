#include "ast/declaration/designator.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned char ascii_lower(unsigned char value) {
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z')
        return (unsigned char)(value - (unsigned char)'A' + (unsigned char)'a');
    return value;
}

static int tokens_equal(const F2cToken *left, const F2cToken *right) {
    size_t index;
    if (left == NULL || right == NULL || left->kind != right->kind || left->length != right->length)
        return 0;
    for (index = 0U; index < left->length; ++index) {
        if (ascii_lower((unsigned char)left->begin[index]) !=
            ascii_lower((unsigned char)right->begin[index]))
            return 0;
    }
    return 1;
}

static int parse_parenthesized_designator(const Line *line, size_t begin, size_t *end,
                                          F2cGenericDesignatorSyntax *designator) {
    const F2cToken *keyword = &line->tokens[begin];
    size_t close;
    if (begin + 1U >= line->token_count || line->tokens[begin + 1U].kind != F2C_TOKEN_LEFT_PAREN ||
        !f2c_token_matching_delimiter(line->tokens, line->token_count, begin + 1U, &close) ||
        close != begin + 3U)
        return 0;
    if (f2c_token_equals(keyword, "operator")) {
        if (line->tokens[begin + 2U].kind != F2C_TOKEN_OPERATOR ||
            f2c_token_equals(&line->tokens[begin + 2U], "=>"))
            return 0;
        designator->kind = F2C_GENERIC_DESIGNATOR_OPERATOR;
    } else if (f2c_token_equals(keyword, "assignment")) {
        if (line->tokens[begin + 2U].kind != F2C_TOKEN_OPERATOR ||
            !f2c_token_equals(&line->tokens[begin + 2U], "="))
            return 0;
        designator->kind = F2C_GENERIC_DESIGNATOR_ASSIGNMENT;
    } else if (f2c_token_equals(keyword, "read") || f2c_token_equals(keyword, "write")) {
        if (line->tokens[begin + 2U].kind != F2C_TOKEN_IDENTIFIER ||
            (!f2c_token_equals(&line->tokens[begin + 2U], "formatted") &&
             !f2c_token_equals(&line->tokens[begin + 2U], "unformatted")))
            return 0;
        designator->kind = F2C_GENERIC_DESIGNATOR_DEFINED_IO;
    } else {
        return 0;
    }
    *end = close + 1U;
    return 1;
}

F2cGenericDesignatorStatus
f2c_parse_generic_designator_syntax(const Line *line, size_t begin, size_t *end,
                                    F2cGenericDesignatorSyntax *designator) {
    if (line == NULL || end == NULL || designator == NULL)
        return F2C_GENERIC_DESIGNATOR_INVALID;
    memset(designator, 0, sizeof(*designator));
    if (begin >= line->token_count || line->tokens[begin].kind != F2C_TOKEN_IDENTIFIER)
        return F2C_GENERIC_DESIGNATOR_NOT_MATCHED;
    if ((f2c_token_equals(&line->tokens[begin], "operator") ||
         f2c_token_equals(&line->tokens[begin], "assignment") ||
         f2c_token_equals(&line->tokens[begin], "read") ||
         f2c_token_equals(&line->tokens[begin], "write")) &&
        begin + 1U < line->token_count && line->tokens[begin + 1U].kind == F2C_TOKEN_LEFT_PAREN) {
        if (!parse_parenthesized_designator(line, begin, end, designator))
            return F2C_GENERIC_DESIGNATOR_INVALID;
    } else {
        designator->kind = F2C_GENERIC_DESIGNATOR_NAME;
        designator->name = &line->tokens[begin];
        *end = begin + 1U;
    }
    designator->range = f2c_line_token_range(line, begin, *end);
    designator->span =
        f2c_source_span_cover(&line->tokens[begin].span, &line->tokens[*end - 1U].span);
    return F2C_GENERIC_DESIGNATOR_PARSED;
}

int f2c_generic_designators_equal(const F2cGenericDesignatorSyntax *left,
                                  const F2cGenericDesignatorSyntax *right) {
    size_t index;
    if (left == NULL || right == NULL || left->kind != right->kind ||
        left->range.count != right->range.count)
        return 0;
    for (index = 0U; index < left->range.count; ++index) {
        if (!tokens_equal(&left->range.tokens[index], &right->range.tokens[index]))
            return 0;
    }
    return 1;
}

static int copy_lower(char *destination, size_t *offset, const F2cToken *token) {
    size_t index;
    if (destination == NULL || offset == NULL || token == NULL)
        return 0;
    for (index = 0U; index < token->length; ++index) {
        const unsigned char value = (unsigned char)token->begin[index];
        if (value != (unsigned char)' ' && value != (unsigned char)'\t' &&
            value != (unsigned char)'\r' && value != (unsigned char)'\n')
            destination[(*offset)++] = (char)ascii_lower(value);
    }
    return 1;
}

char *f2c_generic_operator_key(const char *operator_text) {
    const size_t source_length = operator_text != NULL ? strlen(operator_text) : 0U;
    const char prefix[] = "operator(";
    size_t offset = sizeof(prefix) - 1U;
    size_t index;
    char *key;
    if (source_length > SIZE_MAX - sizeof(prefix) - 1U)
        return NULL;
    key = (char *)malloc(sizeof(prefix) + source_length + 1U);
    if (key == NULL)
        return NULL;
    memcpy(key, prefix, sizeof(prefix) - 1U);
    for (index = 0U; index < source_length; ++index) {
        const unsigned char value = (unsigned char)operator_text[index];
        if (value != (unsigned char)' ' && value != (unsigned char)'\t' &&
            value != (unsigned char)'\r' && value != (unsigned char)'\n')
            key[offset++] = (char)ascii_lower(value);
    }
    key[offset++] = ')';
    key[offset] = '\0';
    return key;
}

char *f2c_generic_designator_key(const F2cGenericDesignatorSyntax *designator) {
    const F2cToken *value;
    const char *prefix;
    size_t prefix_length;
    size_t length;
    size_t offset = 0U;
    char *key;
    if (designator == NULL || designator->range.count == 0U)
        return NULL;
    if (designator->kind == F2C_GENERIC_DESIGNATOR_NAME) {
        value = designator->name;
        prefix = "";
        prefix_length = 0U;
    } else {
        if (designator->range.count < 4U)
            return NULL;
        value = &designator->range.tokens[2U];
        switch (designator->kind) {
        case F2C_GENERIC_DESIGNATOR_OPERATOR:
            prefix = "operator(";
            break;
        case F2C_GENERIC_DESIGNATOR_ASSIGNMENT:
            prefix = "assignment(";
            break;
        case F2C_GENERIC_DESIGNATOR_DEFINED_IO:
            prefix = f2c_token_equals(&designator->range.tokens[0U], "read") ? "read(" : "write(";
            break;
        case F2C_GENERIC_DESIGNATOR_NAME:
            prefix = "";
            break;
        }
        prefix_length = strlen(prefix);
    }
    if (value == NULL || prefix_length > SIZE_MAX - value->length)
        return NULL;
    length = prefix_length + value->length;
    if (prefix_length != 0U) {
        if (length == SIZE_MAX)
            return NULL;
        ++length;
    }
    if (length == SIZE_MAX)
        return NULL;
    key = (char *)malloc(length + 1U);
    if (key == NULL)
        return NULL;
    if (prefix_length != 0U) {
        memcpy(key, prefix, prefix_length);
        offset = prefix_length;
    }
    if (!copy_lower(key, &offset, value)) {
        free(key);
        return NULL;
    }
    if (prefix_length != 0U)
        key[offset++] = ')';
    key[offset] = '\0';
    return key;
}
